/*
 * PipeWire equalizer path modeled on EasyEffects:
 *   Stream/Output/Audio  --(metadata target.*)-->  audiohawk.eq
 *   audiohawk.eq monitors → pw_filter (EQ + FX) → hardware sink
 *
 * Never become default.audio.sink (priority.session=0). WirePlumber moves
 * streams when we set target.node / target.object on the default metadata.
 */

#include "audiohawk/pipewire_eq.h"
#include "audiohawk/advanced.h"
#include "audiohawk/config.h"
#include "audiohawk/dsp.h"
#include "audiohawk/effects.h"
#include "audiohawk/pitch.h"
#include "audiohawk/spatial.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/filter.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AH_NODE_NAME "audiohawk.eq"
#define AH_FILTER_NAME "audiohawk.eq.filter"
#define AH_NODE_GROUP "ah_sink_group"
#define AH_MAX_PORTS 512
#define AH_MAX_NODES 256
#define AH_MAX_LINKS 16
#define AH_MAX_STREAMS 128

/* Fixed slots after the EQ bands: bass HP, shelf, punch, cut, mid, treble. */
#define AH_TONE_SLOTS 6
#define AH_CHAIN_MAX (AH_EQ_BANDS_MAX + AH_TONE_SLOTS)

typedef struct {
    uint32_t rate;
    int n_chain;
    AhBiquad chain[AH_CHAIN_MAX]; /* EQ bands then tone slots, stereo */
    AhBiquad bass_lp;             /* harmonic blend source */
    AhBiquad dlg[2];              /* dialogue lift + scoop, mid channel only */
    bool dlg_on;
    bool lev_on;
    float harm;
    AhSpatialParams spatial;
    float pre_gain;    /* auto preamp: cancels the peak boost of the whole chain */
    float volume_gain; /* 1.0..2.0 from volume_boost %, caught by the limiter */
    float k_det, k_dn, k_up, k_s;
    int pitch;
} AhDspBank;

typedef struct {
    uint32_t id;
    uint32_t node_id;
    uint32_t port_id;
    char name[64];
    char channel[32];
    char direction[8]; /* "in" / "out" */
    bool monitor;
} AhPort;

typedef struct {
    uint32_t id;
    uint64_t serial;
    char name[256];
    char media_class[64];
    int priority;
} AhNode;

typedef struct {
    uint32_t id;
    uint64_t serial;
    bool route; /* false = blocklisted / pinned elsewhere */
} AhStream;

struct AhPipewireEq {
    bool enabled;      /* graph currently armed */
    bool want_enabled; /* user intent — survives disconnect / suspend */
    bool eq_active;    /* graphic EQ gains applied */
    int n_bands;
    AhEqBand bands[AH_EQ_BANDS_MAX];
    AhEffectsState fx;
    AhAdvancedState adv_state;
    AhAdvancedEngine *adv;
    char last_error[256];

    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct spa_hook core_listener;
    struct spa_hook registry_listener;

    struct pw_metadata *metadata;
    struct spa_hook metadata_listener;
    bool have_metadata;

    struct pw_proxy *sink_proxy;
    uint32_t sink_id;
    uint64_t sink_serial;

    struct pw_filter *filter;
    struct spa_hook filter_listener;
    void *port_in_l;
    void *port_in_r;
    void *port_out_l;
    void *port_out_r;
    uint32_t filter_id;
    bool filter_connected;

    struct pw_proxy *links[AH_MAX_LINKS];
    int n_links;

    AhPort ports[AH_MAX_PORTS];
    int n_ports;
    AhNode nodes[AH_MAX_NODES];
    int n_nodes;
    AhStream streams[AH_MAX_STREAMS];
    int n_streams;

    char default_sink[256];
    char hardware_sink[256];
    uint32_t hardware_sink_id;

    /* Double-buffered coeffs; dsp_lock serializes writers, busy guards the RT read. */
    AhDspBank banks[2];
    atomic_int dsp_idx;
    atomic_int dsp_busy;
    pthread_mutex_t dsp_lock;
    AhBiquadMem mem[2][AH_CHAIN_MAX];
    AhBiquadMem lp_mem[2];
    AhBiquadMem dlg_mem[2];
    AhSpatialState spatial;
    AhPitchState pitch;
    AhLimiter lim;
    float pre_g, vol_g, lev_g, lev_ms;

    uint32_t process_rate;

    int sync_seq;
    int pending_res;

    atomic_bool disconnected;
    atomic_bool shutting_down;
    atomic_bool recover_busy;
    pthread_t recover_thread;
    bool recover_started;
};

/* ---------- helpers ---------- */

const char *ah_pw_eq_node_name(void)
{
    return AH_NODE_NAME;
}

const char *ah_pw_eq_last_error(const AhPipewireEq *eq)
{
    return eq ? eq->last_error : "null eq";
}

static void set_err(AhPipewireEq *eq, const char *msg)
{
    snprintf(eq->last_error, sizeof(eq->last_error), "%s", msg ? msg : "unknown");
}

static void on_core_done(void *data, uint32_t id, int seq)
{
    AhPipewireEq *eq = data;
    (void)id;
    if ((int)seq == eq->sync_seq)
        pw_thread_loop_signal(eq->loop, false);
}

static void mark_disconnected(AhPipewireEq *eq, const char *why)
{
    if (atomic_exchange(&eq->disconnected, true))
        return;
    eq->enabled = false;
    set_err(eq, why && why[0] ? why : "PipeWire connection lost");
    if (eq->loop)
        pw_thread_loop_signal(eq->loop, false);
}

static void on_core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
    AhPipewireEq *eq = data;
    (void)seq;
    eq->pending_res = res;
    set_err(eq, message ? message : spa_strerror(res));
    /* Core connection died (common after suspend/resume or PW restart). */
    if (id == PW_ID_CORE)
        mark_disconnected(eq, message ? message : "PipeWire core error");
    if (eq->loop)
        pw_thread_loop_signal(eq->loop, false);
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
    .error = on_core_error,
};

static int core_sync(AhPipewireEq *eq)
{
    if (!eq->core || atomic_load(&eq->disconnected))
        return -EPIPE;
    eq->pending_res = 0;
    eq->sync_seq = pw_core_sync(eq->core, PW_ID_CORE, 0);
    pw_thread_loop_wait(eq->loop);
    if (atomic_load(&eq->disconnected))
        return -EPIPE;
    return eq->pending_res;
}

/* ---------- DSP banks ---------- */

static float soft_saturate(float x)
{
    /* Used only for bass harmonics — keep light so punch stays sharp. */
    if (x > 1.35f)
        x = 1.35f;
    if (x < -1.35f)
        x = -1.35f;
    return x - (x * x * x) * (1.f / 9.f);
}

static float tc(double sec, uint32_t rate)
{
    return (float)(1.0 - exp(-1.0 / (sec * rate)));
}

/* Snaps once float rounding stalls the step, so unity really lands on 1.0. */
static float glide(float g, float target, float k)
{
    const float n = g + (target - g) * k;
    return (n == g || fabsf(target - n) < 1e-6f) ? target : n;
}

