#include "audiohawk/spatial.h"
#include "audiohawk/dsp.h"
#include "audiohawk/effects.h"
#include "audiohawk/pitch.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define test_pid _getpid
#define test_mkdir(p) _mkdir(p)
#define test_rmdir _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#define test_pid getpid
#define test_mkdir(p) mkdir(p, 0700)
#define test_rmdir rmdir
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
/* Never open a real user's effects.conf, even when run from the repository. */
static char config_dir[128];
const char *ah_config_dir(void) { return config_dir; }
static AhSpatialState a, b;

static double response(unsigned rate, float hz, int bass, int treble, int amount, int polarity)
{
    AhSpatialParams p;
    ah_spatial_configure(&p, rate, true, amount, bass, treble);
    memset(&a, 0, sizeof(a));
    double input = 0, output = 0;
    for (unsigned i = 0; i < rate; ++i) {
        float x = 0.1f * sinf(6.28318530718f * hz * i / rate);
        float l = x, r = polarity * x;
        ah_spatial_process(&a, &p, &l, &r);
        CHECK(isfinite(l) && isfinite(r));
        if (polarity == 1) CHECK(fabsf(l - r) < 1e-6f);
        if (i > rate / 2) { input += 2 * x * x; output += l*l + r*r; }
    }
    return sqrt(output / input);
}

