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

/* 4-point Hermite: far less HF loss and aliasing than linear. */
static float hermite_at(const float *buf, double pos)
{
    pos -= (double)AH_PITCH_BUF * floor(pos / (double)AH_PITCH_BUF);
    const int m = AH_PITCH_BUF - 1;
    const int i = (int)pos & m;
    const float f = (float)(pos - floor(pos));
    const float xm = buf[(i - 1) & m], x0 = buf[i], x1 = buf[(i + 1) & m], x2 = buf[(i + 2) & m];
    const float c1 = 0.5f * (x1 - xm);
    const float c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

void ah_pitch_process(AhPitchState *s, int semitones, float *left, float *right)
{
    if (!s || !left || !right)
        return;

    semitones = ah_pitch_clamp(semitones);
    s->l[s->w] = *left;
    s->r[s->w] = *right;

    if (semitones != 0 && semitones != s->last) {
        s->last = semitones;
        s->ratio = pow(2.0, (double)semitones / 12.0);
    }
    /* ~20 ms fade between dry and shifted so toggling never clicks. */
    s->mix += ((semitones ? 1.f : 0.f) - s->mix) * 0.001f;
    if (semitones == 0 && s->mix < 1e-4f) {
        s->mix = 0.f;
        s->last = 0;
        s->ratio = 1.0;
        s->rpos = (double)s->w;
        s->w = (s->w + 1) & (AH_PITCH_BUF - 1);
        return;
    }

    const int hop = AH_PITCH_BUF / 2;
    double rd0 = s->rpos;
    double rd1 = rd0 + (double)hop;

    double dist = (double)s->w - rd0;
    dist -= (double)AH_PITCH_BUF * floor(dist / (double)AH_PITCH_BUF);
    /* Each tap fades to zero exactly where it wraps past the write head. */
    const float sn = sinf((float)(M_PI * dist / AH_PITCH_BUF));
    const float w0 = sn * sn;
    const float w1 = 1.f - w0;

    const float pl = hermite_at(s->l, rd0) * w0 + hermite_at(s->l, rd1) * w1;
    const float pr = hermite_at(s->r, rd0) * w0 + hermite_at(s->r, rd1) * w1;
    *left += s->mix * (pl - *left);
    *right += s->mix * (pr - *right);

    s->rpos += s->ratio;
    while (s->rpos >= (double)AH_PITCH_BUF)
        s->rpos -= (double)AH_PITCH_BUF;
    while (s->rpos < 0.0)
        s->rpos += (double)AH_PITCH_BUF;
    s->w = (s->w + 1) & (AH_PITCH_BUF - 1);
}