static void rebuild_dsp_locked(AhPipewireEq *eq, uint32_t rate)
{
    AhEffectsState fx = eq->fx;
    ah_effects_clamp(&fx);

    const int wi = ah_ui_slot(&eq->dsp_idx, &eq->dsp_busy);
    AhDspBank *b = &eq->banks[wi];
    memset(b, 0, sizeof(*b));
    b->rate = rate;

    int nb = eq->n_bands > 0 ? eq->n_bands : 10;
    if (nb > AH_EQ_BANDS_MAX)
        nb = AH_EQ_BANDS_MAX;
    b->n_chain = nb + AH_TONE_SLOTS;
    for (int i = 0; i < b->n_chain; ++i)
        ah_bq_unity(&b->chain[i]);
    if (eq->eq_active)
        ah_geq_design(eq->bands, nb, rate, b->chain);

    AhBiquad *t = b->chain + nb; /* 0 HP, 1 shelf, 2 punch, 3 cut, 4 mid, 5 treble */
    const double bass = fx.bass_enhancer ? fx.bass_level / 100.0 : 0.0;
    const double mid = fx.mid_enhancer ? fx.mid_level / 100.0 : 0.0;
    const double treble = fx.treble_enhancer ? fx.treble_level / 100.0 : 0.0;
    const double dlg = fx.dialogue_enhancer ? fx.dialogue_strength / 10.0 : 0.0;

    if (bass > 0.01) {
        /* Subsonic guard so the shelf spends headroom on audible bass, not rumble. */
        ah_bq_highpass(&t[0], 18.0, 0.707, rate);
        switch (fx.bass_curve) {
        case AH_BASS_CURVE_SUB_BOOST:
            ah_bq_lowshelf(&t[1], 50.0, 0.707, 9.0 * bass, rate);
            b->harm = (float)(0.08 * bass);
            break;
        case AH_BASS_CURVE_PUNCH:
            ah_bq_peaking(&t[2], 90.0, 1.2, 7.0 * bass, rate);
            ah_bq_peaking(&t[3], 300.0, 1.0, -2.0 * bass, rate);
            b->harm = (float)(0.05 * bass);
            break;
        case AH_BASS_CURVE_BALANCED:
        default:
            ah_bq_lowshelf(&t[1], 90.0, 0.707, 8.0 * bass, rate);
            b->harm = (float)(0.04 * bass);
            break;
        }
        ah_bq_lowpass(&b->bass_lp, 180.0, 0.707, rate);
    }
    if (mid > 0.01)
        ah_bq_peaking(&t[4], 1000.0, 0.7, 4.5 * mid, rate);
    if (treble > 0.01)
        ah_bq_highshelf(&t[5], 8000.0, 0.707, 6.0 * treble, rate);
    b->dlg_on = dlg > 0.01;
    if (b->dlg_on) {
        ah_bq_peaking(&b->dlg[0], 2500.0, 1.0, 6.0 * dlg, rate);
        ah_bq_peaking(&b->dlg[1], 250.0, 0.9, -2.0 * dlg, rate);
    }

    ah_spatial_configure(&b->spatial, rate, fx.surround_virtualizer,
                         fx.surround_amount, fx.surround_bass, fx.surround_treble);

    /* Auto preamp: the loudest point of the summed curve lands at 0 dB. */
    const double top = fmin(20000.0, rate * 0.45);
    double peak = 0.0;
    for (int k = 0; k < 96; ++k) {
        const double f = 20.0 * pow(top / 20.0, k / 95.0);
        double db = 0.0;
        for (int i = 0; i < b->n_chain; ++i)
            db += ah_bq_mag_db(&b->chain[i], f, rate);
        if (b->harm > 0.f)
            db += 20.0 * log10(1.0 + 1.15 * b->harm *
                                         pow(10.0, ah_bq_mag_db(&b->bass_lp, f, rate) / 20.0));
        if (b->dlg_on)
            db += ah_bq_mag_db(&b->dlg[0], f, rate) + ah_bq_mag_db(&b->dlg[1], f, rate);
        if (b->spatial.enabled)
            db += ah_bq_mag_db(&b->spatial.bass, f, rate) +
                  ah_bq_mag_db(&b->spatial.treble, f, rate);
        if (db > peak)
            peak = db;
    }
    b->pre_gain = (float)pow(10.0, -peak / 20.0);

    b->lev_on = fx.volume_leveler;
    b->volume_gain = fx.volume_boost / 100.f;
    if (b->volume_gain < 1.f)
        b->volume_gain = 1.f;
    if (b->volume_gain > 2.f)
        b->volume_gain = 2.f;
    b->k_det = tc(0.4, rate);
    b->k_dn = tc(1.0, rate);
    b->k_up = tc(3.0, rate);
    b->k_s = tc(0.02, rate);
    b->pitch = fx.pitch;

    atomic_store(&eq->dsp_idx, wi);
}

static void rebuild_dsp(AhPipewireEq *eq)
{
    pthread_mutex_lock(&eq->dsp_lock);
    rebuild_dsp_locked(eq, eq->process_rate ? eq->process_rate : 48000);
    pthread_mutex_unlock(&eq->dsp_lock);
}

static void reset_dsp_state(AhPipewireEq *eq, uint32_t rate)
{
    memset(eq->mem, 0, sizeof(eq->mem));
    memset(eq->lp_mem, 0, sizeof(eq->lp_mem));
    memset(eq->dlg_mem, 0, sizeof(eq->dlg_mem));
    memset(&eq->spatial, 0, sizeof(eq->spatial));
    ah_pitch_reset(&eq->pitch);
    ah_limiter_init(&eq->lim, rate, 0.f, 1.5f, 60.f, false);
    eq->pre_g = 0.f; /* 20 ms fade-in instead of a start click */
    eq->vol_g = 1.f;
    eq->lev_g = 1.f;
    eq->lev_ms = 0.04f;
    if (eq->adv)
        ah_advanced_reset(eq->adv);
}

/* ---------- registry bookkeeping ---------- */

static AhNode *find_node_id(AhPipewireEq *eq, uint32_t id)
{
    for (int i = 0; i < eq->n_nodes; ++i) {
        if (eq->nodes[i].id == id)
            return &eq->nodes[i];
    }
    return NULL;
}

static AhNode *find_node_name(AhPipewireEq *eq, const char *name)
{
    if (!name || !name[0])
        return NULL;
    for (int i = 0; i < eq->n_nodes; ++i) {
        if (strcmp(eq->nodes[i].name, name) == 0)
            return &eq->nodes[i];
    }
    return NULL;
}

static void remove_ports_for_node(AhPipewireEq *eq, uint32_t node_id)
{
    int w = 0;
    for (int i = 0; i < eq->n_ports; ++i) {
        if (eq->ports[i].node_id != node_id)
            eq->ports[w++] = eq->ports[i];
    }
    eq->n_ports = w;
}

