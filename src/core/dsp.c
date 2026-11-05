#include "audiohawk/dsp.h"

#include <math.h>
#include <string.h>

#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void ah_bq_unity(AhBiquad *bq)
{
    bq->b0 = 1.0;
    bq->b1 = bq->b2 = bq->a1 = bq->a2 = 0.0;
}

static void prep(double *freq, double *q, uint32_t *rate)
{
    if (*rate < 1)
        *rate = 48000;
    if (*freq < 10.0)
        *freq = 10.0;
    if (*freq > *rate * 0.45)
        *freq = *rate * 0.45;
    if (*q < 0.1)
        *q = 0.1;
}

static void norm(AhBiquad *bq, double b0, double b1, double b2, double a0, double a1, double a2)
{
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

/* RBJ Audio EQ Cookbook forms. */
void ah_bq_peaking(AhBiquad *bq, double freq, double q, double gain_db, uint32_t rate)
{
    if (fabs(gain_db) < 0.001) {
        ah_bq_unity(bq);
        return;
    }
    prep(&freq, &q, &rate);
    const double A = pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * M_PI * freq / rate;
    const double al = sin(w0) / (2.0 * q);
    const double c = cos(w0);
    norm(bq, 1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
}

void ah_bq_lowshelf(AhBiquad *bq, double freq, double q, double gain_db, uint32_t rate)
{
    if (fabs(gain_db) < 0.001) {
        ah_bq_unity(bq);
        return;
    }
    prep(&freq, &q, &rate);
    const double A = pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * M_PI * freq / rate;
    const double c = cos(w0);
    const double k = 2.0 * sqrt(A) * sin(w0) / (2.0 * q);
    norm(bq,
         A * ((A + 1.0) - (A - 1.0) * c + k),
         2.0 * A * ((A - 1.0) - (A + 1.0) * c),
         A * ((A + 1.0) - (A - 1.0) * c - k),
         (A + 1.0) + (A - 1.0) * c + k,
         -2.0 * ((A - 1.0) + (A + 1.0) * c),
         (A + 1.0) + (A - 1.0) * c - k);
}

void ah_bq_highshelf(AhBiquad *bq, double freq, double q, double gain_db, uint32_t rate)
{
    if (fabs(gain_db) < 0.001) {
        ah_bq_unity(bq);
        return;
    }
    prep(&freq, &q, &rate);
    const double A = pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * M_PI * freq / rate;
    const double c = cos(w0);
    const double k = 2.0 * sqrt(A) * sin(w0) / (2.0 * q);
    norm(bq,
         A * ((A + 1.0) + (A - 1.0) * c + k),
         -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
         A * ((A + 1.0) + (A - 1.0) * c - k),
         (A + 1.0) - (A - 1.0) * c + k,
         2.0 * ((A - 1.0) - (A + 1.0) * c),
         (A + 1.0) - (A - 1.0) * c - k);
}

void ah_bq_lowpass(AhBiquad *bq, double freq, double q, uint32_t rate)
{
    prep(&freq, &q, &rate);
    const double w0 = 2.0 * M_PI * freq / rate;
    const double c = cos(w0);
    const double al = sin(w0) / (2.0 * q);
    norm(bq, (1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
}

void ah_bq_highpass(AhBiquad *bq, double freq, double q, uint32_t rate)
{
    prep(&freq, &q, &rate);
    const double w0 = 2.0 * M_PI * freq / rate;
    const double c = cos(w0);
    const double al = sin(w0) / (2.0 * q);
    norm(bq, (1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
}

double ah_bq_mag_db(const AhBiquad *bq, double freq, uint32_t rate)
{
    const double w = 2.0 * M_PI * freq / (rate ? rate : 48000);
    const double c1 = cos(w), s1 = sin(w), c2 = cos(2.0 * w), s2 = sin(2.0 * w);
    const double nr = bq->b0 + bq->b1 * c1 + bq->b2 * c2;
    const double ni = -(bq->b1 * s1 + bq->b2 * s2);
    const double dr = 1.0 + bq->a1 * c1 + bq->a2 * c2;
    const double di = -(bq->a1 * s1 + bq->a2 * s2);
    return 10.0 * log10((nr * nr + ni * ni + 1e-300) / (dr * dr + di * di + 1e-300));
}

/* Gaussian elimination with partial pivoting; answer lands in b. */
static void solve(double *A, double *b, int n)
{
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c + 1; r < n; ++r)
            if (fabs(A[r * n + c]) > fabs(A[p * n + c]))
                p = r;
        if (p != c) {
            for (int k = 0; k < n; ++k) {
                double t = A[c * n + k];
                A[c * n + k] = A[p * n + k];
                A[p * n + k] = t;
            }
            double t = b[c];
            b[c] = b[p];
            b[p] = t;
        }
        const double d = A[c * n + c];
        if (fabs(d) < 1e-12)
            continue;
        for (int r = c + 1; r < n; ++r) {
            const double f = A[r * n + c] / d;
            for (int k = c; k < n; ++k)
                A[r * n + k] -= f * A[c * n + k];
            b[r] -= f * b[c];
        }
    }
    for (int c = n - 1; c >= 0; --c) {
        double s = b[c];
        for (int k = c + 1; k < n; ++k)
            s -= A[c * n + k] * b[k];
        b[c] = fabs(A[c * n + c]) > 1e-12 ? s / A[c * n + c] : 0.0;
    }
}

void ah_geq_design(const AhEqBand *bands, int n, uint32_t rate, AhBiquad *out)
{
    enum { N = 32, P = 2 * N };
    if (!bands || !out || n < 1)
        return;
    if (n > N)
        n = N;
    if (rate < 1)
        rate = 48000;

    double f[N], q[N], g[N], pf[P], t[P], err[P], B[P][N], A[N * N], y[N];
    bool flat = true;
    for (int i = 0; i < n; ++i) {
        f[i] = bands[i].freq_hz;
        q[i] = bands[i].q > 0.05f ? bands[i].q : 1.0;
        g[i] = 0.0;
        if (fabsf(bands[i].gain_db) > 0.01f)
            flat = false;
    }
    if (flat) {
        for (int i = 0; i < n; ++i)
            ah_bq_unity(&out[i]);
        return;
    }

    /* Targets: band centers plus the straight line the UI draws between them. */
    int np = 0;
    for (int i = 0; i < n; ++i) {
        if (f[i] < rate * 0.47) {
            pf[np] = f[i];
            t[np++] = bands[i].gain_db;
        }
        if (i + 1 < n && sqrt(f[i] * f[i + 1]) < rate * 0.47) {
            pf[np] = sqrt(f[i] * f[i + 1]);
            t[np++] = 0.5 * (bands[i].gain_db + bands[i + 1].gain_db);
        }
    }

    /* Interaction matrix: dB response of each band per dB of its gain. */
    for (int m = 0; m < n; ++m) {
        AhBiquad ref;
        ah_bq_peaking(&ref, f[m], q[m], 12.0, rate);
        for (int p = 0; p < np; ++p)
            B[p][m] = ah_bq_mag_db(&ref, pf[p], rate) / 12.0;
    }

    memcpy(err, t, sizeof(double) * (size_t)np);
    for (int it = 0; it < 3; ++it) {
        for (int i = 0; i < n; ++i) {
            y[i] = 0.0;
            for (int p = 0; p < np; ++p)
                y[i] += B[p][i] * err[p];
            for (int j = 0; j < n; ++j) {
                double s = (i == j) ? 1e-3 : 0.0;
                for (int p = 0; p < np; ++p)
                    s += B[p][i] * B[p][j];
                A[i * n + j] = s;
            }
        }
        solve(A, y, n);
        for (int i = 0; i < n; ++i) {
            g[i] += y[i];
            if (g[i] > 24.0)
                g[i] = 24.0;
            if (g[i] < -24.0)
                g[i] = -24.0;
            ah_bq_peaking(&out[i], f[i], q[i], g[i], rate);
        }
        for (int p = 0; p < np; ++p) {
            double db = 0.0;
            for (int m = 0; m < n; ++m)
                db += ah_bq_mag_db(&out[m], pf[p], rate);
            err[p] = t[p] - db;
        }
    }
}

void ah_limiter_init(AhLimiter *m, uint32_t rate, float ceil_db, float look_ms,
                     float release_ms, bool true_peak)
{
    if (!rate)
        rate = 48000;
    memset(m, 0, sizeof(*m));
    m->ceil = powf(10.f, ceil_db / 20.f);
    m->true_peak = true_peak;
    m->look = (int)(look_ms * 0.001f * (float)rate + 0.5f);
    if (m->look < 1)
        m->look = 1;
    if (m->look > AH_LIM_MAX)
        m->look = AH_LIM_MAX;
    m->rel = 1.f - expf(-1.f / (release_ms * 0.001f * (float)rate));
    m->hold = 1.f;
    for (int i = 0; i < m->look; ++i)
        m->gbuf[i] = 1.f;
    m->sum = m->look;
}

/*
 * Gain = moving average (length L) of a min-hold (length L) of the needed gain,
 * audio delayed by L. Every box tap that covers a peak is at or below its need,
 * so the peak lands under the ceiling with a smooth L-sample attack and no
 * waveshaping. Flat input below the ceiling passes bit-exact.
 */
void ah_limiter_tick(AhLimiter *m, float *left, float *right)
{
    const float xl = *left, xr = *right;
    float pk = fmaxf(fabsf(xl), fabsf(xr));
    if (m->true_peak) {
        /* 4-tap half-sample estimate between the two previous samples. */
        for (int c = 0; c < 2; ++c) {
            const float *h = m->hist[c];
            const float x = c ? xr : xl;
            const float mid = (-h[0] + 9.f * h[1] + 9.f * h[2] - x) * (1.f / 16.f);
            pk = fmaxf(pk, fabsf(mid));
        }
        for (int c = 0; c < 2; ++c) {
            m->hist[c][0] = m->hist[c][1];
            m->hist[c][1] = m->hist[c][2];
            m->hist[c][2] = c ? xr : xl;
        }
    }

    const float need = pk > m->ceil ? m->ceil / pk : 1.f;
    if (need < m->hold) {
        m->hold = need;
        m->hold_n = m->look;
    } else if (m->hold_n > 0) {
        m->hold_n--;
    } else if (m->hold < 1.f) {
        m->hold += (1.f - m->hold) * m->rel;
        if (m->hold > 0.999999f)
            m->hold = 1.f;
    }

    const int i = m->pos;
    m->sum += (double)m->hold - (double)m->gbuf[i];
    m->gbuf[i] = m->hold;
    const float g = (float)(m->sum / m->look);

    float ol = m->dl[0][i] * g, or_ = m->dl[1][i] * g;
    m->dl[0][i] = xl;
    m->dl[1][i] = xr;
    m->pos = (i + 1) % m->look;

    if (ol > m->ceil)
        ol = m->ceil;
    if (ol < -m->ceil)
        ol = -m->ceil;
    if (or_ > m->ceil)
        or_ = m->ceil;
    if (or_ < -m->ceil)
        or_ = -m->ceil;
    *left = ol;
    *right = or_;
}

void ah_dsp_no_denormals(void)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040); /* FTZ | DAZ */
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
#endif
}
