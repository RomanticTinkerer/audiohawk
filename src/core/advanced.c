#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "audiohawk/advanced.h"
#include "audiohawk/config.h"
#include "audiohawk/spatial.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AH_ADV_IR 256
#define AH_ADV_COMB 4096
#define AH_ADV_XDEL 96
#define AH_ADV_LA 256
#define AH_ADV_FILT 40

typedef struct {
    float b0, b1, b2, a1, a2;
} AhBq;

typedef struct {
    float z1, z2;
} AhBqZ;

enum {
    F_DC, F_HP, F_KSH, F_KHP,
    F_BASSLP, F_BASSHP, F_PSYCHO,
    F_NR0, F_NR2, F_TAMER, F_DEESS, F_DEESS_HP,
    F_EXCITE, F_TREBHP, F_SPEC_P, F_SPEC_S,
    F_VINYL_R, F_VINYL_M, F_LOFI,
    F_PARA0, F_PARA1, F_PARA2,
    F_MB_LO, F_MB_LO2, F_MB_HI, F_MB_HI2,
    F_DYNEQ, F_AD0, F_AD2, F_FD_LO,
    F_VINT_LO, F_VINT_HI, F_WARM,
    F_COUNT
};

typedef struct {
    bool on[AH_ADV_SLOTS];
    float amt[AH_ADV_SLOTS];
    bool ab;
    bool gain_match;
    int order;
    uint32_t rate;
    AhBq f[F_COUNT];
    AhSpatialParams spatial;
    float ir[AH_ADV_IR];
    int comb_len[4];
    int hall_len[4];
    int xf_delay;
    int speak_delay;
    /* libbs2b HighEC / LowEC coeffs, same family EasyEffects uses. */
    float bs2b_a0lo, bs2b_b1lo, bs2b_a0hi, bs2b_a1hi, bs2b_b1hi;
    int la_len;
    float la_ceil;
    float comp_thr, comp_ratio, comp_knee, comp_atk, comp_rel, comp_makeup;
    float loud_target;
} AhAdvBank;

struct AhAdvancedEngine {
    AhAdvBank banks[2];
    atomic_int idx;
    uint32_t rate;
    AhBqZ mem[2][F_COUNT];
    AhSpatialState spatial;
    float comb[2][4][AH_ADV_COMB];
    float ap[2][2][AH_ADV_COMB];
    int comb_pos[4];
    int ap_pos[2];
    float ir_buf[2][AH_ADV_IR];
    int ir_pos;
    float xdel[2][AH_ADV_XDEL];
    int xpos;
    float env[18];
    float rms[8];
    float noise_floor[3];
    float bs2b_lo[2], bs2b_hi[2];
    float adaa[16];
    float la[2][AH_ADV_LA];
    int la_i;
    float la_g;
    float gm_g;
    float loud_g;
    float agc_g;
    float hold_n;
    float la_prev[2];
    float comb_lp[2][4];
    float lofi_hold[2];
    int lofi_n;
    float wow_phase;
    uint32_t rng;
    float spec_e[AH_ADV_SPEC];
    float peak;
    float clip_hold;
    float dry_ms;
    float wet_ms;
    float corr_n;
    float corr_d;
    float cpu;
    atomic_int pub_clip;
    atomic_int pub_peak;
    atomic_int pub_cpu;
    atomic_int pub_corr;
    atomic_int pub_dry;
    atomic_int pub_wet;
    atomic_int pub_spec[AH_ADV_SPEC];
};

typedef struct {
    const char *key;
    const char *title;
    const char *blurb;
    const char *icon;
    const char *group;
    int tab;
} AhAdvMeta;

static const AhAdvMeta k_meta[AH_ADV_SLOTS] = {
    { "dyn_bass", "Dynamic Bass Enhancement", "Calf-style: harmonics on the low band when bass is playing", "effect-bass", "Tone", 0 },
    { "compressor", "Dynamic Range Compressor", "Soft-knee RMS, 20 ms / 100 ms, linked stereo like EasyEffects", "effect-leveler", "Dynamics", 0 },
    { "stereo_image", "Stereo Image Enhancer", "Mid/side width; 50 stays natural, bass stays closer to mono", "effect-surround", "Stereo", 0 },
    { "exciter", "Harmonic Exciter", "High-passed ADAA tanh harmonics for air without alias fizz", "effect-treble", "Tone", 0 },
    { "crossfeed", "Headphone Crossfeed", "Bauer bs2b HighEC (700 Hz / 4.5 dB), same family as EasyEffects", "effect-surround", "Stereo", 0 },
    { "res_tamer", "Resonance Tamer", "Narrow 3.2 kHz band compressor for harsh peaks", "effect-mid", "Cleanup", 0 },
    { "limiter", "True Peak Limiter", "2 ms lookahead, stereo-linked, about -1 dBTP like LSP", "volume-boost", "Loudness", 0 },
    { "agc", "Automatic Gain Compensation", "Broadcast-style RMS leveler toward -18 dBFS", "effect-leveler", "Loudness", 0 },
    { "dc_remove", "DC Offset Removal", "10 Hz Butterworth high-pass, no tone change", "flat", "Cleanup", 0 },
    { "noise_reduce", "Adaptive Noise Reduction", "Three-band gate against a slow noise-floor estimate", "work", "Cleanup", 0 },
    { "highpass", "High-Pass Filter", "2nd-order rumble cut, 20 to 200 Hz", "bass", "Cleanup", 0 },
    { "headroom", "Output Headroom Control", "Trims 0.3 to 6 dB so the limiter has room", "volume-boost", "Loudness", 0 },
    { "mono_check", "Mono Compatibility Checker", "Shows L/R correlation; high settings fold to mono", "bands", "Check", 0 },
    { "spectral", "Spectral Enhancement", "Presence bell and air shelf, 0 dB at amount 0", "treble", "Tone", 0 },
    { "loudness", "Intelligent Loudness Normalization", "ITU-R BS.1770-5 K-weight ride toward -23 to -14 LUFS", "effect-leveler", "Loudness", 0 },
    { "analog", "Analog Warmth", "Low shelf plus first-order ADAA tanh even harmonics", "intel-warm", "Character", 1 },
    { "vintage", "Vintage Hi-Fi", "Smiley shelves plus gentle ADAA saturation", "mood", "Character", 1 },
    { "room", "Room Simulator", "Short Schroeder combs with HF damping", "movie", "Space", 1 },
    { "hall", "Concert Hall Reverb", "Longer damped combs and allpasses", "movie", "Space", 1 },
    { "widen", "Stereo Widening", "Side gain only; lows stay closer to the center", "effect-surround", "Space", 1 },
    { "speakers", "Virtual Speaker Placement", "ITD-style delay and a little cross-ear bleed", "effect-surround", "Space", 1 },
    { "tape", "Tape Saturation", "ADAA clip, wow, and a touch of hiss", "intel-warm", "Character", 1 },
    { "tube", "Tube Amplifier Emulation", "Asymmetric ADAA tanh, more even than odd", "intel-warm", "Character", 1 },
    { "vinyl", "Vinyl Simulation", "Rumble, light crackle, and a worn mid dip", "mood", "Character", 1 },
    { "spatial3d", "3D Spatial Audio", "Binaural-style width and room", "effect-surround", "Space", 1 },
    { "psycho_bass", "Psychoacoustic Bass Boost", "Missing-fundamental 2nd harmonic on the low band", "effect-bass", "Tone", 1 },
    { "distort", "Harmonic Distortion", "Harder ADAA waveshape, still anti-aliased", "effect-mid", "Character", 1 },
    { "mb_exciter", "Multi-band Exciter", "Separate ADAA exciters on mids and highs", "effect-treble", "Tone", 1 },
    { "dyn_treble", "Dynamic Treble Enhancement", "Opens the air band when the top is already bright", "treble", "Tone", 1 },
    { "lofi", "Lo-Fi Mode", "Hold-and-crush plus a dulling low-pass", "casual", "Character", 1 },
    { "convolve", "Convolution Reverb", "256-tap decaying impulse, energy-normalized", "movie", "Space", 2 },
    { "parametric", "Parametric Equalizer", "Three peaks; amount 50 is flat", "eq", "Equalizer", 2 },
    { "mb_comp", "Multi-band Compressor", "Low / mid / high RMS, EasyEffects-like times", "bands", "Dynamics", 2 },
    { "transients", "Transient Shaper", "Crystalizer-style fast/slow envelope on the highs", "view-curve", "Dynamics", 2 },
    { "deesser", "De-Esser", "6.5 kHz band compressor, 2 ms attack", "effect-dialogue", "Cleanup", 2 },
    { "spec_gate", "Spectral Gate", "Mutes quiet bands", "work", "Cleanup", 2 },
    { "dyn_eq", "Dynamic EQ", "Soft-knee compressor on a 3 kHz presence bell", "eq", "Equalizer", 2 },
    { "adapt_eq", "Adaptive Equalizer", "Slow tilt toward even low / mid / high energy", "intel-balanced", "Equalizer", 2 },
    { "fd_width", "Frequency-Dependent Stereo Width", "Keeps bass centered, widens the air", "effect-surround", "Space", 2 },
};

static const char *k_presets[AH_ADV_PRESETS] = {
    "Custom", "Off", "Music Glue", "Headphones", "Movie Night", "Lo-Fi"
};

static const char *k_orders[AH_ADV_ORDERS] = {
    "Default", "Space last", "Color first", "Fun first"
};