static void remove_node(AhPipewireEq *eq, uint32_t id)
{
    remove_ports_for_node(eq, id);
    for (int i = 0; i < eq->n_nodes; ++i) {
        if (eq->nodes[i].id == id) {
            eq->nodes[i] = eq->nodes[eq->n_nodes - 1];
            eq->n_nodes--;
            break;
        }
    }
    for (int i = 0; i < eq->n_streams; ++i) {
        if (eq->streams[i].id == id) {
            eq->streams[i] = eq->streams[eq->n_streams - 1];
            eq->n_streams--;
            break;
        }
    }
    if (eq->sink_id == id) {
        eq->sink_id = 0;
        eq->sink_serial = 0;
    }
    if (eq->hardware_sink_id == id)
        eq->hardware_sink_id = 0;
}

/* EasyEffects-style exclusions: meters, notifications, speech, foreign targets. */
static bool stream_should_route(AhPipewireEq *eq, const struct spa_dict *props)
{
    const char *role = spa_dict_lookup(props, PW_KEY_MEDIA_ROLE);
    const char *app_id = spa_dict_lookup(props, PW_KEY_APP_ID);
    const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char *app_name = spa_dict_lookup(props, PW_KEY_APP_NAME);
    const char *capture = spa_dict_lookup(props, PW_KEY_STREAM_CAPTURE_SINK);
    const char *target = spa_dict_lookup(props, PW_KEY_TARGET_OBJECT);

    if (capture && strcmp(capture, "true") == 0)
        return false;

    if (role && (strcmp(role, "event") == 0 || strcmp(role, "Notification") == 0))
        return false;

    if (app_id &&
        (strcmp(app_id, "org.PulseAudio.pavucontrol") == 0 ||
         strcmp(app_id, "org.kde.plasma-pa") == 0))
        return false;

    static const char *const blocked_names[] = {
        "speech-dispatcher",
        "speech-dispatcher-dummy",
        "speech-dispatcher-espeak-ng",
        "pwvucontrol-peak-detect",
        "libcanberra",
        "org.gnome.VolumeControl",
        "GNOME Shell",
        "Mutter",
        "gsd-media-keys",
        "AudioHawk",
        NULL,
    };
    for (int i = 0; blocked_names[i]; ++i) {
        if (name && strstr(name, blocked_names[i]))
            return false;
        if (app_name && strstr(app_name, blocked_names[i]))
            return false;
    }

    /* Respect streams the user/app pinned to another device. */
    if (target && target[0]) {
        bool ok = false;
        if (strcmp(target, AH_NODE_NAME) == 0)
            ok = true;
        if (eq->hardware_sink[0] && strcmp(target, eq->hardware_sink) == 0)
            ok = true;
        if (eq->default_sink[0] && strcmp(target, eq->default_sink) == 0)
            ok = true;

        char *end = NULL;
        unsigned long long serial = strtoull(target, &end, 10);
        if (end && end != target && *end == '\0') {
            if (eq->sink_serial && serial == eq->sink_serial)
                ok = true;
            AhNode *hw = eq->hardware_sink[0] ? find_node_name(eq, eq->hardware_sink) : NULL;
            if (hw && serial == hw->serial)
                ok = true;
        }

        if (!ok)
            return false;
    }

    return true;
}

static void track_output_stream(AhPipewireEq *eq, uint32_t id, uint64_t serial,
                                const struct spa_dict *props)
{
    bool route = stream_should_route(eq, props);
    bool found = false;
    for (int i = 0; i < eq->n_streams; ++i) {
        if (eq->streams[i].id == id) {
            eq->streams[i].serial = serial;
            eq->streams[i].route = route;
            found = true;
            break;
        }
    }
    if (!found && eq->n_streams < AH_MAX_STREAMS) {
        eq->streams[eq->n_streams].id = id;
        eq->streams[eq->n_streams].serial = serial;
        eq->streams[eq->n_streams].route = route;
        eq->n_streams++;
    }
}

static void add_or_update_node(AhPipewireEq *eq, uint32_t id, const struct spa_dict *props)
{
    const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char *mclass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char *serial_s = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
    const char *prio_s = spa_dict_lookup(props, PW_KEY_PRIORITY_SESSION);

    if (!name || !mclass)
        return;

    AhNode *n = find_node_id(eq, id);
    if (!n) {
        if (eq->n_nodes >= AH_MAX_NODES)
            return;
        n = &eq->nodes[eq->n_nodes++];
        memset(n, 0, sizeof(*n));
        n->id = id;
    }

    snprintf(n->name, sizeof(n->name), "%s", name);
    snprintf(n->media_class, sizeof(n->media_class), "%s", mclass);
    n->serial = serial_s ? strtoull(serial_s, NULL, 10) : 0;
    n->priority = prio_s ? atoi(prio_s) : 0;

    if (strcmp(name, AH_NODE_NAME) == 0) {
        eq->sink_id = id;
        eq->sink_serial = n->serial;
    }

    if (strcmp(mclass, "Stream/Output/Audio") == 0)
        track_output_stream(eq, id, n->serial, props);
}

static void add_port(AhPipewireEq *eq, uint32_t id, const struct spa_dict *props)
{
    const char *node_s = spa_dict_lookup(props, PW_KEY_NODE_ID);
    const char *port_s = spa_dict_lookup(props, PW_KEY_PORT_ID);
    const char *name = spa_dict_lookup(props, PW_KEY_PORT_NAME);
    const char *chan = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL);
    const char *dir = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
    const char *mon = spa_dict_lookup(props, PW_KEY_PORT_MONITOR);

    if (!node_s || !dir)
        return;

    /* Replace if already tracked. */
    for (int i = 0; i < eq->n_ports; ++i) {
        if (eq->ports[i].id == id) {
            eq->ports[i].node_id = (uint32_t)atoi(node_s);
            eq->ports[i].port_id = port_s ? (uint32_t)atoi(port_s) : 0;
            snprintf(eq->ports[i].name, sizeof(eq->ports[i].name), "%s", name ? name : "");
            snprintf(eq->ports[i].channel, sizeof(eq->ports[i].channel), "%s", chan ? chan : "");
            snprintf(eq->ports[i].direction, sizeof(eq->ports[i].direction), "%s", dir);
            eq->ports[i].monitor = mon && strcmp(mon, "true") == 0;
            return;
        }
    }

    if (eq->n_ports >= AH_MAX_PORTS)
        return;

    AhPort *p = &eq->ports[eq->n_ports++];
    memset(p, 0, sizeof(*p));
    p->id = id;
    p->node_id = (uint32_t)atoi(node_s);
    p->port_id = port_s ? (uint32_t)atoi(port_s) : 0;
    snprintf(p->name, sizeof(p->name), "%s", name ? name : "");
    snprintf(p->channel, sizeof(p->channel), "%s", chan ? chan : "");
    snprintf(p->direction, sizeof(p->direction), "%s", dir);
    p->monitor = mon && strcmp(mon, "true") == 0;
}

static void remove_port(AhPipewireEq *eq, uint32_t id)
{
    for (int i = 0; i < eq->n_ports; ++i) {
        if (eq->ports[i].id == id) {
            eq->ports[i] = eq->ports[eq->n_ports - 1];
            eq->n_ports--;
            return;
        }
    }
}

