#include "audiohawk/spatial.h"
#include <math.h>
#include <string.h>

static int percent(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static void design_unity(AhSpatialBiquad *bq)
{
    bq->b0 = 1.f;
    bq->b1 = 0.f;
    bq->b2 = 0.f;
    bq->a1 = 0.f;
    bq->a2 = 0.f;
}

static void clamp_freq(double *freq, uint32_t rate)
{
    if (*freq < 20.f)
        *freq = 20.f;
    if (*freq > (double)rate * 0.45f)
        *freq = (double)rate * 0.45f;
}

static void design_lowshelf(AhSpatialBiquad *bq, double freq, double q, double gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabs(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }

    const double A = pow(10.f, gain_db / 40.f);
    const double w0 = 2.f * (double)M_PI * freq / (double)rate;
    const double cosw = cos(w0);
    const double sinw = sin(w0);
    const double alpha = sinw / (2.f * q);
    const double two_sqrtA_alpha = 2.f * sqrt(A) * alpha;

    const double b0 = A * ((A + 1.f) - (A - 1.f) * cosw + two_sqrtA_alpha);
    const double b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw);
    const double b2 = A * ((A + 1.f) - (A - 1.f) * cosw - two_sqrtA_alpha);
    const double a0 = (A + 1.f) + (A - 1.f) * cosw + two_sqrtA_alpha;
    const double a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw);
    const double a2 = (A + 1.f) + (A - 1.f) * cosw - two_sqrtA_alpha;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_highshelf(AhSpatialBiquad *bq, double freq, double q, double gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabs(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }

    const double A = pow(10.f, gain_db / 40.f);
    const double w0 = 2.f * (double)M_PI * freq / (double)rate;
    const double cosw = cos(w0);
    const double sinw = sin(w0);
    const double alpha = sinw / (2.f * q);
    const double two_sqrtA_alpha = 2.f * sqrt(A) * alpha;

    const double b0 = A * ((A + 1.f) + (A - 1.f) * cosw + two_sqrtA_alpha);
    const double b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw);
    const double b2 = A * ((A + 1.f) + (A - 1.f) * cosw - two_sqrtA_alpha);
    const double a0 = (A + 1.f) - (A - 1.f) * cosw + two_sqrtA_alpha;
    const double a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw);
    const double a2 = (A + 1.f) - (A - 1.f) * cosw - two_sqrtA_alpha;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}


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
    design_lowshelf(&p->bass, 120.f, 0.707f,
                    20.f * log10f(1.f + percent(bass_percent) / 100.f), rate);
    design_highshelf(&p->treble, 6500.f, 0.707f,
                     20.f * log10f(1.f + percent(treble_percent) / 100.f), rate);
}

static float tone(const AhSpatialBiquad *b, double mem[2], float x)
{
    double y = b->b0 * x + mem[0];
    mem[0] = b->b1 * x - b->a1 * y + mem[1];
    mem[1] = b->b2 * x - b->a2 * y;
    return y;
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
        wet[c] = tone(&p->bass, s->tone[c][0], dry[c]);
        wet[c] = tone(&p->treble, s->tone[c][1], wet[c]);
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

void ah_spatial_limit(float *left, float *right)
{
    float peak = fmaxf(fabsf(*left), fabsf(*right));
    if (peak > 0.97f) {
        float ceiling = 0.97f + 0.03f * tanhf((peak - 0.97f) / 0.03f);
        float gain = ceiling / peak;
        *left *= gain;
        *right *= gain;
    }
}
