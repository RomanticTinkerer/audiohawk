#ifndef AUDIOHAWK_DSP_H
#define AUDIOHAWK_DSP_H

#include "audiohawk/profiles.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#define ah_cpu_relax() ((void)0)
#else
#include <sched.h>
#define ah_cpu_relax() sched_yield()
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Double precision: low bells at high rates sit right on the unit circle. */
typedef struct {
    double b0, b1, b2, a1, a2;
} AhBiquad;

typedef struct {
    double z1, z2;
} AhBiquadMem;

void ah_bq_unity(AhBiquad *bq);
void ah_bq_peaking(AhBiquad *bq, double freq, double q, double gain_db, uint32_t rate);
void ah_bq_lowshelf(AhBiquad *bq, double freq, double q, double gain_db, uint32_t rate);
void ah_bq_highshelf(AhBiquad *bq, double freq, double q, double gain_db, uint32_t rate);
void ah_bq_lowpass(AhBiquad *bq, double freq, double q, uint32_t rate);
void ah_bq_highpass(AhBiquad *bq, double freq, double q, uint32_t rate);
double ah_bq_mag_db(const AhBiquad *bq, double freq, uint32_t rate);

static inline double ah_bq_run(const AhBiquad *bq, AhBiquadMem *m, double x)
{
    const double y = bq->b0 * x + m->z1;
    m->z1 = bq->b1 * x - bq->a1 * y + m->z2;
    m->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

/*
 * Graphic EQ whose real response matches the drawn gains at band centers and
 * midpoints (least-squares band-interaction compensation, after Valimaki and
 * Liski's accurate cascade graphic EQ).
 */
void ah_geq_design(const AhEqBand *bands, int n, uint32_t rate, AhBiquad *out);

/* Stereo-linked lookahead brickwall. Output is the input delayed by the lookahead. */
#define AH_LIM_MAX 1024

typedef struct {
    float ceil;     /* linear */
    bool true_peak; /* also catch inter-sample peaks */
    int look;
    float rel;
    float hold;
    int hold_n;
    double sum;
    int pos;
    float hist[2][3];
    float dl[2][AH_LIM_MAX];
    float gbuf[AH_LIM_MAX];
} AhLimiter;

void ah_limiter_init(AhLimiter *m, uint32_t rate, float ceil_db, float look_ms,
                     float release_ms, bool true_peak);
void ah_limiter_tick(AhLimiter *m, float *left, float *right);

/* Flush denormals on the calling thread (IIR tails in silence). */
void ah_dsp_no_denormals(void);

/*
 * Double-buffer handoff. The RT thread marks the bank it reads; the UI side
 * never overwrites that bank mid-block.
 */
static inline int ah_rt_acquire(atomic_int *idx, atomic_int *busy)
{
    int i;
    do {
        i = atomic_load(idx);
        atomic_store(busy, i);
    } while (atomic_load(idx) != i);
    return i;
}

static inline void ah_rt_release(atomic_int *busy)
{
    atomic_store(busy, -1);
}

static inline int ah_ui_slot(atomic_int *idx, atomic_int *busy)
{
    const int w = 1 - atomic_load(idx);
    while (atomic_load(busy) == w)
        ah_cpu_relax();
    return w;
}

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_DSP_H */