static int count_node_ports(AhPipewireEq *eq, uint32_t node_id, const char *dir)
{
    int n = 0;
    for (int i = 0; i < eq->n_ports; ++i) {
        if (eq->ports[i].node_id == node_id &&
            (!dir || strcmp(eq->ports[i].direction, dir) == 0))
            n++;
    }
    return n;
}

/* ---------- metadata (default sink + stream targeting) ---------- */

static int on_metadata_property(void *data, uint32_t id, const char *key,
                                const char *type, const char *value)
{
    AhPipewireEq *eq = data;
    (void)type;
    if (!key)
        return 0;

    if (strcmp(key, "default.audio.sink") == 0 && value && value[0]) {
        /* value is JSON: {"name":"..."} */
        const char *p = strstr(value, "\"name\"");
        if (p) {
            p = strchr(p + 6, '"');
            if (p) {
                p++;
                const char *end = strchr(p, '"');
                if (end && (size_t)(end - p) < sizeof(eq->default_sink)) {
                    char name[256];
                    memcpy(name, p, (size_t)(end - p));
                    name[end - p] = '\0';
                    if (strcmp(name, AH_NODE_NAME) != 0) {
                        snprintf(eq->default_sink, sizeof(eq->default_sink), "%s", name);
                        if (!eq->enabled || !eq->hardware_sink[0])
                            snprintf(eq->hardware_sink, sizeof(eq->hardware_sink), "%s", name);
                    }
                }
            }
        }
        (void)id;
    }
    return 0;
}

static const struct pw_metadata_events metadata_events = {
    PW_VERSION_METADATA_EVENTS,
    .property = on_metadata_property,
};

/* EasyEffects injection: ask WirePlumber to move the stream onto our sink. */
static void set_stream_target(AhPipewireEq *eq, uint32_t stream_id,
                              uint64_t target_serial, uint32_t target_id)
{
    if (!eq->metadata || !target_id || !target_serial)
        return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", target_id);
    pw_metadata_set_property(eq->metadata, stream_id, "target.node", "Spa:Id", buf);
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)target_serial);
    pw_metadata_set_property(eq->metadata, stream_id, "target.object", "Spa:Id", buf);
}

static void clear_stream_target(AhPipewireEq *eq, uint32_t stream_id)
{
    if (!eq->metadata)
        return;
    pw_metadata_set_property(eq->metadata, stream_id, "target.node", NULL, NULL);
    pw_metadata_set_property(eq->metadata, stream_id, "target.object", NULL, NULL);
}

static void route_streams_to_sink(AhPipewireEq *eq)
{
    if (!eq->sink_id || !eq->sink_serial)
        return;
    for (int i = 0; i < eq->n_streams; ++i) {
        if (eq->streams[i].route)
            set_stream_target(eq, eq->streams[i].id, eq->sink_serial, eq->sink_id);
    }
}

static void unroute_streams(AhPipewireEq *eq)
{
    for (int i = 0; i < eq->n_streams; ++i)
        clear_stream_target(eq, eq->streams[i].id);
}

static void on_registry_global(void *data, uint32_t id, uint32_t permissions,
                               const char *type, uint32_t version,
                               const struct spa_dict *props)
{
    AhPipewireEq *eq = data;
    (void)permissions;
    (void)version;

    if (!props || !type)
        return;

    if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        add_or_update_node(eq, id, props);
        if (eq->enabled && eq->sink_id && eq->sink_serial) {
            const char *mclass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
            if (mclass && strcmp(mclass, "Stream/Output/Audio") == 0) {
                for (int i = 0; i < eq->n_streams; ++i) {
                    if (eq->streams[i].id == id && eq->streams[i].route) {
                        set_stream_target(eq, id, eq->sink_serial, eq->sink_id);
                        break;
                    }
                }
            }
        }
    } else if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        add_port(eq, id, props);
    } else if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (name && strcmp(name, "default") == 0 && !eq->metadata) {
            eq->metadata = pw_registry_bind(eq->registry, id, type, PW_VERSION_METADATA, 0);
            if (eq->metadata) {
                pw_metadata_add_listener(eq->metadata, &eq->metadata_listener,
                                         &metadata_events, eq);
                eq->have_metadata = true;
            }
        }
    }
}

