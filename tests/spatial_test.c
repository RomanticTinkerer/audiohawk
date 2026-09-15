#include "audiohawk/spatial.h"
#include "audiohawk/effects.h"
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
    for (int i = 0; i < 10000; ++i) {
        float l = i * 0.01f, r = -l * 0.5f;
        ah_spatial_limit(&l, &r);
        CHECK(fabsf(l) <= 1.f && fabsf(r) <= 1.f);
        CHECK(fabsf(r + l * 0.5f) < 1e-6f);
    }
    /* Maximum controls and a rate change must remain bounded after limiting. */
    AhSpatialParams p;
    ah_spatial_configure(&p, 48000, true, 100, 100, 100);
    for (int i = 0; i < 96000; ++i) {
        if (i == 48000) ah_spatial_configure(&p, 96000, true, 100, 100, 100);
        float l = 4.f * sinf(i * 0.013f), r = 3.f * cosf(i * 0.057f);
        ah_spatial_process(&a, &p, &l, &r);
        ah_spatial_limit(&l, &r);
        CHECK(isfinite(l) && isfinite(r) && fabsf(l) <= 1.f && fabsf(r) <= 1.f);
    }
    memset(&a, 0, sizeof(a));
    for (int i = 0; i < 96000; ++i) {
        float l = 0, r = 0;
        ah_spatial_process(&a, &p, &l, &r);
        CHECK(l == 0 && r == 0);
    }
    puts("All spatial DSP and settings checks passed.");
    return 0;
}
