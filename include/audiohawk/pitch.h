#ifndef AUDIOHAWK_PITCH_H
#define AUDIOHAWK_PITCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Power-of-two delay: ~85 ms at 48 kHz. No RT allocation. */
#define AH_PITCH_BUF 4096
#define AH_PITCH_MIN -15
#define AH_PITCH_MAX 15

typedef struct {
    float l[AH_PITCH_BUF];
    float r[AH_PITCH_BUF];
    int w;
    int last;
    double rpos;
    double ratio;
} AhPitchState;

int ah_pitch_clamp(int semitones);
void ah_pitch_reset(AhPitchState *s);
void ah_pitch_process(AhPitchState *s, int semitones, float *left, float *right);

#ifdef __cplusplus
}
#endif

#endif