static void on_registry_global_remove(void *data, uint32_t id)
{
    AhPipewireEq *eq = data;
    remove_port(eq, id);
    remove_node(eq, id);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

/* ---------- filter DSP ---------- */

static void on_filter_state_changed(void *data, enum pw_filter_state old,
                                    enum pw_filter_state state, const char *error)
{
    AhPipewireEq *eq = data;
    (void)old;
    if (state == PW_FILTER_STATE_ERROR) {
        set_err(eq, error ? error : "filter error");
        /* Keep want_enabled; recovery thread will tear down and re-arm. */
        eq->enabled = false;
        atomic_store(&eq->disconnected, true);
    }
    if (eq->loop)
        pw_thread_loop_signal(eq->loop, false);
}

static void on_filter_process(void *data, struct spa_io_position *position)
{
    AhPipewireEq *eq = data;
    if (!position)
        return;

    ah_dsp_no_denormals();

    const uint32_t n_samples = position->clock.duration;
    const uint32_t rate = (uint32_t)position->clock.rate.denom;
    /* Rare: redesign here, or next cycle if the UI holds the lock right now. */
    if (rate > 0 && rate != eq->process_rate && pthread_mutex_trylock(&eq->dsp_lock) == 0) {
        eq->process_rate = rate;
        rebuild_dsp_locked(eq, rate);
        reset_dsp_state(eq, rate);
        if (eq->adv)
            ah_advanced_apply(eq->adv, &eq->adv_state, rate);
        pthread_mutex_unlock(&eq->dsp_lock);
    }

    float *in_l = pw_filter_get_dsp_buffer(eq->port_in_l, n_samples);
    float *in_r = pw_filter_get_dsp_buffer(eq->port_in_r, n_samples);
    float *out_l = pw_filter_get_dsp_buffer(eq->port_out_l, n_samples);
    float *out_r = pw_filter_get_dsp_buffer(eq->port_out_r, n_samples);

    if (!out_l || !out_r)
        return;

    const AhDspBank *b = &eq->banks[ah_rt_acquire(&eq->dsp_idx, &eq->dsp_busy)];

    for (uint32_t i = 0; i < n_samples; ++i) {
        eq->pre_g = glide(eq->pre_g, b->pre_gain, b->k_s);
        double l = (in_l ? in_l[i] : 0.f) * eq->pre_g;
        double r = (in_r ? in_r[i] : 0.f) * eq->pre_g;

        for (int k = 0; k < b->n_chain; ++k) {
            l = ah_bq_run(&b->chain[k], &eq->mem[0][k], l);
            r = ah_bq_run(&b->chain[k], &eq->mem[1][k], r);
        }

        if (b->harm > 0.f) {
            l += soft_saturate((float)ah_bq_run(&b->bass_lp, &eq->lp_mem[0], l) * 1.15f) * b->harm;
            r += soft_saturate((float)ah_bq_run(&b->bass_lp, &eq->lp_mem[1], r) * 1.15f) * b->harm;
        }

        /* Voice lives in the center, so lift it there and leave the sides alone. */
        if (b->dlg_on) {
            const double s = 0.5 * (l - r);
            double m = 0.5 * (l + r);
            m = ah_bq_run(&b->dlg[0], &eq->dlg_mem[0], m);
            m = ah_bq_run(&b->dlg[1], &eq->dlg_mem[1], m);
            l = m + s;
            r = m - s;
        }

        float fl = (float)l, fr = (float)r;
        ah_spatial_process(&eq->spatial, &b->spatial, &fl, &fr);

        /* Slow RMS leveler toward -14 dBFS, within -10..+6 dB; holds through silence. */
        if (b->lev_on) {
            eq->lev_ms += (0.5f * (fl * fl + fr * fr) - eq->lev_ms) * b->k_det;
            if (eq->lev_ms > 1e-5f) {
                float want = 0.2f / sqrtf(eq->lev_ms);
                want = want < 0.316f ? 0.316f : (want > 2.f ? 2.f : want);
                eq->lev_g += (want - eq->lev_g) * (want < eq->lev_g ? b->k_dn : b->k_up);
            }
        } else {
            eq->lev_g = glide(eq->lev_g, 1.f, b->k_s);
        }
        eq->vol_g = glide(eq->vol_g, b->volume_gain, b->k_s);
        const float g = eq->lev_g * eq->vol_g;
        fl *= g;
        fr *= g;

        ah_pitch_process(&eq->pitch, b->pitch, &fl, &fr);

        out_l[i] = fl;
        out_r[i] = fr;
    }

    if (eq->adv)
        ah_advanced_process_block(eq->adv, out_l, out_r, n_samples, rate);

    for (uint32_t i = 0; i < n_samples; ++i)
        ah_limiter_tick(&eq->lim, &out_l[i], &out_r[i]);

    ah_rt_release(&eq->dsp_busy);
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = on_filter_state_changed,
    .process = on_filter_process,
};

/* ---------- graph: sink, filter, links ---------- */

static uint32_t resolve_hardware_sink(AhPipewireEq *eq)
{
    if (eq->hardware_sink[0]) {
        AhNode *n = find_node_name(eq, eq->hardware_sink);
        if (n && strcmp(n->name, AH_NODE_NAME) != 0)
            return n->id;
    }
    if (eq->default_sink[0] && strcmp(eq->default_sink, AH_NODE_NAME) != 0) {
        AhNode *n = find_node_name(eq, eq->default_sink);
        if (n)
            return n->id;
    }

    /* Fallback: highest-priority Audio/Sink that isn't ours. */
    int best_prio = -1;
    uint32_t best = 0;
    for (int i = 0; i < eq->n_nodes; ++i) {
        AhNode *n = &eq->nodes[i];
        if (strcmp(n->name, AH_NODE_NAME) == 0)
            continue;
        if (strcmp(n->media_class, "Audio/Sink") != 0 &&
            strcmp(n->media_class, "Audio/Sink/Virtual") != 0)
            continue;
        if (n->priority >= best_prio) {
            best_prio = n->priority;
            best = n->id;
            snprintf(eq->hardware_sink, sizeof(eq->hardware_sink), "%s", n->name);
        }
    }
    return best;
}

static int create_virtual_sink(AhPipewireEq *eq)
{
    if (eq->sink_proxy)
        return 0;

    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_NODE_NAME, AH_NODE_NAME,
        PW_KEY_NODE_DESCRIPTION, "AudioHawk Equalizer",
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_NODE_GROUP, AH_NODE_GROUP,
        PW_KEY_FACTORY_NAME, "support.null-audio-sink",
        "audio.position", "FL,FR",
        /* Honor sink soft-volume on the monitor feed into the DSP. */
        "monitor.channel-volumes", "true",
        "monitor.passthrough", "true",
        /* Stay out of default-device election (EasyEffects pattern). */
        PW_KEY_PRIORITY_SESSION, "0",
        NULL);

    eq->sink_proxy = pw_core_create_object(eq->core,
                                           "adapter",
                                           PW_TYPE_INTERFACE_Node,
                                           PW_VERSION_NODE,
                                           &props->dict,
                                           0);
    pw_properties_free(props);

    if (!eq->sink_proxy) {
        set_err(eq, "failed to create virtual sink");
        return -1;
    }

    core_sync(eq);

    /* Wait for registry to publish the sink + monitor ports. */
    for (int i = 0; i < 50; ++i) {
        if (eq->sink_id && count_node_ports(eq, eq->sink_id, "out") >= 2)
            break;
        pw_thread_loop_unlock(eq->loop);
        usleep(20000);
        pw_thread_loop_lock(eq->loop);
        core_sync(eq);
    }

    if (!eq->sink_id) {
        set_err(eq, "virtual sink did not appear in registry");
        return -1;
    }
    return 0;
}

static int create_eq_filter(AhPipewireEq *eq)
{
    if (eq->filter)
        return 0;

    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Duplex",
        PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_NODE_NAME, AH_FILTER_NAME,
        PW_KEY_NODE_DESCRIPTION, "AudioHawk EQ Filter",
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_NODE_GROUP, AH_NODE_GROUP,
        PW_KEY_NODE_PASSIVE, "true",
        NULL);

    eq->filter = pw_filter_new(eq->core, AH_FILTER_NAME, props);
    if (!eq->filter) {
        set_err(eq, "pw_filter_new failed");
        return -1;
    }

    struct pw_properties *p_in_l = pw_properties_new(
        PW_KEY_FORMAT_DSP, "32 bit float mono audio",
        PW_KEY_PORT_NAME, "input_FL",
        PW_KEY_AUDIO_CHANNEL, "FL",
        NULL);
    eq->port_in_l = pw_filter_add_port(eq->filter, PW_DIRECTION_INPUT,
                                       PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                       0, p_in_l, NULL, 0);

    struct pw_properties *p_in_r = pw_properties_new(
        PW_KEY_FORMAT_DSP, "32 bit float mono audio",
        PW_KEY_PORT_NAME, "input_FR",
        PW_KEY_AUDIO_CHANNEL, "FR",
        NULL);
    eq->port_in_r = pw_filter_add_port(eq->filter, PW_DIRECTION_INPUT,
                                       PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                       0, p_in_r, NULL, 0);

    struct pw_properties *p_out_l = pw_properties_new(
        PW_KEY_FORMAT_DSP, "32 bit float mono audio",
        PW_KEY_PORT_NAME, "output_FL",
        PW_KEY_AUDIO_CHANNEL, "FL",
        NULL);
    eq->port_out_l = pw_filter_add_port(eq->filter, PW_DIRECTION_OUTPUT,
                                        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                        0, p_out_l, NULL, 0);

    struct pw_properties *p_out_r = pw_properties_new(
        PW_KEY_FORMAT_DSP, "32 bit float mono audio",
        PW_KEY_PORT_NAME, "output_FR",
        PW_KEY_AUDIO_CHANNEL, "FR",
        NULL);
    eq->port_out_r = pw_filter_add_port(eq->filter, PW_DIRECTION_OUTPUT,
                                        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                        0, p_out_r, NULL, 0);

    if (pw_filter_connect(eq->filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0) != 0) {
        set_err(eq, "pw_filter_connect failed");
        pw_filter_destroy(eq->filter);
        eq->filter = NULL;
        return -1;
    }

    pw_filter_add_listener(eq->filter, &eq->filter_listener, &filter_events, eq);
    eq->filter_connected = true;
    core_sync(eq);

    for (int i = 0; i < 50; ++i) {
        eq->filter_id = pw_filter_get_node_id(eq->filter);
        if (eq->filter_id != SPA_ID_INVALID &&
            count_node_ports(eq, eq->filter_id, "in") >= 2 &&
            count_node_ports(eq, eq->filter_id, "out") >= 2)
            break;
        pw_thread_loop_unlock(eq->loop);
        usleep(20000);
        pw_thread_loop_lock(eq->loop);
        core_sync(eq);
    }

    if (eq->filter_id == 0 || eq->filter_id == SPA_ID_INVALID) {
        set_err(eq, "EQ filter node id unavailable");
        return -1;
    }
    return 0;
}