/* Default processing groups. Output slots always run last. */
static const int k_cleanup[] = {
    AH_ADV_DC, AH_ADV_HIGHPASS, AH_ADV_NOISE, AH_ADV_SPEC_GATE,
    AH_ADV_RES_TAMER, AH_ADV_DEESSER
};
static const int k_tone[] = {
    AH_ADV_DYN_BASS, AH_ADV_PSYCHO_BASS, AH_ADV_COMPRESSOR, AH_ADV_MB_COMP,
    AH_ADV_TRANSIENT, AH_ADV_DYN_EQ, AH_ADV_ADAPT_EQ, AH_ADV_PARAMETRIC,
    AH_ADV_SPECTRAL, AH_ADV_DYN_TREBLE, AH_ADV_EXCITER, AH_ADV_MB_EXCITER
};
static const int k_color[] = {
    AH_ADV_ANALOG, AH_ADV_TUBE, AH_ADV_TAPE, AH_ADV_DISTORT, AH_ADV_VINYL,
    AH_ADV_VINTAGE, AH_ADV_LOFI
};
static const int k_space[] = {
    AH_ADV_STEREO_IMAGE, AH_ADV_WIDEN, AH_ADV_FD_WIDTH, AH_ADV_CROSSFEED,
    AH_ADV_SPEAKERS, AH_ADV_SPATIAL3D, AH_ADV_ROOM, AH_ADV_HALL, AH_ADV_CONVOLVE
};
static const int k_output[] = {
    AH_ADV_AGC, AH_ADV_LOUDNESS, AH_ADV_HEADROOM,
    AH_ADV_MONO, AH_ADV_LIMITER
};

const char *ah_advanced_slot_key(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].key;
}

const char *ah_advanced_slot_title(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].title;
}

const char *ah_advanced_slot_blurb(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].blurb;
}

const char *ah_advanced_slot_icon(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "eq";
    return k_meta[slot].icon;
}

const char *ah_advanced_slot_group(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].group;
}

int ah_advanced_slot_tab(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return 0;
    return k_meta[slot].tab;
}

const char *ah_advanced_preset_name(int id)
{
    if (id < 0 || id >= AH_ADV_PRESETS)
        return k_presets[0];
    return k_presets[id];
}

const char *ah_advanced_order_name(int id)
{
    if (id < 0 || id >= AH_ADV_ORDERS)
        return k_orders[0];
    return k_orders[id];
}

static int snap5(int v)
{
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    return ((v + 2) / 5) * 5;
}

static int find_app(AhAdvancedState *s, const char *name)
{
    if (!s || !name || !name[0])
        return -1;
    for (int i = 0; i < s->n_apps; ++i) {
        if (strcmp(s->apps[i].name, name) == 0)
            return i;
    }
    return -1;
}

static void seed_apps(AhAdvancedState *s)
{
    static const char *names[] = { "firefox", "spotify", "steam", NULL };
    s->n_apps = 0;
    for (int i = 0; names[i] && s->n_apps < AH_ADV_APPS; ++i) {
        snprintf(s->apps[s->n_apps].name, AH_ADV_APP_NAME, "%s", names[i]);
        memcpy(s->apps[s->n_apps].amount, s->amount, sizeof(s->amount));
        s->n_apps++;
    }
    snprintf(s->app_name, AH_ADV_APP_NAME, "%s", s->apps[0].name);
}

void ah_advanced_defaults(AhAdvancedState *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < AH_ADV_SLOTS; ++i)
        s->amount[i] = 50;
    s->amount[AH_ADV_DC] = 100;
    s->amount[AH_ADV_LIMITER] = 70;
    s->amount[AH_ADV_HIGHPASS] = 20;
    s->amount[AH_ADV_HEADROOM] = 10;
    seed_apps(s);
}

void ah_advanced_clamp(AhAdvancedState *s)
{
    if (!s)
        return;
    for (int i = 0; i < AH_ADV_SLOTS; ++i)
        s->amount[i] = snap5(s->amount[i]);
    if (s->order < 0 || s->order >= AH_ADV_ORDERS)
        s->order = 0;
    if (s->preset < 0 || s->preset >= AH_ADV_PRESETS)
        s->preset = 0;
    s->app_name[AH_ADV_APP_NAME - 1] = '\0';
    if (s->n_apps < 0)
        s->n_apps = 0;
    if (s->n_apps > AH_ADV_APPS)
        s->n_apps = AH_ADV_APPS;
    for (int a = 0; a < s->n_apps; ++a) {
        s->apps[a].name[AH_ADV_APP_NAME - 1] = '\0';
        for (int i = 0; i < AH_ADV_SLOTS; ++i)
            s->apps[a].amount[i] = snap5(s->apps[a].amount[i]);
    }
    if (s->n_apps == 0)
        seed_apps(s);
}

bool ah_advanced_any_active(const AhAdvancedState *s)
{
    if (!s || s->ab_bypass)
        return false;
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        if (s->on[i])
            return true;
    }
    return false;
}

void ah_advanced_store_app(AhAdvancedState *s)
{
    if (!s || !s->per_app)
        return;
    int i = find_app(s, s->app_name);
    if (i < 0 && s->n_apps < AH_ADV_APPS && s->app_name[0]) {
        i = s->n_apps++;
        snprintf(s->apps[i].name, AH_ADV_APP_NAME, "%s", s->app_name);
    }
    if (i < 0)
        return;
    memcpy(s->apps[i].on, s->on, sizeof(s->on));
    memcpy(s->apps[i].amount, s->amount, sizeof(s->amount));
}

void ah_advanced_select_app(AhAdvancedState *s, const char *name)
{
    if (!s || !name)
        return;
    ah_advanced_store_app(s);
    snprintf(s->app_name, AH_ADV_APP_NAME, "%s", name);
    int i = find_app(s, name);
    if (i < 0 && s->n_apps < AH_ADV_APPS) {
        i = s->n_apps++;
        snprintf(s->apps[i].name, AH_ADV_APP_NAME, "%s", name);
    }
    if (i >= 0) {
        memcpy(s->on, s->apps[i].on, sizeof(s->on));
        memcpy(s->amount, s->apps[i].amount, sizeof(s->amount));
    }
}

void ah_advanced_apply_preset(AhAdvancedState *s, int preset)
{
    if (!s)
        return;
    if (preset <= 0 || preset >= AH_ADV_PRESETS) {
        s->preset = 0;
        return;
    }
    bool keep_app = s->per_app;
    char app[AH_ADV_APP_NAME];
    memcpy(app, s->app_name, sizeof(app));
    int n_apps = s->n_apps;
    struct {
        char name[AH_ADV_APP_NAME];
        bool on[AH_ADV_SLOTS];
        int amount[AH_ADV_SLOTS];
    } apps[AH_ADV_APPS];
    memcpy(apps, s->apps, sizeof(apps));

    ah_advanced_defaults(s);
    memset(s->on, 0, sizeof(s->on));
    s->preset = preset;
    s->per_app = keep_app;
    memcpy(s->app_name, app, sizeof(app));
    s->n_apps = n_apps;
    memcpy(s->apps, apps, sizeof(apps));

    switch (preset) {
    case 1: /* Off */
        break;
    case 2: /* Music */
        s->on[AH_ADV_DYN_BASS] = true;
        s->amount[AH_ADV_DYN_BASS] = 40;
        s->on[AH_ADV_COMPRESSOR] = true;
        s->amount[AH_ADV_COMPRESSOR] = 25;
        s->on[AH_ADV_STEREO_IMAGE] = true;
        s->amount[AH_ADV_STEREO_IMAGE] = 30;
        s->on[AH_ADV_EXCITER] = true;
        s->amount[AH_ADV_EXCITER] = 20;
        s->on[AH_ADV_LIMITER] = true;
        s->on[AH_ADV_DC] = true;
        s->on[AH_ADV_SPECTRAL] = true;
        s->amount[AH_ADV_SPECTRAL] = 25;
        break;
    case 3: /* Headphones */
        s->on[AH_ADV_CROSSFEED] = true;
        s->amount[AH_ADV_CROSSFEED] = 55;
        s->on[AH_ADV_ANALOG] = true;
        s->amount[AH_ADV_ANALOG] = 30;
        s->on[AH_ADV_SPATIAL3D] = true;
        s->amount[AH_ADV_SPATIAL3D] = 35;
        s->on[AH_ADV_LIMITER] = true;
        s->on[AH_ADV_DC] = true;
        s->on[AH_ADV_HIGHPASS] = true;
        break;
    case 4: /* Movie */
        s->on[AH_ADV_COMPRESSOR] = true;
        s->amount[AH_ADV_COMPRESSOR] = 35;
        s->on[AH_ADV_LOUDNESS] = true;
        s->amount[AH_ADV_LOUDNESS] = 45;
        s->on[AH_ADV_SPECTRAL] = true;
        s->amount[AH_ADV_SPECTRAL] = 35;
        s->on[AH_ADV_HALL] = true;
        s->amount[AH_ADV_HALL] = 15;
        s->on[AH_ADV_LIMITER] = true;
        s->on[AH_ADV_DC] = true;
        break;
    case 5: /* Lo-Fi */
        s->on[AH_ADV_LOFI] = true;
        s->amount[AH_ADV_LOFI] = 70;
        s->on[AH_ADV_VINYL] = true;
        s->amount[AH_ADV_VINYL] = 30;
        s->on[AH_ADV_TAPE] = true;
        s->amount[AH_ADV_TAPE] = 40;
        s->on[AH_ADV_LIMITER] = true;
        break;
    default:
        break;
    }
}

static int parse_bool(const char *v, bool *out)
{
    if (!v)
        return -1;
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0) {
        *out = true;
        return 0;
    }
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

