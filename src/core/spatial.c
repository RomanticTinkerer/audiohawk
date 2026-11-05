#include "audiohawk/spatial.h"
#include <math.h>
#include <string.h>

static int percent(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void ah_spatial_configure(AhSpatialParams *p, uint32_t rate, bool enabled,
                          int amount, int bass_percent, int treble_percent)
{
    memset(p, 0, sizeof(*p));
    if (!rate) rate = 48000;
    p->rate = rate;
    p->enabled = enabled;
    p->amount = percent(amount) / 100.f;
    p->cross_delay = (int)(rate * 0.00032f);
    p->room_delay = (int)(rate * 0.012f);
    if (p->cross_delay < 1) p->cross_delay = 1;
    if (p->room_delay < 1) p->room_delay = 1;
    if (p->cross_delay >= AH_SPATIAL_DELAY_MAX) p->cross_delay = AH_SPATIAL_DELAY_MAX - 1;
    if (p->room_delay >= AH_SPATIAL_DELAY_MAX) p->room_delay = AH_SPATIAL_DELAY_MAX - 1;
    p->low_alpha = 1.f - expf(-2.f * (float)M_PI * 220.f / rate);
    p->shadow_alpha = 1.f - expf(-2.f * (float)M_PI * 4000.f / rate);
    p->smooth = 1.f - expf(-1.f / (0.02f * rate));
    /* Percent means amplitude, not percent of a dB slider or perceived loudness. */
    ah_bq_lowshelf(&p->bass, 120.0, 0.707,
                   20.0 * log10(1.0 + percent(bass_percent) / 100.0), rate);
    ah_bq_highshelf(&p->treble, 6500.0, 0.707,
                    20.0 * log10(1.0 + percent(treble_percent) / 100.0), rate);
}

void ah_spatial_process(AhSpatialState *s, const AhSpatialParams *p,
                        float *left, float *right)
{
    if (s->rate != p->rate) {
        memset(s, 0, sizeof(*s));
        s->rate = p->rate;
    }
    s->mix += ((p->enabled ? 1.f : 0.f) - s->mix) * p->smooth;
    if (!p->enabled && s->mix < 0.000001f) s->mix = 0.f;
    float dry[2] = {*left, *right}, wet[2];
    for (int c = 0; c < 2; ++c) {
        wet[c] = (float)ah_bq_run(&p->bass, &s->tone[c][0], dry[c]);
        wet[c] = (float)ah_bq_run(&p->treble, &s->tone[c][1], wet[c]);
        /* Band-limit only the added signal, preserving all direct bass. */
        s->low[c] += p->low_alpha * (wet[c] - s->low[c]);
        s->shadow[c] += p->shadow_alpha * (wet[c] - s->low[c] - s->shadow[c]);
        s->delay[c][s->pos] = s->shadow[c];
    }
    int cross = (s->pos - p->cross_delay + AH_SPATIAL_DELAY_MAX) % AH_SPATIAL_DELAY_MAX;
    int room = (s->pos - p->room_delay + AH_SPATIAL_DELAY_MAX) % AH_SPATIAL_DELAY_MAX;
    /* Symmetric virtual speakers: quieter delayed opposite-ear sound and
     * early room reflections. Feed forward only, without recirculating echoes.
     * Added side energy has zero mono sum and leaves the center intact. */
    float side = 0.15f * p->amount * (s->shadow[0] - s->shadow[1]);
    wet[0] += side + p->amount * (0.18f * s->delay[1][cross] + 0.10f * s->delay[1][room]);
    wet[1] += -side + p->amount * (0.18f * s->delay[0][cross] + 0.10f * s->delay[0][room]);
    *left = dry[0] + s->mix * (wet[0] - dry[0]);
    *right = dry[1] + s->mix * (wet[1] - dry[1]);
    s->pos = (s->pos + 1) % AH_SPATIAL_DELAY_MAX;
}
