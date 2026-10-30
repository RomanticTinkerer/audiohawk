#include "audiohawk/advanced.h"

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

static char config_dir[128];
const char *ah_config_dir(void) { return config_dir; }

static float peak_of(const float *l, const float *r, int n)
{
    float p = 0.f;
    for (int i = 0; i < n; ++i) {
        float a = fabsf(l[i]) > fabsf(r[i]) ? fabsf(l[i]) : fabsf(r[i]);
        if (a > p)
            p = a;
    }
    return p;
}

static void tone(float *l, float *r, int n, unsigned rate, float hz, float amp)
{
    for (int i = 0; i < n; ++i) {
        float x = amp * sinf(6.28318530718f * hz * (float)i / (float)rate);
        l[i] = x;
        r[i] = x;
    }
}

int main(void)
{
    snprintf(config_dir, sizeof(config_dir), "audiohawk-adv-%ld", (long)test_pid());
    CHECK(test_mkdir(config_dir) == 0);

    AhAdvancedState s, loaded;
    ah_advanced_defaults(&s);
    CHECK(s.amount[AH_ADV_DC] == 100);
    CHECK(!ah_advanced_any_active(&s));
    s.on[AH_ADV_COMPRESSOR] = true;
    s.amount[AH_ADV_COMPRESSOR] = 83;
    CHECK(ah_advanced_any_active(&s));
    CHECK(ah_advanced_save(&s) == 0);
    CHECK(ah_advanced_load(&loaded) == 0);
    CHECK(loaded.on[AH_ADV_COMPRESSOR]);
    CHECK(loaded.amount[AH_ADV_COMPRESSOR] == 85);

    ah_advanced_apply_preset(&s, 1);
    CHECK(!ah_advanced_any_active(&s));
    ah_advanced_apply_preset(&s, 2);
    CHECK(s.on[AH_ADV_DYN_BASS] && s.on[AH_ADV_LIMITER]);

    AhAdvancedEngine *eng = ah_advanced_create();
    CHECK(eng);

    const unsigned rate = 48000;
    const int n = 4800;
    float *l = calloc((size_t)n, sizeof(float));
    float *r = calloc((size_t)n, sizeof(float));
    CHECK(l && r);

    /* DC removal knocks a standing offset down. */
    ah_advanced_defaults(&s);
    s.on[AH_ADV_DC] = true;
    for (int i = 0; i < n; ++i) {
        l[i] = 0.25f;
        r[i] = 0.25f;
    }
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    float dc = 0.f;
    for (int i = n / 2; i < n; ++i)
        dc += l[i] + r[i];
    dc /= (float)n;
    CHECK(fabsf(dc) < 0.05f);

    /* Limiter keeps a hot tone under 1.0. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_LIMITER] = true;
    s.amount[AH_ADV_LIMITER] = 80;
    tone(l, r, n, rate, 400.f, 1.4f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(peak_of(l, r, n) < 1.0f);

    /* A/B bypass leaves the dry signal. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_DISTORT] = true;
    s.amount[AH_ADV_DISTORT] = 100;
    s.ab_bypass = true;
    tone(l, r, n, rate, 800.f, 0.4f);
    float dry0 = l[100];
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(fabsf(l[100] - dry0) < 1e-6f);

    /* Stereo width makes L and R differ on a sided signal. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_STEREO_IMAGE] = true;
    s.amount[AH_ADV_STEREO_IMAGE] = 100;
    for (int i = 0; i < n; ++i) {
        float x = 0.3f * sinf(6.28318530718f * 700.f * (float)i / (float)rate);
        l[i] = x;
        r[i] = -x;
    }
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(fabsf(l[200] - r[200]) > 0.2f);

    /* Soft-knee compressor pulls a hot tone down. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_COMPRESSOR] = true;
    s.amount[AH_ADV_COMPRESSOR] = 90;
    tone(l, r, n, rate, 440.f, 0.85f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(peak_of(l + n / 2, r + n / 2, n / 2) < 0.80f);

    /* Bauer crossfeed keeps a sided signal finite and less isolated. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_CROSSFEED] = true;
    s.amount[AH_ADV_CROSSFEED] = 100;
    for (int i = 0; i < n; ++i) {
        float x = 0.4f * sinf(6.28318530718f * 400.f * (float)i / (float)rate);
        l[i] = x;
        r[i] = 0.f;
    }
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(l[300] == l[300] && r[300] == r[300]);
    CHECK(fabsf(l[300]) < 2.f && fabsf(r[300]) < 2.f);
    CHECK(fabsf(r[300]) > 0.02f);

    /* K-weighted loudness does not explode a quiet tone. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_LOUDNESS] = true;
    s.amount[AH_ADV_LOUDNESS] = 70;
    tone(l, r, n, rate, 1000.f, 0.05f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(peak_of(l, r, n) < 0.5f);
    CHECK(l[100] == l[100] && r[100] == r[100]);

    AhAdvancedMeters m;
    ah_advanced_meters(eng, &m);
    CHECK(m.peak >= 0.f);

    ah_advanced_destroy(eng);
    free(l);
    free(r);
    char path[160];
    snprintf(path, sizeof(path), "%s/advanced.conf", config_dir);
    CHECK(remove(path) == 0);
    CHECK(test_rmdir(config_dir) == 0);
    puts("All advanced DSP checks passed.");
    return 0;
}