static bool channels_match(const char *a, const char *b)
{
    if (!a || !b || !a[0] || !b[0])
        return false;
    if (strcmp(a, "MONO") == 0 || strcmp(b, "MONO") == 0)
        return true;
    return strcmp(a, b) == 0;
}

static void dlog(const char *fmt, ...)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("AUDIOHAWK_DEBUG") != NULL;
    if (!enabled)
        return;
    FILE *f = fopen("/tmp/audiohawk-pw.log", "a");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

static int link_port_pair(AhPipewireEq *eq, uint32_t out_node, uint32_t out_port,
                          uint32_t in_node, uint32_t in_port)
{
    if (eq->n_links >= AH_MAX_LINKS)
        return -1;

    char o_node[16], o_port[16], i_node[16], i_port[16];
    snprintf(o_node, sizeof(o_node), "%u", out_node);
    snprintf(o_port, sizeof(o_port), "%u", out_port);
    snprintf(i_node, sizeof(i_node), "%u", in_node);
    snprintf(i_port, sizeof(i_port), "%u", in_port);

    struct pw_properties *props = pw_properties_new(
        PW_KEY_OBJECT_LINGER, "false",
        PW_KEY_LINK_OUTPUT_NODE, o_node,
        PW_KEY_LINK_OUTPUT_PORT, o_port,
        PW_KEY_LINK_INPUT_NODE, i_node,
        PW_KEY_LINK_INPUT_PORT, i_port,
        NULL);

    eq->pending_res = 0;
    struct pw_proxy *proxy = pw_core_create_object(eq->core,
                                                   "link-factory",
                                                   PW_TYPE_INTERFACE_Link,
                                                   PW_VERSION_LINK,
                                                   &props->dict,
                                                   0);
    pw_properties_free(props);

    if (!proxy) {
        dlog("link create null: %u:%u -> %u:%u\n", out_node, out_port, in_node, in_port);
        return -1;
    }

    core_sync(eq);
    if (eq->pending_res < 0) {
        dlog("link error %d (%s): %u:%u -> %u:%u\n",
             eq->pending_res, eq->last_error, out_node, out_port, in_node, in_port);
        pw_proxy_destroy(proxy);
        return -1;
    }

    dlog("link ok: %u:%u -> %u:%u\n", out_node, out_port, in_node, in_port);
    eq->links[eq->n_links++] = proxy;
    return 0;
}

static void dump_node_ports(AhPipewireEq *eq, uint32_t node_id, const char *label)
{
    dlog("ports for %s (node %u):\n", label, node_id);
    for (int i = 0; i < eq->n_ports; ++i) {
        const AhPort *p = &eq->ports[i];
        if (p->node_id != node_id)
            continue;
        dlog("  id=%u port_id=%u dir=%s ch=%s name=%s mon=%d\n",
             p->id, p->port_id, p->direction, p->channel, p->name, (int)p->monitor);
    }
}

static int link_nodes(AhPipewireEq *eq, uint32_t out_node, uint32_t in_node)
{
    int linked = 0;
    dlog("link_nodes %u -> %u\n", out_node, in_node);
    dump_node_ports(eq, out_node, "output");
    dump_node_ports(eq, in_node, "input");

    for (int i = 0; i < eq->n_ports; ++i) {
        const AhPort *op = &eq->ports[i];
        if (op->node_id != out_node || strcmp(op->direction, "out") != 0)
            continue;

        for (int j = 0; j < eq->n_ports; ++j) {
            const AhPort *ip = &eq->ports[j];
            if (ip->node_id != in_node || strcmp(ip->direction, "in") != 0)
                continue;
            if (!channels_match(op->channel, ip->channel))
                continue;
            if (link_port_pair(eq, out_node, op->id, in_node, ip->id) == 0)
                linked++;
        }
    }
    dlog("link_nodes result=%d\n", linked);
    return linked;
}

static void destroy_links(AhPipewireEq *eq)
{
    for (int i = 0; i < eq->n_links; ++i) {
        if (eq->links[i])
            pw_proxy_destroy(eq->links[i]);
        eq->links[i] = NULL;
    }
    eq->n_links = 0;
}

static int connect_graph(AhPipewireEq *eq)
{
    destroy_links(eq);

    eq->hardware_sink_id = resolve_hardware_sink(eq);
    if (!eq->hardware_sink_id) {
        set_err(eq, "no hardware output sink found");
        return -1;
    }

    /* Wait for hardware ports. */
    for (int i = 0; i < 50; ++i) {
        if (count_node_ports(eq, eq->hardware_sink_id, "in") >= 2)
            break;
        pw_thread_loop_unlock(eq->loop);
        usleep(20000);
        pw_thread_loop_lock(eq->loop);
        core_sync(eq);
    }

    /* EasyEffects order (reverse connect): speakers ← filter ← virtual sink */
    int n1 = link_nodes(eq, eq->filter_id, eq->hardware_sink_id);
    int n2 = link_nodes(eq, eq->sink_id, eq->filter_id);

    if (n1 < 2 || n2 < 2) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "graph link failed (filter→hw=%d, sink→filter=%d)", n1, n2);
        set_err(eq, msg);
        return -1;
    }

    set_err(eq, "");
    return 0;
}

static void teardown_graph(AhPipewireEq *eq)
{
    const bool alive = eq->core && !atomic_load(&eq->disconnected);

    /* Clear metadata targets so WirePlumber returns streams to the real default. */
    if (alive)
        unroute_streams(eq);

    destroy_links(eq);

    if (eq->filter) {
        if (eq->filter_connected) {
            spa_hook_remove(&eq->filter_listener);
            if (alive)
                pw_filter_disconnect(eq->filter);
            eq->filter_connected = false;
        }
        pw_filter_destroy(eq->filter);
        eq->filter = NULL;
        eq->filter_id = 0;
        eq->port_in_l = eq->port_in_r = eq->port_out_l = eq->port_out_r = NULL;
    }

    if (eq->sink_proxy) {
        if (alive)
            pw_proxy_destroy(eq->sink_proxy);
        eq->sink_proxy = NULL;
        eq->sink_id = 0;
        eq->sink_serial = 0;
    }

    if (alive)
        core_sync(eq);
}

