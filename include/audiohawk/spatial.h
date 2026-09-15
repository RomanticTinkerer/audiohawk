#ifndef AUDIOHAWK_SPATIAL_H
#define AUDIOHAWK_SPATIAL_H

#include <stdbool.h>
#include <stdint.h>

/* Fixed storage covers 12 ms at rates up to 384 kHz. No RT allocation. */
#define AH_SPATIAL_DELAY_MAX 8192
typedef struct {
    double b0, b1, b2, a1, a2;
} AhSpatialBiquad;
typedef struct {
    AhSpatialBiquad bass, treble;
    float low_alpha, shadow_alpha, amount, smooth;
    int cross_delay, room_delay;
    uint32_t rate;
    bool enabled;
} AhSpatialParams;
typedef struct {
    float delay[2][AH_SPATIAL_DELAY_MAX];
    float low[2], shadow[2];
    double tone[2][2][2];
    float mix;
    int pos;
    uint32_t rate;
} AhSpatialState;

void ah_spatial_configure(AhSpatialParams *p, uint32_t rate, bool enabled,
                          int amount, int bass_percent, int treble_percent);
void ah_spatial_process(AhSpatialState *s, const AhSpatialParams *p,
                        float *left, float *right);
/* Shared peak gain preserves the L/R ratio and never exceeds full scale. */
void ah_spatial_limit(float *left, float *right);
#endif