int ah_advanced_load(AhAdvancedState *s)
{
    ah_advanced_defaults(s);
    char path[640];
    snprintf(path, sizeof(path), "%s/advanced.conf", ah_config_dir());
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (strcmp(key, "ab_bypass") == 0)
            parse_bool(val, &s->ab_bypass);
        else if (strcmp(key, "gain_match") == 0)
            parse_bool(val, &s->gain_match);
        else if (strcmp(key, "per_app") == 0)
            parse_bool(val, &s->per_app);
        else if (strcmp(key, "order") == 0)
            s->order = atoi(val);
        else if (strcmp(key, "preset") == 0)
            s->preset = atoi(val);
        else if (strcmp(key, "app_name") == 0)
            snprintf(s->app_name, sizeof(s->app_name), "%s", val);
        else if (strncmp(key, "on.", 3) == 0) {
            for (int i = 0; i < AH_ADV_SLOTS; ++i) {
                if (strcmp(key + 3, k_meta[i].key) == 0)
                    parse_bool(val, &s->on[i]);
            }
        } else if (strncmp(key, "amt.", 4) == 0) {
            for (int i = 0; i < AH_ADV_SLOTS; ++i) {
                if (strcmp(key + 4, k_meta[i].key) == 0)
                    s->amount[i] = atoi(val);
            }
        } else if (strncmp(key, "app.", 4) == 0) {
            /* app.<idx>.name / app.<idx>.on.<key> / app.<idx>.amt.<key> */
            int idx = atoi(key + 4);
            const char *dot = strchr(key + 4, '.');
            if (!dot || idx < 0 || idx >= AH_ADV_APPS)
                continue;
            if (idx + 1 > s->n_apps)
                s->n_apps = idx + 1;
            if (strcmp(dot + 1, "name") == 0)
                snprintf(s->apps[idx].name, AH_ADV_APP_NAME, "%s", val);
            else if (strncmp(dot + 1, "on.", 3) == 0) {
                for (int i = 0; i < AH_ADV_SLOTS; ++i) {
                    if (strcmp(dot + 4, k_meta[i].key) == 0)
                        parse_bool(val, &s->apps[idx].on[i]);
                }
            } else if (strncmp(dot + 1, "amt.", 4) == 0) {
                for (int i = 0; i < AH_ADV_SLOTS; ++i) {
                    if (strcmp(dot + 5, k_meta[i].key) == 0)
                        s->apps[idx].amount[i] = atoi(val);
                }
            }
        }
    }
    fclose(f);
    ah_advanced_clamp(s);
    if (s->per_app && s->app_name[0]) {
        int i = find_app(s, s->app_name);
        if (i >= 0) {
            memcpy(s->on, s->apps[i].on, sizeof(s->on));
            memcpy(s->amount, s->apps[i].amount, sizeof(s->amount));
        }
    }
    return 0;
}

int ah_advanced_save(const AhAdvancedState *s)
{
    if (!s)
        return -1;
    AhAdvancedState tmp = *s;
    ah_advanced_clamp(&tmp);
    if (tmp.per_app)
        ah_advanced_store_app(&tmp);

    char path[640];
    snprintf(path, sizeof(path), "%s/advanced.conf", ah_config_dir());
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# AudioHawk advanced effects\n");
    fprintf(f, "ab_bypass=%s\n", tmp.ab_bypass ? "true" : "false");
    fprintf(f, "gain_match=%s\n", tmp.gain_match ? "true" : "false");
    fprintf(f, "per_app=%s\n", tmp.per_app ? "true" : "false");
    fprintf(f, "order=%d\n", tmp.order);
    fprintf(f, "preset=%d\n", tmp.preset);
    fprintf(f, "app_name=%s\n", tmp.app_name);
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        fprintf(f, "on.%s=%s\n", k_meta[i].key, tmp.on[i] ? "true" : "false");
        fprintf(f, "amt.%s=%d\n", k_meta[i].key, tmp.amount[i]);
    }
    for (int a = 0; a < tmp.n_apps; ++a) {
        fprintf(f, "app.%d.name=%s\n", a, tmp.apps[a].name);
        for (int i = 0; i < AH_ADV_SLOTS; ++i) {
            fprintf(f, "app.%d.on.%s=%s\n", a, k_meta[i].key,
                    tmp.apps[a].on[i] ? "true" : "false");
            fprintf(f, "app.%d.amt.%s=%d\n", a, k_meta[i].key, tmp.apps[a].amount[i]);
        }
    }
    fclose(f);
    return 0;
}

/* ---------- DSP ---------- */

static void design_unity(AhBq *bq)
{
    bq->b0 = 1.f;
    bq->b1 = 0.f;
    bq->b2 = 0.f;
    bq->a1 = 0.f;
    bq->a2 = 0.f;
}

static void clamp_freq_lo(float *freq, uint32_t rate, float lo)
{
    if (*freq < lo)
        *freq = lo;
    if (*freq > (float)rate * 0.45f)
        *freq = (float)rate * 0.45f;
}

static void clamp_freq(float *freq, uint32_t rate)
{
    clamp_freq_lo(freq, rate, 20.f);
}