static int arm_graph(AhPipewireEq *eq)
{
    /* Capture the real hardware sink name for filter output linking. */
    if (!eq->hardware_sink[0] && eq->default_sink[0] &&
        strcmp(eq->default_sink, AH_NODE_NAME) != 0) {
        snprintf(eq->hardware_sink, sizeof(eq->hardware_sink), "%s", eq->default_sink);
    }

    rebuild_dsp(eq);
    reset_dsp_state(eq, eq->process_rate ? eq->process_rate : 48000);

    if (create_virtual_sink(eq) != 0)
        return -1;
    if (create_eq_filter(eq) != 0)
        return -1;
    if (connect_graph(eq) != 0)
        return -1;

    /*
     * EasyEffects pattern: never steal default.audio.sink.
     * Steer output streams via target.node / target.object metadata.
     */
    route_streams_to_sink(eq);
    core_sync(eq);
    set_err(eq, "");
    return 0;
}

/* ---------- connection lifecycle ---------- */

static void clear_registry_cache(AhPipewireEq *eq)
{
    eq->n_ports = 0;
    eq->n_nodes = 0;
    eq->n_streams = 0;
    eq->n_links = 0;
    eq->have_metadata = false;
    eq->metadata = NULL;
    eq->default_sink[0] = '\0';
    eq->hardware_sink[0] = '\0';
    eq->hardware_sink_id = 0;
    eq->sink_id = 0;
    eq->sink_serial = 0;
    eq->filter_id = 0;
    eq->filter_connected = false;
    memset(eq->ports, 0, sizeof(eq->ports));
    memset(eq->nodes, 0, sizeof(eq->nodes));
    memset(eq->streams, 0, sizeof(eq->streams));
    memset(eq->links, 0, sizeof(eq->links));
}

static void disconnect_pw(AhPipewireEq *eq)
{
    if (!eq->loop && !eq->core)
        return;

    eq->enabled = false;

    /*
     * Stop the PipeWire thread before tearing proxies down so callbacks
     * cannot race destroy. Safe to call from the recovery thread only
     * (never from a PW callback).
     */
    if (eq->loop)
        pw_thread_loop_stop(eq->loop);

    if (eq->filter) {
        if (eq->filter_connected)
            spa_hook_remove(&eq->filter_listener);
        pw_filter_destroy(eq->filter);
        eq->filter = NULL;
        eq->port_in_l = eq->port_in_r = eq->port_out_l = eq->port_out_r = NULL;
    }

    for (int i = 0; i < eq->n_links; ++i) {
        if (eq->links[i])
            pw_proxy_destroy(eq->links[i]);
        eq->links[i] = NULL;
    }
    eq->n_links = 0;

    if (eq->sink_proxy) {
        pw_proxy_destroy(eq->sink_proxy);
        eq->sink_proxy = NULL;
    }

    if (eq->metadata) {
        spa_hook_remove(&eq->metadata_listener);
        pw_proxy_destroy((struct pw_proxy *)eq->metadata);
        eq->metadata = NULL;
    }
    if (eq->registry) {
        spa_hook_remove(&eq->registry_listener);
        pw_proxy_destroy((struct pw_proxy *)eq->registry);
        eq->registry = NULL;
    }
    if (eq->core) {
        spa_hook_remove(&eq->core_listener);
        pw_core_disconnect(eq->core);
        eq->core = NULL;
    }
    if (eq->context) {
        pw_context_destroy(eq->context);
        eq->context = NULL;
    }
    if (eq->loop) {
        pw_thread_loop_destroy(eq->loop);
        eq->loop = NULL;
    }

    clear_registry_cache(eq);
    atomic_store(&eq->disconnected, false);
}

