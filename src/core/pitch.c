#include "audiohawk/pitch.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int ah_pitch_clamp(int semitones)
{
    if (semitones < AH_PITCH_MIN)
        return AH_PITCH_MIN;
    if (semitones > AH_PITCH_MAX)
        return AH_PITCH_MAX;
    return semitones;
}

void ah_pitch_reset(AhPitchState *s)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->ratio = 1.0;
}

static float lerp_at(const float *buf, double pos)
{
    while (pos < 0.0)
        pos += (double)AH_PITCH_BUF;
    while (pos >= (double)AH_PITCH_BUF)
        pos -= (double)AH_PITCH_BUF;
    int i0 = (int)pos;
    int i1 = i0 + 1;
    if (i1 >= AH_PITCH_BUF)
        i1 = 0;
    float f = (float)(pos - (double)i0);
    return buf[i0] + f * (buf[i1] - buf[i0]);
}

void ah_pitch_process(AhPitchState *s, int semitones, float *left, float *right)
{
    if (!s || !left || !right)
        return;

    semitones = ah_pitch_clamp(semitones);
    s->l[s->w] = *left;
    s->r[s->w] = *right;

    if (semitones == 0) {
        s->last = 0;
        s->ratio = 1.0;
        s->rpos = (double)s->w;
        s->w = (s->w + 1) & (AH_PITCH_BUF - 1);
        return;
    }

    if (semitones != s->last) {
        s->last = semitones;
        s->ratio = pow(2.0, (double)semitones / 12.0);
    }

    const int hop = AH_PITCH_BUF / 2;
    double rd0 = s->rpos;
    double rd1 = rd0 + (double)hop;

    double dist = (double)s->w - rd0;
    while (dist < 0.0)
        dist += (double)AH_PITCH_BUF;
    while (dist >= (double)AH_PITCH_BUF)
        dist -= (double)AH_PITCH_BUF;
    double t = dist / (double)hop;
    t -= floor(t);
    float w0 = 0.5f * (1.f + cosf((float)(M_PI * t)));
    float w1 = 1.f - w0;

    *left = lerp_at(s->l, rd0) * w0 + lerp_at(s->l, rd1) * w1;
    *right = lerp_at(s->r, rd0) * w0 + lerp_at(s->r, rd1) * w1;

    s->rpos += s->ratio;
    while (s->rpos >= (double)AH_PITCH_BUF)
        s->rpos -= (double)AH_PITCH_BUF;
    while (s->rpos < 0.0)
        s->rpos += (double)AH_PITCH_BUF;
    s->w = (s->w + 1) & (AH_PITCH_BUF - 1);
}