int main(void)
{
    AhEffectsState fx, loaded;
    snprintf(config_dir, sizeof(config_dir), "audiohawk-test-%ld", (long)test_pid());
    CHECK(test_mkdir(config_dir) == 0);
    char config_path[160];
    snprintf(config_path, sizeof(config_path), "%s/effects.conf", config_dir);
    ah_effects_defaults(&fx);
    CHECK(fx.surround_bass == 25 && fx.surround_treble == 25 && fx.surround_amount == 50);
    FILE *f = fopen(config_path, "w"); CHECK(f);
    fputs("surround_virtualizer=true\n", f); fclose(f);
    CHECK(ah_effects_load(&loaded) == 0 && loaded.surround_bass == 25);
    fx.surround_virtualizer = true;
    fx.surround_amount = 83; fx.surround_bass = -5; fx.surround_treble = 500;
    CHECK(ah_effects_save(&fx) == 0);
    CHECK(ah_effects_load(&loaded) == 0);
    CHECK(loaded.surround_virtualizer && loaded.surround_amount == 85);
    CHECK(loaded.surround_bass == 0 && loaded.surround_treble == 100);
    fx.pitch = 40;
    CHECK(ah_effects_save(&fx) == 0);
    CHECK(ah_effects_load(&loaded) == 0);
    CHECK(loaded.pitch == 15);
    CHECK(remove(config_path) == 0);
    CHECK(test_rmdir(config_dir) == 0);

    unsigned rates[] = {44100, 48000, 96000, 192000, 384000};
    for (unsigned n = 0; n < sizeof(rates)/sizeof(rates[0]); ++n) {
        unsigned rate = rates[n];
        double bass = response(rate, 25, 25, 0, 0, 1);
        double treble = response(rate, rate * 0.4f, 0, 25, 0, 1);
        CHECK(fabs(bass - 1.25) < 0.005);
        CHECK(fabs(treble - 1.25) < 0.005);
        CHECK(response(rate, 40, 0, 0, 100, -1) > 0.98);
        CHECK(fabs(response(rate, 1000, 0, 0, 0, 1) - 1) < 1e-6);
        AhSpatialParams p;
        ah_spatial_configure(&p, rate, true, 100, 25, 25);
        CHECK(abs(p.room_delay - (int)(rate * 0.012)) <= 1);
        memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
        double opposite = 0;
        for (unsigned i = 0; i < rate; ++i) {
            float l = i == rate/2 ? 0.5f : 0, r = 0;
            float rl = r, rr = l;
            ah_spatial_process(&a, &p, &l, &r);
            ah_spatial_process(&b, &p, &rl, &rr);
            CHECK(fabsf(l - rr) < 1e-6f && fabsf(r - rl) < 1e-6f);
            opposite += r*r;
        }
        CHECK(opposite > 0.00001);
        p.enabled = false;
        for (unsigned i = 0; i < rate; ++i) {
            float l = 0.1f, r = -0.05f;
            ah_spatial_process(&a, &p, &l, &r);
            if (i == rate-1) CHECK(l == 0.1f && r == -0.05f);
        }
        printf("%u Hz: bass %.4fx, treble %.4fx; symmetry, bass retention, bypass OK\n", rate, bass, treble);
    }
    static AhLimiter lim;
    ah_limiter_init(&lim, 48000, 0.f, 1.5f, 60.f, false);
    for (int i = 0; i < 10000; ++i) {
        float x = 0.9f * sinf(i * 0.05f), l = x, r = -0.5f * x;
        ah_limiter_tick(&lim, &l, &r);
        if (i >= lim.look) /* below the ceiling: bit-exact, just delayed */
            CHECK(l == 0.9f * sinf((i - lim.look) * 0.05f) && r == -0.5f * l);
    }
    for (int i = 0; i < 10000; ++i) {
        float l = i * 0.01f, r = -l * 0.5f;
        ah_limiter_tick(&lim, &l, &r);
        CHECK(fabsf(l) <= 1.f && fabsf(r) <= 1.f);
        CHECK(fabsf(r + l * 0.5f) < 1e-6f);
    }
    /* Maximum controls and a rate change must remain bounded after limiting. */
    AhSpatialParams p;
    ah_spatial_configure(&p, 48000, true, 100, 100, 100);
    float worst_step = 0.f, last = 0.f;
    for (int i = 0; i < 96000; ++i) {
        if (i == 48000) ah_spatial_configure(&p, 96000, true, 100, 100, 100);
        float l = 4.f * sinf(i * 0.013f), r = 3.f * cosf(i * 0.057f);
        ah_spatial_process(&a, &p, &l, &r);
        ah_limiter_tick(&lim, &l, &r);
        CHECK(isfinite(l) && isfinite(r) && fabsf(l) <= 1.f && fabsf(r) <= 1.f);
        if (i > 1000 && i < 48000 && fabsf(l - last) > worst_step) worst_step = fabsf(l - last);
        last = l;
    }
    /* Smooth gain, no clipping corners: steps stay near the sine's own slope. */
    CHECK(worst_step < 0.1f);

    /* Graphic EQ lands on the drawn curve despite band overlap. */
    {
        static const float fq[10] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        static const float shapes[3][10] = {
            {6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
            {6, -6, 6, -6, 6, -6, 6, -6, 6, -6},
            {12, 9, 4, 0, -3, -3, 0, 4, 9, 12},
        };
        for (int s = 0; s < 3; ++s) {
            AhEqBand bands[10];
            AhBiquad bq[10];
            for (int i = 0; i < 10; ++i) {
                bands[i].freq_hz = fq[i];
                bands[i].q = 0.667f;
                bands[i].gain_db = shapes[s][i];
            }
            ah_geq_design(bands, 10, 48000, bq);
            double worst = 0.0;
            for (int i = 0; i < 10; ++i) {
                double db = 0.0;
                for (int m = 0; m < 10; ++m)
                    db += ah_bq_mag_db(&bq[m], fq[i], 48000);
                if (fabs(db - shapes[s][i]) > worst)
                    worst = fabs(db - shapes[s][i]);
            }
            printf("geq shape %d: worst band error %.2f dB\n", s, worst);
            CHECK(worst < (s == 1 ? 1.5 : 0.5));
        }
    }
    memset(&a, 0, sizeof(a));
    for (int i = 0; i < 96000; ++i) {
        float l = 0, r = 0;
        ah_spatial_process(&a, &p, &l, &r);
        CHECK(l == 0 && r == 0);
    }
    CHECK(ah_pitch_clamp(40) == 15 && ah_pitch_clamp(-40) == -15);
    {
        AhPitchState ps;
        float l = 0.3f, r = -0.1f;
        ah_pitch_reset(&ps);
        ah_pitch_process(&ps, 0, &l, &r);
        CHECK(l == 0.3f && r == -0.1f);
    }
    {
        const unsigned rate = 48000;
        const float hz = 440.f;
        int zc0 = 0, zc12 = 0;
        float prev0 = 0, prev12 = 0;
        AhPitchState p0, p12;
        ah_pitch_reset(&p0);
        ah_pitch_reset(&p12);
        unsigned skip = AH_PITCH_BUF * 2;
        for (unsigned i = 0; i < rate + skip; ++i) {
            float x = sinf(6.28318530718f * hz * i / rate);
            float a = x, b = x, c = x, d = x;
            ah_pitch_process(&p0, 0, &a, &b);
            ah_pitch_process(&p12, 12, &c, &d);
            CHECK(isfinite(c) && isfinite(d));
            if (i > skip) {
                if (prev0 < 0 && a >= 0)
                    zc0++;
                if (prev12 < 0 && c >= 0)
                    zc12++;
            }
            prev0 = a;
            prev12 = c;
        }
        CHECK(zc0 > 400 && zc0 < 480);
        CHECK(zc12 > zc0);
    }
    {
        /* No clicks: shifting, changing, and switching off stay near the sine's slope. */
        AhPitchState ps;
        ah_pitch_reset(&ps);
        float jump = 0.f, prev = 0.f;
        for (int i = 0; i < 192000; ++i) {
            const int st = i < 96000 ? 3 : (i < 144000 ? -5 : 0);
            float l = 0.5f * sinf(6.28318530718f * 440.f * i / 48000.f), r = l;
            ah_pitch_process(&ps, st, &l, &r);
            if (i > 0 && fabsf(l - prev) > jump)
                jump = fabsf(l - prev);
            prev = l;
        }
        printf("pitch: biggest sample step %.4f (clean sine 0.0288)\n", jump);
        CHECK(jump < 0.06f);
    }
    puts("All spatial DSP and settings checks passed.");
    return 0;
}