static void design_peaking(AhBq *bq, float freq, float q, float gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabsf(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }
    const float A = powf(10.f, gain_db / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float alpha = sinf(w0) / (2.f * q);
    const float cosw = cosf(w0);
    const float b0 = 1.f + alpha * A;
    const float b1 = -2.f * cosw;
    const float b2 = 1.f - alpha * A;
    const float a0 = 1.f + alpha / A;
    const float a1 = -2.f * cosw;
    const float a2 = 1.f - alpha / A;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_lowshelf(AhBq *bq, float freq, float q, float gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabsf(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }
    const float A = powf(10.f, gain_db / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float sinw = sinf(w0);
    const float alpha = sinw / (2.f * q);
    const float two = 2.f * sqrtf(A) * alpha;
    const float b0 = A * ((A + 1.f) - (A - 1.f) * cosw + two);
    const float b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw);
    const float b2 = A * ((A + 1.f) - (A - 1.f) * cosw - two);
    const float a0 = (A + 1.f) + (A - 1.f) * cosw + two;
    const float a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw);
    const float a2 = (A + 1.f) + (A - 1.f) * cosw - two;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_highshelf(AhBq *bq, float freq, float q, float gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabsf(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }
    const float A = powf(10.f, gain_db / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float sinw = sinf(w0);
    const float alpha = sinw / (2.f * q);
    const float two = 2.f * sqrtf(A) * alpha;
    const float b0 = A * ((A + 1.f) + (A - 1.f) * cosw + two);
    const float b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw);
    const float b2 = A * ((A + 1.f) + (A - 1.f) * cosw - two);
    const float a0 = (A + 1.f) - (A - 1.f) * cosw + two;
    const float a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw);
    const float a2 = (A + 1.f) - (A - 1.f) * cosw - two;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_lowpass(AhBq *bq, float freq, float q, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float b0 = (1.f - cosw) * 0.5f;
    const float a0 = 1.f + alpha;
    bq->b0 = b0 / a0;
    bq->b1 = (1.f - cosw) / a0;
    bq->b2 = b0 / a0;
    bq->a1 = (-2.f * cosw) / a0;
    bq->a2 = (1.f - alpha) / a0;
}

static void design_highpass(AhBq *bq, float freq, float q, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float b0 = (1.f + cosw) * 0.5f;
    const float a0 = 1.f + alpha;
    bq->b0 = b0 / a0;
    bq->b1 = -(1.f + cosw) / a0;
    bq->b2 = b0 / a0;
    bq->a1 = (-2.f * cosw) / a0;
    bq->a2 = (1.f - alpha) / a0;
}

static float bq_run(const AhBq *bq, AhBqZ *m, float x)
{
    const float y = bq->b0 * x + m->z1;
    m->z1 = bq->b1 * x - bq->a1 * y + m->z2;
    m->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

static float clampf(float x, float lo, float hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static float db2lin(float db)
{
    return powf(10.f, db * 0.05f);
}

static float lin2db(float x)
{
    return 20.f * log10f(x + 1e-12f);
}

static float tau_coeff(float ms, uint32_t rate)
{
    float t = (ms * 0.001f) * (float)(rate > 0 ? rate : 48000);
    if (t < 1.f)
        t = 1.f;
    return 1.f - expf(-1.f / t);
}

/* First-order ADAA tanh. Antiderivative is log(cosh). Cuts the alias fizz of raw tanh. */
static float tanh_adaa(float *z, float x)
{
    float x1 = *z;
    *z = x;
    if (x > 20.f)
        x = 20.f;
    if (x < -20.f)
        x = -20.f;
    if (x1 > 20.f)
        x1 = 20.f;
    if (x1 < -20.f)
        x1 = -20.f;
    float d = x - x1;
    if (fabsf(d) < 1e-5f)
        return tanhf(0.5f * (x + x1));
    float Fx = fabsf(x) + logf((1.f + expf(-2.f * fabsf(x))) * 0.5f);
    float Fx1 = fabsf(x1) + logf((1.f + expf(-2.f * fabsf(x1))) * 0.5f);
    if (x < 0.f)
        Fx = -Fx;
    if (x1 < 0.f)
        Fx1 = -Fx1;
    return (Fx - Fx1) / d;
}

static float follow(float *env, float x, float atk, float rel)
{
    float a = fabsf(x);
    if (a > *env)
        *env += (a - *env) * atk;
    else
        *env += (a - *env) * rel;
    return *env;
}

static float rms_follow(float *s, float x, float atk, float rel)
{
    float p = x * x;
    if (p > *s)
        *s += (p - *s) * atk;
    else
        *s += (p - *s) * rel;
    return sqrtf(*s + 1e-16f);
}

/* Soft-knee GR in dB. Knee sits on the threshold, same idea as LSP / EasyEffects. */
static float softknee_gr(float level_db, float thr, float ratio, float knee)
{
    if (ratio < 1.001f)
        return 0.f;
    float over = level_db - thr;
    float half = knee * 0.5f;
    float slope = 1.f - 1.f / ratio;
    if (over > half)
        return over * slope;
    if (over > -half) {
        float x = over + half;
        return (x * x) / (2.f * knee + 1e-8f) * slope;
    }
    return 0.f;
}

static float comp_gain(float *rms, float det, float atk, float rel,
                       float thr_db, float ratio, float knee_db, float makeup_db)
{
    float level = rms_follow(rms, det, atk, rel);
    float gr = softknee_gr(lin2db(level), thr_db, ratio, knee_db);
    return db2lin(-gr + makeup_db);
}

/* 4x linear true-peak estimate (ITU-R BS.1770 oversample idea, cheap). */
static float truepeak4(float a, float b)
{
    float p = fmaxf(fabsf(a), fabsf(b));
    p = fmaxf(p, fabsf(0.75f * a + 0.25f * b));
    p = fmaxf(p, fabsf(0.5f * a + 0.5f * b));
    p = fmaxf(p, fabsf(0.25f * a + 0.75f * b));
    return p;
}

/* ITU-R BS.1770-5 / libebur128 analog prototypes, bilinear + pre-warp. */
static void design_k_shelf(AhBq *bq, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    const float f0 = 1681.974450955533f;
    const float G = 3.999843853973347f;
    const float Q = 0.7071752369554196f;
    const float K = tanf((float)M_PI * f0 / (float)rate);
    const float Vh = powf(10.f, G / 20.f);
    const float Vb = powf(Vh, 0.4996667741545416f);
    const float a0 = 1.f + K / Q + K * K;
    bq->b0 = (Vh + Vb * K / Q + K * K) / a0;
    bq->b1 = 2.f * (K * K - Vh) / a0;
    bq->b2 = (Vh - Vb * K / Q + K * K) / a0;
    bq->a1 = 2.f * (K * K - 1.f) / a0;
    bq->a2 = (1.f - K / Q + K * K) / a0;
}

static void design_k_hp(AhBq *bq, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    const float f0 = 38.13547087602444f;
    const float Q = 0.5003270373238773f;
    const float K = tanf((float)M_PI * f0 / (float)rate);
    const float a0 = 1.f + K / Q + K * K;
    bq->b0 = 1.f / a0;
    bq->b1 = -2.f / a0;
    bq->b2 = 1.f / a0;
    bq->a1 = 2.f * (K * K - 1.f) / a0;
    bq->a2 = (1.f - K / Q + K * K) / a0;
}

/*
 * Bauer stereophonic-to-binaural, OpenAL-soft / libbs2b Easy Crossfeed.
 * Amount 0 is LowEC (360 Hz), 1 is HighEC (700 Hz / 4.5 dB), EasyEffects default.
 */
static void design_bs2b(AhAdvBank *b, float amt, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    amt = clampf(amt, 0.f, 1.f);
    const float fc_lo = 360.f + amt * 340.f;
    const float fc_hi = 494.f + amt * 481.f;
    const float G_lo = 0.316227766016838f + amt * (0.398107170553497f - 0.316227766016838f);
    const float G_hi = 0.168236228897329f + amt * (0.205671765275719f - 0.168236228897329f);
    const float g = 1.f / (1.f - G_hi + G_lo);
    const float xlo = expf(-2.f * (float)M_PI * fc_lo / (float)rate);
    const float xhi = expf(-2.f * (float)M_PI * fc_hi / (float)rate);
    b->bs2b_b1lo = xlo;
    b->bs2b_a0lo = G_lo * (1.f - xlo) * g;
    b->bs2b_b1hi = xhi;
    b->bs2b_a0hi = (1.f - G_hi * (1.f - xhi)) * g;
    b->bs2b_a1hi = -xhi * g;
}

static void bs2b_tick(const AhAdvBank *b, AhAdvancedEngine *e, float *l, float *r)
{
    const float xL = *l, xR = *r;
    float yLhi = b->bs2b_a0hi * xL + e->bs2b_hi[0];
    e->bs2b_hi[0] = b->bs2b_a1hi * xL + b->bs2b_b1hi * yLhi;
    float yLlo = b->bs2b_a0lo * xL + e->bs2b_lo[0];
    e->bs2b_lo[0] = b->bs2b_b1lo * yLlo;

    float yRlo = b->bs2b_a0lo * xR + e->bs2b_lo[1];
    e->bs2b_lo[1] = b->bs2b_b1lo * yRlo;
    float yRhi = b->bs2b_a0hi * xR + e->bs2b_hi[1];
    e->bs2b_hi[1] = b->bs2b_a1hi * xR + b->bs2b_b1hi * yRhi;

    *l = yLhi + yRlo;
    *r = yLlo + yRhi;
}

static void design_highpass_min(AhBq *bq, float freq, float q, uint32_t rate, float lo)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq_lo(&freq, rate, lo);
    if (q < 0.1f)
        q = 0.1f;
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float b0 = (1.f + cosw) * 0.5f;
    const float a0 = 1.f + alpha;
    bq->b0 = b0 / a0;
    bq->b1 = -(1.f + cosw) / a0;
    bq->b2 = b0 / a0;
    bq->a1 = (-2.f * cosw) / a0;
    bq->a2 = (1.f - alpha) / a0;
}

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s ? *s : 0xA341316Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static float rng_f(uint32_t *s)
{
    return (rng_next(s) / (float)UINT32_MAX) * 2.f - 1.f;
}

static void design_bank(AhAdvBank *b, const AhAdvancedState *s, uint32_t rate)
{
    memset(b, 0, sizeof(*b));
    if (rate < 1)
        rate = 48000;
    b->rate = rate;
    b->ab = s->ab_bypass;
    b->gain_match = s->gain_match;
    b->order = s->order;
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        b->on[i] = s->on[i];
        b->amt[i] = s->amount[i] / 100.f;
    }

    for (int i = 0; i < F_COUNT; ++i)
        design_unity(&b->f[i]);

    design_highpass_min(&b->f[F_DC], 10.f, 0.707f, rate, 2.f);
    float hp = 20.f + b->amt[AH_ADV_HIGHPASS] * 180.f;
    design_highpass(&b->f[F_HP], hp, 0.707f, rate);
    design_k_shelf(&b->f[F_KSH], rate);
    design_k_hp(&b->f[F_KHP], rate);
    design_lowpass(&b->f[F_NR0], 250.f, 0.707f, rate);
    design_highpass(&b->f[F_NR2], 4000.f, 0.707f, rate);
    design_peaking(&b->f[F_TAMER], 3200.f, 2.4f, 0.f, rate);
    design_peaking(&b->f[F_DEESS], 6500.f, 2.2f, 0.f, rate);
    design_highpass(&b->f[F_DEESS_HP], 6500.f, 0.707f, rate);
    design_lowpass(&b->f[F_BASSLP], 90.f, 0.707f, rate);
    design_highpass(&b->f[F_BASSHP], 180.f, 0.707f, rate);
    design_lowpass(&b->f[F_PSYCHO], 90.f, 0.707f, rate);
    design_highpass(&b->f[F_EXCITE], 3500.f, 0.707f, rate);
    design_highpass(&b->f[F_TREBHP], 6000.f, 0.707f, rate);
    design_peaking(&b->f[F_SPEC_P], 2800.f, 1.0f, 4.5f * b->amt[AH_ADV_SPECTRAL], rate);
    design_highshelf(&b->f[F_SPEC_S], 9000.f, 0.707f, 3.5f * b->amt[AH_ADV_SPECTRAL], rate);
    design_lowpass(&b->f[F_VINYL_R], 55.f, 0.707f, rate);
    design_peaking(&b->f[F_VINYL_M], 400.f, 0.8f, -3.f * b->amt[AH_ADV_VINYL], rate);
    design_lowpass(&b->f[F_LOFI], 2800.f + (1.f - b->amt[AH_ADV_LOFI]) * 7000.f, 0.707f, rate);
    design_peaking(&b->f[F_PARA0], 80.f, 0.9f, 6.f * (b->amt[AH_ADV_PARAMETRIC] - 0.5f), rate);
    design_peaking(&b->f[F_PARA1], 1000.f, 0.9f, 4.f * (b->amt[AH_ADV_PARAMETRIC] - 0.5f), rate);
    design_peaking(&b->f[F_PARA2], 8000.f, 0.8f, 5.f * (b->amt[AH_ADV_PARAMETRIC] - 0.5f), rate);
    /* Cascaded Butterworth ~ LR4 splits at 200 Hz and 2.5 kHz. */
    design_lowpass(&b->f[F_MB_LO], 200.f, 0.707f, rate);
    design_lowpass(&b->f[F_MB_LO2], 200.f, 0.707f, rate);
    design_highpass(&b->f[F_MB_HI], 2500.f, 0.707f, rate);
    design_highpass(&b->f[F_MB_HI2], 2500.f, 0.707f, rate);
    design_peaking(&b->f[F_DYNEQ], 3000.f, 1.4f, 0.f, rate);
    design_lowpass(&b->f[F_AD0], 250.f, 0.707f, rate);
    design_highpass(&b->f[F_AD2], 5000.f, 0.707f, rate);
    design_lowpass(&b->f[F_FD_LO], 120.f, 0.707f, rate);
    design_lowshelf(&b->f[F_VINT_LO], 90.f, 0.707f, 3.5f * b->amt[AH_ADV_VINTAGE], rate);
    design_highshelf(&b->f[F_VINT_HI], 7000.f, 0.707f, 3.0f * b->amt[AH_ADV_VINTAGE], rate);
    design_lowshelf(&b->f[F_WARM], 220.f, 0.707f, 2.0f * b->amt[AH_ADV_ANALOG], rate);

    design_bs2b(b, b->amt[AH_ADV_CROSSFEED], rate);

    /* EasyEffects compressor defaults: -12 dB, 4:1, 6 dB knee, 20/100 ms. */
    b->comp_thr = -6.f - b->amt[AH_ADV_COMPRESSOR] * 18.f;
    b->comp_ratio = 1.5f + b->amt[AH_ADV_COMPRESSOR] * 6.5f;
    b->comp_knee = 6.f;
    b->comp_atk = tau_coeff(20.f, rate);
    b->comp_rel = tau_coeff(100.f, rate);
    b->comp_makeup = b->amt[AH_ADV_COMPRESSOR] * 3.5f;

    /* LSP-style ceiling around -0.3 to -1.8 dBTP, 1.5 to 2.5 ms lookahead. */
    b->la_ceil = db2lin(-0.3f - b->amt[AH_ADV_LIMITER] * 1.5f);
    b->la_len = (int)((1.5f + b->amt[AH_ADV_LIMITER]) * 0.001f * (float)rate);
    if (b->la_len < 8)
        b->la_len = 8;
    if (b->la_len >= AH_ADV_LA)
        b->la_len = AH_ADV_LA - 1;

    /* Amount 0 -> EBU -23 LUFS, 1 -> streaming -14 LUFS. */
    b->loud_target = -23.f + b->amt[AH_ADV_LOUDNESS] * 9.f;

    ah_spatial_configure(&b->spatial, rate, true,
                         (int)(b->amt[AH_ADV_SPATIAL3D] * 80.f), 20, 20);

    float decay = 0.92f + 0.055f * b->amt[AH_ADV_CONVOLVE];
    float v = 1.f;
    float e2 = 0.f;
    uint32_t irng = 0x51A7EEDEu;
    for (int i = 0; i < AH_ADV_IR; ++i) {
        float n = (i == 0) ? 1.f : (0.22f + 0.18f * rng_f(&irng));
        b->ir[i] = v * n;
        e2 += b->ir[i] * b->ir[i];
        v *= decay;
    }
    float inorm = (e2 > 1e-12f) ? (1.f / sqrtf(e2)) : 1.f;
    for (int i = 0; i < AH_ADV_IR; ++i)
        b->ir[i] *= inorm;

    int ms = (int)rate;
    b->comb_len[0] = (29 * ms) / 1000;
    b->comb_len[1] = (37 * ms) / 1000;
    b->comb_len[2] = (41 * ms) / 1000;
    b->comb_len[3] = (53 * ms) / 1000;
    b->hall_len[0] = (43 * ms) / 1000;
    b->hall_len[1] = (67 * ms) / 1000;
    b->hall_len[2] = (89 * ms) / 1000;
    b->hall_len[3] = (97 * ms) / 1000;
    for (int i = 0; i < 4; ++i) {
        if (b->comb_len[i] < 8)
            b->comb_len[i] = 8;
        if (b->hall_len[i] < 16)
            b->hall_len[i] = 16;
        if (b->comb_len[i] >= AH_ADV_COMB)
            b->comb_len[i] = AH_ADV_COMB - 1;
        if (b->hall_len[i] >= AH_ADV_COMB)
            b->hall_len[i] = AH_ADV_COMB - 1;
    }
    b->xf_delay = (int)(rate * 0.00032f);
    if (b->xf_delay < 1)
        b->xf_delay = 1;
    if (b->xf_delay >= AH_ADV_XDEL)
        b->xf_delay = AH_ADV_XDEL - 1;
    b->speak_delay = (int)(rate * 0.00045f);
    if (b->speak_delay < 1)
        b->speak_delay = 1;
    if (b->speak_delay >= AH_ADV_XDEL)
        b->speak_delay = AH_ADV_XDEL - 1;
}

static void ms_width(float *l, float *r, float width)
{
    float mid = 0.5f * (*l + *r);
    float side = 0.5f * (*l - *r) * width;
    *l = mid + side;
    *r = mid - side;
}

static float comb_tick(float *buf, int len, int *pos, float x, float fb,
                       float *damp_z, float damp)
{
    if (len < 2)
        len = 2;
    if (len >= AH_ADV_COMB)
        len = AH_ADV_COMB - 1;
    int i = *pos;
    if (i < 0 || i >= len)
        i = 0;
    float y = buf[i];
    *damp_z += damp * (y - *damp_z);
    buf[i] = x + *damp_z * fb;
    *pos = (i + 1) % len;
    return y;
}

static float ap_tick(float *buf, int len, int *pos, float x, float g)
{
    if (len < 2)
        len = 2;
    if (len >= AH_ADV_COMB)
        len = AH_ADV_COMB - 1;
    int i = *pos;
    if (i < 0 || i >= len)
        i = 0;
    float d = buf[i];
    float y = -g * x + d;
    buf[i] = x + g * y;
    *pos = (i + 1) % len;
    return y;
}

static void run_reverb(AhAdvancedEngine *e, const AhAdvBank *b, float *l, float *r,
                       const int *lens, float fb, float wet, float damp)
{
    float inl = *l, inr = *r;
    float acc_l = 0.f, acc_r = 0.f;
    for (int c = 0; c < 4; ++c) {
        acc_l += comb_tick(e->comb[0][c], lens[c], &e->comb_pos[c], inl, fb,
                           &e->comb_lp[0][c], damp);
        acc_r += comb_tick(e->comb[1][c], lens[c], &e->comb_pos[c], inr, fb,
                           &e->comb_lp[1][c], damp);
    }
    acc_l *= 0.25f;
    acc_r *= 0.25f;
    int ap_len0 = lens[0] / 3;
    int ap_len1 = lens[1] / 4;
    acc_l = ap_tick(e->ap[0][0], ap_len0, &e->ap_pos[0], acc_l, 0.5f);
    acc_r = ap_tick(e->ap[1][0], ap_len0, &e->ap_pos[0], acc_r, 0.5f);
    acc_l = ap_tick(e->ap[0][1], ap_len1, &e->ap_pos[1], acc_l, 0.5f);
    acc_r = ap_tick(e->ap[1][1], ap_len1, &e->ap_pos[1], acc_r, 0.5f);
    (void)b;
    *l = inl + acc_l * wet;
    *r = inr + acc_r * wet;
}

static void run_slot(AhAdvancedEngine *e, const AhAdvBank *b, int slot, float *l, float *r)
{
    if (!b->on[slot])
        return;
    const float a = b->amt[slot];
    const uint32_t rate = b->rate ? b->rate : 48000;
    float left = *l, right = *r;

    switch (slot) {
    case AH_ADV_DC:
        left = bq_run(&b->f[F_DC], &e->mem[0][F_DC], left);
        right = bq_run(&b->f[F_DC], &e->mem[1][F_DC], right);
        break;
    case AH_ADV_HIGHPASS:
        left = bq_run(&b->f[F_HP], &e->mem[0][F_HP], left);
        right = bq_run(&b->f[F_HP], &e->mem[1][F_HP], right);
        break;
    case AH_ADV_NOISE: {
        float lo = bq_run(&b->f[F_NR0], &e->mem[0][F_NR0], 0.5f * (left + right));
        float hi = bq_run(&b->f[F_NR2], &e->mem[0][F_NR2], 0.5f * (left + right));
        float mid = 0.5f * (left + right) - lo;
        float bands[3] = { lo, mid, hi };
        float g[3];
        for (int i = 0; i < 3; ++i) {
            float en = fabsf(bands[i]);
            e->noise_floor[i] += ((en < e->noise_floor[i] ? en : e->noise_floor[i] * 1.002f)
                                  - e->noise_floor[i]) * 0.0004f;
            float thr = e->noise_floor[i] * (2.5f - a);
            g[i] = en < thr ? (en / (thr + 1e-8f)) : 1.f;
            if (g[i] < 1.f - a)
                g[i] = 1.f - a;
        }
        float mix = lo * g[0] + mid * g[1] + hi * g[2];
        float dry = 0.5f * (left + right);
        float wet = dry + (mix - dry) * a;
        float side = 0.5f * (left - right);
        left = wet + side;
        right = wet - side;
        break;
    }
    case AH_ADV_SPEC_GATE: {
        float mag = 0.5f * (fabsf(left) + fabsf(right));
        float thr = 0.002f + (1.f - a) * 0.03f;
        float g = mag < thr ? mag / (thr + 1e-8f) : 1.f;
        left *= g;
        right *= g;
        break;
    }
    case AH_ADV_RES_TAMER: {
        float pl = bq_run(&b->f[F_TAMER], &e->mem[0][F_TAMER], left);
        float pr = bq_run(&b->f[F_TAMER], &e->mem[1][F_TAMER], right);
        float det = 0.7071f * sqrtf(pl * pl + pr * pr);
        float g = comp_gain(&e->rms[0], det, tau_coeff(4.f, rate), tau_coeff(40.f, rate),
                            -18.f + (1.f - a) * 12.f, 3.f + a * 5.f, 4.f, 0.f);
        g = 1.f + (g - 1.f) * a;
        left -= pl * (1.f - g);
        right -= pr * (1.f - g);
        break;
    }
    case AH_ADV_DEESSER: {
        float sl = bq_run(&b->f[F_DEESS_HP], &e->mem[0][F_DEESS_HP], left);
        float sr = bq_run(&b->f[F_DEESS_HP], &e->mem[1][F_DEESS_HP], right);
        float det = bq_run(&b->f[F_DEESS], &e->mem[0][F_DEESS], 0.5f * (sl + sr));
        float g = comp_gain(&e->rms[7], det, tau_coeff(2.f, rate), tau_coeff(40.f, rate),
                            -24.f + (1.f - a) * 16.f, 4.f + a * 6.f, 4.f, 0.f);
        g = 1.f + (g - 1.f) * a;
        left = (left - sl) + sl * g;
        right = (right - sr) + sr * g;
        break;
    }
    case AH_ADV_DYN_BASS: {
        float bl = bq_run(&b->f[F_BASSLP], &e->mem[0][F_BASSLP], left);
        float br = bq_run(&b->f[F_BASSLP], &e->mem[1][F_BASSLP], right);
        float env = rms_follow(&e->rms[3], 0.5f * (bl + br), tau_coeff(12.f, rate),
                               tau_coeff(180.f, rate));
        float gate = clampf((env - 0.01f) * 18.f, 0.f, 1.f);
        float harm_l = tanh_adaa(&e->adaa[0], bl * (1.4f + a * 2.2f));
        float harm_r = tanh_adaa(&e->adaa[1], br * (1.4f + a * 2.2f));
        float mix = a * gate * 0.28f;
        left += (bl * a * 0.35f * gate) + (harm_l - bl) * mix;
        right += (br * a * 0.35f * gate) + (harm_r - br) * mix;
        break;
    }
    case AH_ADV_PSYCHO_BASS: {
        float bl = bq_run(&b->f[F_PSYCHO], &e->mem[0][F_PSYCHO], left);
        float br = bq_run(&b->f[F_PSYCHO], &e->mem[1][F_PSYCHO], right);
        /* Missing fundamental: even (2nd) harmonic of the low band. */
        float h_l = tanh_adaa(&e->adaa[2], bl * bl * (bl >= 0.f ? 2.f : -2.f));
        float h_r = tanh_adaa(&e->adaa[3], br * br * (br >= 0.f ? 2.f : -2.f));
        left += h_l * a * 0.38f;
        right += h_r * a * 0.38f;
        break;
    }
    case AH_ADV_COMPRESSOR: {
        float det = 0.7071f * sqrtf(left * left + right * right);
        float g = comp_gain(&e->env[3], det, b->comp_atk, b->comp_rel,
                            b->comp_thr, b->comp_ratio, b->comp_knee, b->comp_makeup);
        g = 1.f + (g - 1.f) * a;
        left *= g;
        right *= g;
        break;
    }
    case AH_ADV_MB_COMP: {
        float lo_l = bq_run(&b->f[F_MB_LO2], &e->mem[0][F_MB_LO2],
                            bq_run(&b->f[F_MB_LO], &e->mem[0][F_MB_LO], left));
        float lo_r = bq_run(&b->f[F_MB_LO2], &e->mem[1][F_MB_LO2],
                            bq_run(&b->f[F_MB_LO], &e->mem[1][F_MB_LO], right));
        float hi_l = bq_run(&b->f[F_MB_HI2], &e->mem[0][F_MB_HI2],
                            bq_run(&b->f[F_MB_HI], &e->mem[0][F_MB_HI], left));
        float hi_r = bq_run(&b->f[F_MB_HI2], &e->mem[1][F_MB_HI2],
                            bq_run(&b->f[F_MB_HI], &e->mem[1][F_MB_HI], right));
        float mid_l = left - lo_l - hi_l;
        float mid_r = right - lo_r - hi_r;
        float gl = comp_gain(&e->env[4], 0.7071f * sqrtf(lo_l * lo_l + lo_r * lo_r),
                             tau_coeff(40.f, rate), tau_coeff(200.f, rate),
                             -14.f - a * 8.f, 2.f + a * 3.f, 6.f, a * 1.5f);
        float gm = comp_gain(&e->env[5], 0.7071f * sqrtf(mid_l * mid_l + mid_r * mid_r),
                             tau_coeff(20.f, rate), tau_coeff(100.f, rate),
                             -16.f - a * 8.f, 2.5f + a * 3.5f, 6.f, a * 1.2f);
        float gh = comp_gain(&e->env[6], 0.7071f * sqrtf(hi_l * hi_l + hi_r * hi_r),
                             tau_coeff(8.f, rate), tau_coeff(60.f, rate),
                             -18.f - a * 6.f, 2.f + a * 4.f, 4.f, a * 0.8f);
        gl = 1.f + (gl - 1.f) * a;
        gm = 1.f + (gm - 1.f) * a;
        gh = 1.f + (gh - 1.f) * a;
        left = lo_l * gl + mid_l * gm + hi_l * gh;
        right = lo_r * gl + mid_r * gm + hi_r * gh;
        break;
    }
    case AH_ADV_TRANSIENT: {
        float hl = bq_run(&b->f[F_BASSHP], &e->mem[0][F_BASSHP], left);
        float hr = bq_run(&b->f[F_BASSHP], &e->mem[1][F_BASSHP], right);
        float mag = 0.5f * (fabsf(hl) + fabsf(hr));
        float fast = follow(&e->env[7], mag, tau_coeff(1.5f, rate), tau_coeff(30.f, rate));
        float slow = follow(&e->env[8], mag, tau_coeff(20.f, rate), tau_coeff(180.f, rate));
        float delta = (fast - slow) * (a - 0.5f) * 2.8f;
        left += hl * delta;
        right += hr * delta;
        break;
    }
    case AH_ADV_DYN_EQ: {
        float p = bq_run(&b->f[F_DYNEQ], &e->mem[0][F_DYNEQ], 0.5f * (left + right));
        float g = comp_gain(&e->env[9], p, tau_coeff(8.f, rate), tau_coeff(80.f, rate),
                            -20.f + (1.f - a) * 14.f, 3.f + a * 4.f, 6.f, 0.f);
        g = 1.f + (g - 1.f) * a;
        left -= p * (1.f - g);
        right -= p * (1.f - g);
        break;
    }
    case AH_ADV_ADAPT_EQ: {
        float lo = bq_run(&b->f[F_AD0], &e->mem[0][F_AD0], 0.5f * (left + right));
        float hi = bq_run(&b->f[F_AD2], &e->mem[0][F_AD2], 0.5f * (left + right));
        float mid = 0.5f * (left + right) - lo;
        follow(&e->env[10], lo, 0.002f, 0.002f);
        follow(&e->env[11], mid, 0.002f, 0.002f);
        follow(&e->env[12], hi, 0.002f, 0.002f);
        float mean = (e->env[10] + e->env[11] + e->env[12]) / 3.f + 1e-6f;
        float gl = 1.f + (mean / (e->env[10] + 1e-6f) - 1.f) * a * 0.25f;
        float gm = 1.f + (mean / (e->env[11] + 1e-6f) - 1.f) * a * 0.25f;
        float gh = 1.f + (mean / (e->env[12] + 1e-6f) - 1.f) * a * 0.25f;
        float wet = lo * gl + mid * gm + hi * gh;
        float side = 0.5f * (left - right);
        left = wet + side;
        right = wet - side;
        break;
    }
    case AH_ADV_PARAMETRIC:
        left = bq_run(&b->f[F_PARA0], &e->mem[0][F_PARA0], left);
        right = bq_run(&b->f[F_PARA0], &e->mem[1][F_PARA0], right);
        left = bq_run(&b->f[F_PARA1], &e->mem[0][F_PARA1], left);
        right = bq_run(&b->f[F_PARA1], &e->mem[1][F_PARA1], right);
        left = bq_run(&b->f[F_PARA2], &e->mem[0][F_PARA2], left);
        right = bq_run(&b->f[F_PARA2], &e->mem[1][F_PARA2], right);
        break;
    case AH_ADV_SPECTRAL:
        left = bq_run(&b->f[F_SPEC_P], &e->mem[0][F_SPEC_P], left);
        right = bq_run(&b->f[F_SPEC_P], &e->mem[1][F_SPEC_P], right);
        left = bq_run(&b->f[F_SPEC_S], &e->mem[0][F_SPEC_S], left);
        right = bq_run(&b->f[F_SPEC_S], &e->mem[1][F_SPEC_S], right);
        break;
    case AH_ADV_DYN_TREBLE: {
        float hl = bq_run(&b->f[F_TREBHP], &e->mem[0][F_TREBHP], left);
        float hr = bq_run(&b->f[F_TREBHP], &e->mem[1][F_TREBHP], right);
        float env = rms_follow(&e->env[13], 0.5f * (hl + hr),
                               tau_coeff(8.f, rate), tau_coeff(90.f, rate));
        float g = a * clampf(env * 3.2f, 0.f, 1.f) * 0.55f;
        left += hl * g;
        right += hr * g;
        break;
    }
    case AH_ADV_EXCITER: {
        float hl = bq_run(&b->f[F_EXCITE], &e->mem[0][F_EXCITE], left);
        float hr = bq_run(&b->f[F_EXCITE], &e->mem[1][F_EXCITE], right);
        left += tanh_adaa(&e->adaa[4], hl * (1.6f + a * 2.4f)) * a * 0.24f;
        right += tanh_adaa(&e->adaa[5], hr * (1.6f + a * 2.4f)) * a * 0.24f;
        break;
    }
    case AH_ADV_MB_EXCITER: {
        float lo_l = bq_run(&b->f[F_MB_LO], &e->mem[0][F_MB_LO], left);
        float lo_r = bq_run(&b->f[F_MB_LO], &e->mem[1][F_MB_LO], right);
        float hi_l = bq_run(&b->f[F_MB_HI], &e->mem[0][F_MB_HI], left);
        float hi_r = bq_run(&b->f[F_MB_HI], &e->mem[1][F_MB_HI], right);
        float mid_l = left - lo_l - hi_l;
        float mid_r = right - lo_r - hi_r;
        mid_l += tanh_adaa(&e->adaa[4], mid_l * (1.4f + a)) * a * 0.10f;
        mid_r += tanh_adaa(&e->adaa[5], mid_r * (1.4f + a)) * a * 0.10f;
        hi_l += tanh_adaa(&e->adaa[6], hi_l * (1.8f + a * 1.6f)) * a * 0.20f;
        hi_r += tanh_adaa(&e->adaa[7], hi_r * (1.8f + a * 1.6f)) * a * 0.20f;
        left = lo_l + mid_l + hi_l;
        right = lo_r + mid_r + hi_r;
        break;
    }
    case AH_ADV_ANALOG: {
        left = bq_run(&b->f[F_WARM], &e->mem[0][F_WARM], left);
        right = bq_run(&b->f[F_WARM], &e->mem[1][F_WARM], right);
        float wl = tanh_adaa(&e->adaa[8], left * (1.f + a * 0.9f));
        float wr = tanh_adaa(&e->adaa[9], right * (1.f + a * 0.9f));
        left = left + (wl - left) * a + left * left * 0.06f * a;
        right = right + (wr - right) * a + right * right * 0.06f * a;
        break;
    }
    case AH_ADV_TUBE: {
        float dl = left >= 0.f ? (1.15f + a * 0.7f) : (0.82f + 0.18f * a);
        float dr = right >= 0.f ? (1.15f + a * 0.7f) : (0.82f + 0.18f * a);
        left = tanh_adaa(&e->adaa[10], left * dl);
        right = tanh_adaa(&e->adaa[11], right * dr);
        break;
    }
    case AH_ADV_TAPE: {
        e->wow_phase += 6.2831853f * 0.7f / (float)rate;
        if (e->wow_phase > 6.2831853f)
            e->wow_phase -= 6.2831853f;
        float w = 1.f + 0.0035f * a * sinf(e->wow_phase);
        left = tanh_adaa(&e->adaa[12], left * (1.05f + 0.35f * a) * w);
        right = tanh_adaa(&e->adaa[13], right * (1.05f + 0.35f * a) * w);
        left += rng_f(&e->rng) * 0.0012f * a;
        right += rng_f(&e->rng) * 0.0012f * a;
        break;
    }
    case AH_ADV_DISTORT:
        left = tanh_adaa(&e->adaa[14], left * (1.f + a * 4.f));
        right = tanh_adaa(&e->adaa[15], right * (1.f + a * 4.f));
        break;
    case AH_ADV_VINYL: {
        float rum = bq_run(&b->f[F_VINYL_R], &e->mem[0][F_VINYL_R], rng_f(&e->rng));
        left = bq_run(&b->f[F_VINYL_M], &e->mem[0][F_VINYL_M], left) + rum * 0.03f * a;
        right = bq_run(&b->f[F_VINYL_M], &e->mem[1][F_VINYL_M], right) + rum * 0.03f * a;
        if ((rng_next(&e->rng) & 1023) < (uint32_t)(2 + a * 10.f)) {
            float click = rng_f(&e->rng) * 0.12f * a;
            left += click;
            right += click * 0.7f;
        }
        break;
    }
    case AH_ADV_VINTAGE:
        left = bq_run(&b->f[F_VINT_LO], &e->mem[0][F_VINT_LO], left);
        right = bq_run(&b->f[F_VINT_LO], &e->mem[1][F_VINT_LO], right);
        left = bq_run(&b->f[F_VINT_HI], &e->mem[0][F_VINT_HI], left);
        right = bq_run(&b->f[F_VINT_HI], &e->mem[1][F_VINT_HI], right);
        left += (tanh_adaa(&e->adaa[8], left) - left) * a * 0.35f;
        right += (tanh_adaa(&e->adaa[9], right) - right) * a * 0.35f;
        break;
    case AH_ADV_LOFI: {
        int hold = 1 + (int)(a * 6.f);
        if (e->lofi_n == 0) {
            e->lofi_hold[0] = left;
            e->lofi_hold[1] = right;
        }
        e->lofi_n = (e->lofi_n + 1) % hold;
        float steps = 4.f + (1.f - a) * 28.f;
        left = floorf(e->lofi_hold[0] * steps) / steps;
        right = floorf(e->lofi_hold[1] * steps) / steps;
        left = bq_run(&b->f[F_LOFI], &e->mem[0][F_LOFI], left);
        right = bq_run(&b->f[F_LOFI], &e->mem[1][F_LOFI], right);
        break;
    }
    case AH_ADV_STEREO_IMAGE: {
        float lo_l = bq_run(&b->f[F_FD_LO], &e->mem[0][F_FD_LO], left);
        float lo_r = bq_run(&b->f[F_FD_LO], &e->mem[1][F_FD_LO], right);
        float hi_l = left - lo_l;
        float hi_r = right - lo_r;
        float bass_m = 0.5f * (lo_l + lo_r);
        float bass_w = 1.f + (a - 0.5f) * 0.35f;
        float side_lo = 0.5f * (lo_l - lo_r) * bass_w;
        ms_width(&hi_l, &hi_r, 1.f + (a - 0.5f) * 1.4f);
        left = bass_m + side_lo + hi_l;
        right = bass_m - side_lo + hi_r;
        break;
    }
    case AH_ADV_WIDEN: {
        float lo_l = bq_run(&b->f[F_FD_LO], &e->mem[0][F_FD_LO], left);
        float lo_r = bq_run(&b->f[F_FD_LO], &e->mem[1][F_FD_LO], right);
        float hi_l = left - lo_l;
        float hi_r = right - lo_r;
        float mid = 0.5f * (lo_l + lo_r);
        ms_width(&hi_l, &hi_r, 1.f + a * 1.25f);
        left = mid + hi_l;
        right = mid + hi_r;
        break;
    }
    case AH_ADV_FD_WIDTH: {
        float lo_l = bq_run(&b->f[F_FD_LO], &e->mem[0][F_FD_LO], left);
        float lo_r = bq_run(&b->f[F_FD_LO], &e->mem[1][F_FD_LO], right);
        float hi_l = left - lo_l;
        float hi_r = right - lo_r;
        float mid = 0.5f * (lo_l + lo_r);
        ms_width(&hi_l, &hi_r, 1.f + a * 1.4f);
        left = mid + hi_l;
        right = mid + hi_r;
        break;
    }
    case AH_ADV_CROSSFEED: {
        float dry_l = left, dry_r = right;
        bs2b_tick(b, e, &left, &right);
        left = dry_l * (1.f - a) + left * a;
        right = dry_r * (1.f - a) + right * a;
        break;
    }
    case AH_ADV_SPEAKERS: {
        e->xdel[0][e->xpos] = left;
        e->xdel[1][e->xpos] = right;
        int d = (e->xpos - b->speak_delay + AH_ADV_XDEL) % AH_ADV_XDEL;
        left = left * (1.f - 0.15f * a) + e->xdel[1][d] * 0.18f * a;
        right = right * (1.f - 0.15f * a) + e->xdel[0][d] * 0.18f * a;
        e->xpos = (e->xpos + 1) % AH_ADV_XDEL;
        break;
    }
    case AH_ADV_SPATIAL3D:
        ah_spatial_process(&e->spatial, &b->spatial, &left, &right);
        break;
    case AH_ADV_ROOM:
        run_reverb(e, b, &left, &right, b->comb_len, 0.58f, a * 0.20f, 0.28f);
        break;
    case AH_ADV_HALL:
        run_reverb(e, b, &left, &right, b->hall_len, 0.76f, a * 0.30f, 0.16f);
        break;
    case AH_ADV_CONVOLVE: {
        e->ir_buf[0][e->ir_pos] = left;
        e->ir_buf[1][e->ir_pos] = right;
        float acc_l = 0.f, acc_r = 0.f;
        for (int i = 0; i < AH_ADV_IR; ++i) {
            int idx = (e->ir_pos - i + AH_ADV_IR) % AH_ADV_IR;
            acc_l += e->ir_buf[0][idx] * b->ir[i];
            acc_r += e->ir_buf[1][idx] * b->ir[i];
        }
        e->ir_pos = (e->ir_pos + 1) % AH_ADV_IR;
        left += (acc_l - left) * a * 0.55f;
        right += (acc_r - right) * a * 0.55f;
        break;
    }
    case AH_ADV_AGC: {
        float det = 0.7071f * sqrtf(left * left + right * right);
        float env = rms_follow(&e->rms[2], det, tau_coeff(200.f, rate), tau_coeff(800.f, rate));
        float target = db2lin(-18.f);
        float want = env > 1e-5f ? target / env : 1.f;
        want = clampf(want, 0.4f, 2.4f);
        e->agc_g += tau_coeff(250.f, rate) * (want - e->agc_g);
        float g = 1.f + (e->agc_g - 1.f) * a;
        left *= g;
        right *= g;
        break;
    }
    case AH_ADV_LOUDNESS: {
        float kl = bq_run(&b->f[F_KHP], &e->mem[0][F_KHP],
                          bq_run(&b->f[F_KSH], &e->mem[0][F_KSH], left));
        float kr = bq_run(&b->f[F_KHP], &e->mem[1][F_KHP],
                          bq_run(&b->f[F_KSH], &e->mem[1][F_KSH], right));
        float ms = kl * kl + kr * kr;
        e->rms[1] += (ms - e->rms[1]) * tau_coeff(400.f, rate);
        float lufs = -0.691f + 10.f * log10f(e->rms[1] + 1e-12f);
        float want = 1.f;
        if (lufs > -60.f) {
            float err = clampf(b->loud_target - lufs, -12.f, 12.f);
            want = db2lin(err * (0.4f + 0.6f * a));
            want = clampf(want, 0.4f, 2.2f);
        }
        e->loud_g += tau_coeff(220.f, rate) * (want - e->loud_g);
        left *= e->loud_g;
        right *= e->loud_g;
        break;
    }
    case AH_ADV_HEADROOM: {
        float g = db2lin(-0.3f - a * 5.7f);
        left *= g;
        right *= g;
        break;
    }
    case AH_ADV_MONO:
        if (a >= 0.7f) {
            float m = 0.5f * (left + right);
            left = m;
            right = m;
        }
        break;
    case AH_ADV_LIMITER: {
        e->la[0][e->la_i] = left;
        e->la[1][e->la_i] = right;
        float tp = fmaxf(truepeak4(e->la_prev[0], left), truepeak4(e->la_prev[1], right));
        e->la_prev[0] = left;
        e->la_prev[1] = right;
        float need = (tp > b->la_ceil) ? (b->la_ceil / (tp + 1e-12f)) : 1.f;
        float rel = tau_coeff(8.f, rate);
        if (need < e->la_g) {
            e->la_g = need;
            e->hold_n = (float)b->la_len;
        } else if (e->hold_n > 0.f) {
            e->hold_n -= 1.f;
        } else {
            e->la_g += (1.f - e->la_g) * rel;
        }
        int rd = (e->la_i - b->la_len + AH_ADV_LA) % AH_ADV_LA;
        left = e->la[0][rd] * e->la_g;
        right = e->la[1][rd] * e->la_g;
        e->la_i = (e->la_i + 1) % AH_ADV_LA;
        break;
    }
    default:
        break;
    }

    *l = left;
    *r = right;
}

static void run_group(AhAdvancedEngine *e, const AhAdvBank *b,
                      const int *ids, int n, float *l, float *r)
{
    for (int i = 0; i < n; ++i)
        run_slot(e, b, ids[i], l, r);
}

static void run_chain(AhAdvancedEngine *e, const AhAdvBank *b, float *l, float *r)
{
    const int *first = k_tone;
    int nfirst = (int)(sizeof(k_tone) / sizeof(k_tone[0]));
    const int *second = k_color;
    int nsecond = (int)(sizeof(k_color) / sizeof(k_color[0]));
    const int *third = k_space;
    int nthird = (int)(sizeof(k_space) / sizeof(k_space[0]));

    if (b->order == 2) {
        first = k_color;
        nfirst = (int)(sizeof(k_color) / sizeof(k_color[0]));
        second = k_tone;
        nsecond = (int)(sizeof(k_tone) / sizeof(k_tone[0]));
    } else if (b->order == 3) {
        first = k_color;
        nfirst = (int)(sizeof(k_color) / sizeof(k_color[0]));
        second = k_space;
        nsecond = (int)(sizeof(k_space) / sizeof(k_space[0]));
        third = k_tone;
        nthird = (int)(sizeof(k_tone) / sizeof(k_tone[0]));
    }

    run_group(e, b, k_cleanup, (int)(sizeof(k_cleanup) / sizeof(k_cleanup[0])), l, r);
    run_group(e, b, first, nfirst, l, r);
    run_group(e, b, second, nsecond, l, r);
    run_group(e, b, third, nthird, l, r);
    run_group(e, b, k_output, (int)(sizeof(k_output) / sizeof(k_output[0])), l, r);
}

AhAdvancedEngine *ah_advanced_create(void)
{
    AhAdvancedEngine *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    atomic_init(&e->idx, 0);
    e->rng = 0xC001D00Du;
    e->la_g = 1.f;
    e->gm_g = 1.f;
    e->loud_g = 1.f;
    e->agc_g = 1.f;
    AhAdvancedState s;
    ah_advanced_defaults(&s);
    design_bank(&e->banks[0], &s, 48000);
    return e;
}

void ah_advanced_destroy(AhAdvancedEngine *eng)
{
    free(eng);
}

void ah_advanced_reset(AhAdvancedEngine *eng)
{
    if (!eng)
        return;
    memset(eng->mem, 0, sizeof(eng->mem));
    memset(&eng->spatial, 0, sizeof(eng->spatial));
    memset(eng->comb, 0, sizeof(eng->comb));
    memset(eng->ap, 0, sizeof(eng->ap));
    memset(eng->ir_buf, 0, sizeof(eng->ir_buf));
    memset(eng->xdel, 0, sizeof(eng->xdel));
    memset(eng->env, 0, sizeof(eng->env));
    memset(eng->rms, 0, sizeof(eng->rms));
    memset(eng->bs2b_lo, 0, sizeof(eng->bs2b_lo));
    memset(eng->bs2b_hi, 0, sizeof(eng->bs2b_hi));
    memset(eng->adaa, 0, sizeof(eng->adaa));
    memset(eng->la, 0, sizeof(eng->la));
    memset(eng->la_prev, 0, sizeof(eng->la_prev));
    memset(eng->comb_lp, 0, sizeof(eng->comb_lp));
    memset(eng->spec_e, 0, sizeof(eng->spec_e));
    eng->lofi_n = 0;
    eng->xpos = 0;
    eng->ir_pos = 0;
    eng->la_i = 0;
    eng->hold_n = 0.f;
    eng->la_g = 1.f;
    eng->gm_g = 1.f;
    eng->loud_g = 1.f;
    eng->agc_g = 1.f;
    eng->peak = 0.f;
    eng->clip_hold = 0.f;
}

void ah_advanced_apply(AhAdvancedEngine *eng, const AhAdvancedState *s, uint32_t rate)
{
    if (!eng || !s)
        return;
    int wi = 1 - atomic_load_explicit(&eng->idx, memory_order_relaxed);
    AhAdvancedState tmp = *s;
    ah_advanced_clamp(&tmp);
    design_bank(&eng->banks[wi], &tmp, rate ? rate : 48000);
    atomic_store_explicit(&eng->idx, wi, memory_order_release);
}

void ah_advanced_process_block(AhAdvancedEngine *eng, float *left, float *right,
                               uint32_t n, uint32_t rate)
{
    if (!eng || !left || !right || n == 0)
        return;

    const int ri = atomic_load_explicit(&eng->idx, memory_order_acquire);
    const AhAdvBank *b = &eng->banks[ri];

    if (rate > 0 && rate != eng->rate) {
        eng->rate = rate;
        ah_advanced_reset(eng);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    float peak = 0.f;
    float dry_acc = 0.f, wet_acc = 0.f, corr = 0.f, energy = 0.f;
    float spec[AH_ADV_SPEC];
    memcpy(spec, eng->spec_e, sizeof(spec));

    for (uint32_t i = 0; i < n; ++i) {
        float l = left[i];
        float r = right[i];
        float dry_l = l, dry_r = r;
        dry_acc += dry_l * dry_l + dry_r * dry_r;

        if (!b->ab)
            run_chain(eng, b, &l, &r);

        wet_acc += l * l + r * r;
        corr += l * r;
        energy += 0.5f * (l * l + r * r);
        float p = fmaxf(fabsf(l), fabsf(r));
        if (p > peak)
            peak = p;

        float mag = 0.5f * (fabsf(l) + fabsf(r));
        for (int k = 0; k < AH_ADV_SPEC; ++k) {
            float alpha = 0.02f + 0.06f * (float)k / (float)AH_ADV_SPEC;
            spec[k] += alpha * (mag - spec[k]);
            mag *= 0.72f;
        }

        left[i] = l;
        right[i] = r;
    }

    if (b->gain_match && !b->ab) {
        float g = (wet_acc > 1e-10f) ? sqrtf(dry_acc / wet_acc) : 1.f;
        g = clampf(g, 0.4f, 2.f);
        eng->gm_g += 0.18f * (g - eng->gm_g);
        g = eng->gm_g;
        wet_acc = 0.f;
        corr = 0.f;
        energy = 0.f;
        peak = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            left[i] *= g;
            right[i] *= g;
            wet_acc += left[i] * left[i] + right[i] * right[i];
            corr += left[i] * right[i];
            energy += 0.5f * (left[i] * left[i] + right[i] * right[i]);
            float p = fmaxf(fabsf(left[i]), fabsf(right[i]));
            if (p > peak)
                peak = p;
        }
    }

    memcpy(eng->spec_e, spec, sizeof(spec));
    if (peak > 0.99f)
        eng->clip_hold = (float)rate * 0.8f;
    else if (eng->clip_hold > 0.f)
        eng->clip_hold -= (float)n;
    eng->peak += 0.25f * (peak - eng->peak);
    eng->dry_ms = sqrtf(dry_acc / (float)(n * 2u) + 1e-12f);
    eng->wet_ms = sqrtf(wet_acc / (float)(n * 2u) + 1e-12f);
    eng->corr_n = corr;
    eng->corr_d = energy;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double used = (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    double budget = rate > 0 ? (double)n / (double)rate : 1.0;
    float cpu = (float)(used / (budget > 1e-9 ? budget : 1e-9));
    eng->cpu += 0.2f * (cpu - eng->cpu);

    atomic_store(&eng->pub_peak, (int)(eng->peak * 1000.f));
    atomic_store(&eng->pub_clip, eng->clip_hold > 0.f ? 1 : 0);
    atomic_store(&eng->pub_cpu, (int)(eng->cpu * 1000.f));
    float corr_n = energy > 1e-12f ? (corr / (energy * 2.f)) : 1.f;
    if (corr_n > 1.f)
        corr_n = 1.f;
    if (corr_n < -1.f)
        corr_n = -1.f;
    atomic_store(&eng->pub_corr, (int)(corr_n * 100.f));
    atomic_store(&eng->pub_dry, (int)(eng->dry_ms * 1000.f));
    atomic_store(&eng->pub_wet, (int)(eng->wet_ms * 1000.f));
    for (int k = 0; k < AH_ADV_SPEC; ++k) {
        float v = spec[k] * 8.f;
        if (v > 1.f)
            v = 1.f;
        atomic_store(&eng->pub_spec[k], (int)(v * 1000.f));
    }
}

void ah_advanced_meters(const AhAdvancedEngine *eng, AhAdvancedMeters *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!eng)
        return;
    out->peak = atomic_load(&eng->pub_peak) / 1000.f;
    out->cpu = atomic_load(&eng->pub_cpu) / 1000.f;
    out->corr = atomic_load(&eng->pub_corr) / 100.f;
    out->dry_rms = atomic_load(&eng->pub_dry) / 1000.f;
    out->wet_rms = atomic_load(&eng->pub_wet) / 1000.f;
    out->clip = atomic_load(&eng->pub_clip) != 0;
    for (int k = 0; k < AH_ADV_SPEC; ++k)
        out->spec[k] = atomic_load(&eng->pub_spec[k]) / 1000.f;
}