static int connect_pw(AhPipewireEq *eq)
{
    if (eq->core)
        return 0;

    pw_init(NULL, NULL);
    eq->loop = pw_thread_loop_new("audiohawk", NULL);
    if (!eq->loop) {
        set_err(eq, "pw_thread_loop_new failed");
        return -1;
    }

    pw_thread_loop_lock(eq->loop);
    eq->context = pw_context_new(pw_thread_loop_get_loop(eq->loop), NULL, 0);
    if (!eq->context) {
        pw_thread_loop_unlock(eq->loop);
        set_err(eq, "pw_context_new failed");
        return -1;
    }

    eq->core = pw_context_connect(eq->context, NULL, 0);
    if (!eq->core) {
        pw_thread_loop_unlock(eq->loop);
        set_err(eq, "pw_context_connect failed — is PipeWire running?");
        return -1;
    }

    atomic_store(&eq->disconnected, false);
    pw_core_add_listener(eq->core, &eq->core_listener, &core_events, eq);
    eq->registry = pw_core_get_registry(eq->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(eq->registry, &eq->registry_listener, &registry_events, eq);

    pw_thread_loop_start(eq->loop);
    core_sync(eq);

    /* Give registry/metadata a moment to populate. */
    for (int i = 0; i < 25; ++i) {
        if (eq->have_metadata && (eq->default_sink[0] || eq->n_nodes > 0))
            break;
        pw_thread_loop_unlock(eq->loop);
        usleep(20000);
        pw_thread_loop_lock(eq->loop);
        core_sync(eq);
    }

    if (!eq->hardware_sink[0])
        (void)resolve_hardware_sink(eq);

    pw_thread_loop_unlock(eq->loop);
    return 0;
}

static int try_rearm_locked(AhPipewireEq *eq)
{
    if (!eq->want_enabled)
        return 0;

    teardown_graph(eq);
    eq->hardware_sink_id = 0;
    if (!eq->hardware_sink[0] && eq->default_sink[0] &&
        strcmp(eq->default_sink, AH_NODE_NAME) != 0) {
        snprintf(eq->hardware_sink, sizeof(eq->hardware_sink), "%s", eq->default_sink);
    }

    eq->enabled = true;
    if (arm_graph(eq) != 0) {
        eq->enabled = false;
        teardown_graph(eq);
        return -1;
    }
    return 0;
}

static bool graph_needs_rearm(AhPipewireEq *eq)
{
    if (!eq->want_enabled || !eq->core || atomic_load(&eq->disconnected))
        return false;
    if (!eq->enabled)
        return true;
    if (!eq->sink_proxy || !eq->filter || eq->n_links < 4)
        return true;
    if (eq->hardware_sink_id == 0 && resolve_hardware_sink(eq) != 0)
        return true;
    return false;
}

bool ah_pw_eq_needs_recovery(const AhPipewireEq *eq)
{
    if (!eq)
        return false;
    if (atomic_load(&eq->disconnected))
        return true;
    if (eq->want_enabled && !eq->core)
        return true;
    return false;
}

int ah_pw_eq_recover(AhPipewireEq *eq)
{
    if (!eq || atomic_load(&eq->shutting_down))
        return -1;

    bool expected = false;
    if (!atomic_compare_exchange_strong(&eq->recover_busy, &expected, true))
        return 0;

    int rc = 0;
    const bool lost = atomic_load(&eq->disconnected) || !eq->core;

    if (lost) {
        disconnect_pw(eq);
        if (connect_pw(eq) != 0) {
            atomic_store(&eq->disconnected, true);
            rc = -1;
            goto out;
        }

        if (eq->want_enabled) {
            pw_thread_loop_lock(eq->loop);
            rc = try_rearm_locked(eq);
            pw_thread_loop_unlock(eq->loop);
        }
        goto out;
    }

    /* Connection alive but graph incomplete (device vanished after resume). */
    if (eq->loop) {
        pw_thread_loop_lock(eq->loop);
        if (graph_needs_rearm(eq))
            rc = try_rearm_locked(eq);
        pw_thread_loop_unlock(eq->loop);
    }

out:
    atomic_store(&eq->recover_busy, false);
    return rc;
}

static void *recover_main(void *arg)
{
    AhPipewireEq *eq = arg;

    while (!atomic_load(&eq->shutting_down)) {
        struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
        if (atomic_load(&eq->shutting_down))
            break;

        bool need = atomic_load(&eq->disconnected) ||
                    (eq->want_enabled && !eq->core);
        if (!need && eq->loop && eq->core && eq->want_enabled) {
            pw_thread_loop_lock(eq->loop);
            need = graph_needs_rearm(eq);
            pw_thread_loop_unlock(eq->loop);
        }

        if (need)
            (void)ah_pw_eq_recover(eq);
    }
    return NULL;
}

static void start_recover_thread(AhPipewireEq *eq)
{
    if (eq->recover_started)
        return;
    atomic_store(&eq->shutting_down, false);
    if (pthread_create(&eq->recover_thread, NULL, recover_main, eq) == 0)
        eq->recover_started = true;
}

static void stop_recover_thread(AhPipewireEq *eq)
{
    if (!eq->recover_started)
        return;
    atomic_store(&eq->shutting_down, true);
    pthread_join(eq->recover_thread, NULL);
    eq->recover_started = false;
}

/* ---------- public API ---------- */

AhPipewireEq *ah_pw_eq_create(void)
{
    AhPipewireEq *eq = calloc(1, sizeof(*eq));
    if (!eq)
        return NULL;

    eq->n_bands = 10;
    eq->eq_active = true;
    atomic_init(&eq->dsp_idx, 0);
    atomic_init(&eq->dsp_busy, -1);
    pthread_mutex_init(&eq->dsp_lock, NULL);
    atomic_init(&eq->disconnected, false);
    atomic_init(&eq->shutting_down, false);
    atomic_init(&eq->recover_busy, false);
    ah_effects_defaults(&eq->fx);
    ah_advanced_defaults(&eq->adv_state);
    eq->adv = ah_advanced_create();
    if (eq->adv)
        ah_advanced_apply(eq->adv, &eq->adv_state, 48000);
    AhEqBand tmp[AH_EQ_BANDS];
    ah_compose_eq(AH_PROFILE_MUSIC, AH_INTEL_OFF, tmp);
    memcpy(eq->bands, tmp, sizeof(tmp));
    rebuild_dsp(eq);
    reset_dsp_state(eq, 48000);
    (void)connect_pw(eq);
    start_recover_thread(eq);
    return eq;
}

void ah_pw_eq_destroy(AhPipewireEq *eq)
{
    if (!eq)
        return;

    stop_recover_thread(eq);

    eq->want_enabled = false;
    if (eq->enabled && eq->loop && eq->core && !atomic_load(&eq->disconnected)) {
        pw_thread_loop_lock(eq->loop);
        eq->enabled = false;
        teardown_graph(eq);
        pw_thread_loop_unlock(eq->loop);
    }

    disconnect_pw(eq);
    ah_advanced_destroy(eq->adv);
    pthread_mutex_destroy(&eq->dsp_lock);
    free(eq);
}

bool ah_pw_eq_is_enabled(const AhPipewireEq *eq)
{
    return eq && eq->enabled;
}

void ah_pw_eq_set_eq_active(AhPipewireEq *eq, bool active)
{
    if (!eq)
        return;
    eq->eq_active = active;
    rebuild_dsp(eq);
}

int ah_pw_eq_apply_bands(AhPipewireEq *eq, const AhEqBand *bands, int n_bands)
{
    if (!eq || !bands || n_bands < 1)
        return -1;
    if (n_bands > AH_EQ_BANDS_MAX)
        n_bands = AH_EQ_BANDS_MAX;

    eq->n_bands = n_bands;
    memset(eq->bands, 0, sizeof(eq->bands));
    memcpy(eq->bands, bands, sizeof(AhEqBand) * (size_t)n_bands);

    rebuild_dsp(eq);
    return 0;
}

int ah_pw_eq_apply_effects(AhPipewireEq *eq, const AhEffectsState *fx)
{
    if (!eq || !fx)
        return -1;
    eq->fx = *fx;
    ah_effects_clamp(&eq->fx);
    rebuild_dsp(eq);
    return 0;
}

int ah_pw_eq_apply_advanced(AhPipewireEq *eq, const AhAdvancedState *adv)
{
    if (!eq || !adv)
        return -1;
    eq->adv_state = *adv;
    ah_advanced_clamp(&eq->adv_state);
    pthread_mutex_lock(&eq->dsp_lock);
    if (eq->adv)
        ah_advanced_apply(eq->adv, &eq->adv_state,
                          eq->process_rate ? eq->process_rate : 48000);
    pthread_mutex_unlock(&eq->dsp_lock);
    return 0;
}

void ah_pw_eq_advanced_meters(const AhPipewireEq *eq, AhAdvancedMeters *out)
{
    if (!out)
        return;
    if (!eq || !eq->adv) {
        memset(out, 0, sizeof(*out));
        return;
    }
    ah_advanced_meters(eq->adv, out);
}

int ah_pw_eq_enable(AhPipewireEq *eq, bool enabled)
{
    if (!eq)
        return -1;

    eq->want_enabled = enabled;

    if (atomic_load(&eq->disconnected) || !eq->core) {
        if (!enabled)
            return 0;
        if (ah_pw_eq_recover(eq) != 0)
            return -1;
        return eq->enabled ? 0 : -1;
    }

    if (enabled == eq->enabled) {
        if (!enabled)
            return 0;
        if (eq->sink_proxy && eq->filter && eq->n_links >= 4)
            return 0;
    }

    if (connect_pw(eq) != 0)
        return -1;

    pw_thread_loop_lock(eq->loop);

    if (enabled) {
        eq->enabled = true;
        if (arm_graph(eq) != 0) {
            eq->enabled = false;
            teardown_graph(eq);
            pw_thread_loop_unlock(eq->loop);
            return -1;
        }
        pw_thread_loop_unlock(eq->loop);
        return 0;
    }

    eq->enabled = false;
    rebuild_dsp(eq);
    teardown_graph(eq);
    set_err(eq, "");
    pw_thread_loop_unlock(eq->loop);
    return 0;
}
