#include "audiohawk/advanced.h"
#include "audiohawk/effects.h"
#include "audiohawk/profiles.h"

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

/* Music-like test signal: bass, mids, sibilance, stereo noise, clicks and a hiss floor. */
#define SW_RATE 48000
#define SW_N (SW_RATE * 3)
#define SW_SKIP (SW_RATE / 4)

static float sw_x[2][SW_N], sw_a[2][SW_N], sw_b[2][SW_N];

static float frand(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return ((float)(*s >> 8) / (float)(1 << 24)) * 2.f - 1.f;
}

static void make_signal(void)
{
    unsigned s = 1234;
    float pl = 0.f, pr = 0.f, pc = 0.f;
    for (int i = 0; i < SW_N; ++i) {
        const float t = (float)i / SW_RATE;
        /* Loud and quiet halves, then half a second of nothing but hiss. */
        const float lvl = i / (SW_RATE / 2) == 4 ? 0.f : (i / (SW_RATE / 2)) % 2 ? 0.25f : 1.0f;
        pc += 0.02f * (frand(&s) - pc);
        pl += 0.2f * (frand(&s) - pl);
        pr += 0.2f * (frand(&s) - pr);
        const float c = 0.20f * sinf(6.2831853f * 60.f * t) + 0.08f * sinf(6.2831853f * 1000.f * t) +
                        0.05f * sinf(6.2831853f * 3200.f * t) +
                        0.05f * sinf(6.2831853f * 6500.f * t) + 2.0f * pc;
        const float click = lvl > 0.f && (i % (SW_RATE / 4)) < 24 ? 0.5f : 0.f;
        sw_x[0][i] = lvl * (c + 0.12f * pl + 0.05f * frand(&s)) + click + 0.004f * frand(&s);
        sw_x[1][i] = lvl * (c + 0.12f * pr + 0.05f * frand(&s)) + click + 0.004f * frand(&s);
    }
}

static void sweep_run(const AhAdvancedState *s, float (*out)[SW_N])
{
    AhAdvancedEngine *e = ah_advanced_create();
    CHECK(e);
    ah_advanced_apply(e, s, SW_RATE);
    memcpy(out, sw_x, sizeof(sw_x));
    for (int i = 0; i < SW_N; i += 512)
        ah_advanced_process_block(e, out[0] + i, out[1] + i,
                                  (uint32_t)(i + 512 <= SW_N ? 512 : SW_N - i), SW_RATE);
    ah_advanced_destroy(e);
    for (int i = 0; i < SW_N; ++i) {
        CHECK(out[0][i] == out[0][i] && out[1][i] == out[1][i]);
        CHECK(fabsf(out[0][i]) < 16.f && fabsf(out[1][i]) < 16.f);
    }
}

/* Difference between two runs, in dB relative to the input. */
static double sweep_diff(float (*a)[SW_N], float (*b)[SW_N])
{
    double d = 0.0, x = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = SW_SKIP; i < SW_N; ++i) {
            d += (double)(a[ch][i] - b[ch][i]) * (double)(a[ch][i] - b[ch][i]);
            x += (double)sw_x[ch][i] * (double)sw_x[ch][i];
        }
    }
    return 10.0 * log10(d / x + 1e-12);
}

/* Every control of every effect has to change the sound when it goes from its
 * minimum to its maximum. This is the guard against sliders that do nothing.
 * Some controls only matter once a neighbour is off its neutral default (the
 * frequency of an EQ band with 0 dB gain), so those get a second and third
 * try with the other controls at their maximum, then minimum. */
static void check_every_control(bool verbose)
{
    make_signal();
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot) {
        const int count = ah_advanced_param_count(slot);
        CHECK(count >= 1 && count <= AH_ADV_PARAMS);
        CHECK(ah_advanced_param(slot, count) == NULL);
        for (int i = 0; i < count; ++i) {
            const AhAdvParam *p = ah_advanced_param(slot, i);
            CHECK(p && p->key && p->label && p->unit);
            CHECK(p->min < p->max && p->step > 0.f);
            CHECK(p->def >= p->min && p->def <= p->max);
            for (int j = 0; j < i; ++j)
                CHECK(strcmp(p->key, ah_advanced_param(slot, j)->key) != 0);

            double best = -200.0;
            for (int others = 0; others < 3 && best < -60.0; ++others) {
                AhAdvancedState s;
                ah_advanced_defaults(&s);
                s.on[slot] = true;
                for (int j = 0; j < count && others > 0; ++j) {
                    const AhAdvParam *q = ah_advanced_param(slot, j);
                    s.param[slot][j] = others == 1 ? q->max : q->min;
                }
                s.param[slot][i] = p->min;
                sweep_run(&s, sw_a);
                s.param[slot][i] = p->max;
                sweep_run(&s, sw_b);
                const double d = sweep_diff(sw_a, sw_b);
                if (d > best)
                    best = d;
            }
            if (verbose)
                printf("%-36s %-20s %7.1f dB\n", ah_advanced_slot_title(slot), p->label, best);
            if (best < -60.0) {
                fprintf(stderr, "FAIL: %s / %s does nothing (%.1f dB)\n",
                        ah_advanced_slot_title(slot), p->label, best);
                exit(1);
            }
        }
    }
}

int main(int argc, char **argv)
{
    snprintf(config_dir, sizeof(config_dir), "audiohawk-adv-%ld", (long)test_pid());
    CHECK(test_mkdir(config_dir) == 0);
    char path[160];
    snprintf(path, sizeof(path), "%s/advanced.conf", config_dir);

    AhAdvancedState s, loaded;
    ah_advanced_defaults(&s);
    CHECK(s.param[AH_ADV_DC][0] == 10.f);
    CHECK(!ah_advanced_any_active(&s));
    s.on[AH_ADV_COMPRESSOR] = true;
    s.param[AH_ADV_COMPRESSOR][0] = -23.5f; /* threshold */
    s.param[AH_ADV_COMPRESSOR][1] = 99.f;   /* ratio, over the limit */
    CHECK(ah_advanced_any_active(&s));
    CHECK(ah_advanced_save(&s) == 0);
    CHECK(ah_advanced_load(&loaded) == 0);
    CHECK(loaded.on[AH_ADV_COMPRESSOR]);
    CHECK(loaded.param[AH_ADV_COMPRESSOR][0] == -23.5f);
    CHECK(loaded.param[AH_ADV_COMPRESSOR][1] == 20.f);

    /* A 0.3.5b file: on/off survives, the old amounts fall back to defaults, and
     * a mono checker that was on but not folding stays that way. */
    FILE *old = fopen(path, "w");
    CHECK(old);
    fputs("on.mono_check=true\namt.mono_check=50\non.hall=true\namt.hall=85\n", old);
    fclose(old);
    CHECK(ah_advanced_load(&loaded) == 0);
    CHECK(loaded.on[AH_ADV_MONO] && loaded.param[AH_ADV_MONO][0] == 0.f);
    CHECK(loaded.on[AH_ADV_HALL] && loaded.param[AH_ADV_HALL][1] == 1.8f);

    ah_advanced_apply_preset(&s, 1);
    CHECK(!ah_advanced_any_active(&s));
    ah_advanced_apply_preset(&s, 2);
    CHECK(s.on[AH_ADV_DYN_BASS] && s.on[AH_ADV_LIMITER]);
    CHECK(s.param[AH_ADV_COMPRESSOR][1] == 2.f);

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
    s.param[AH_ADV_DC][0] = 30.f;
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

    /* Limiter holds a hot tone under its ceiling, even with input gain on top. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_LIMITER] = true;
    s.param[AH_ADV_LIMITER][0] = 6.f;  /* input gain */
    s.param[AH_ADV_LIMITER][1] = -3.f; /* ceiling, 0.708 */
    tone(l, r, n, rate, 400.f, 1.4f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(peak_of(l, r, n) < 0.72f);
    CHECK(peak_of(l + n / 2, r + n / 2, n / 2) > 0.5f);

    /* A/B bypass leaves the dry signal. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_DISTORT] = true;
    s.param[AH_ADV_DISTORT][0] = 36.f;
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
    s.param[AH_ADV_STEREO_IMAGE][0] = 200.f;
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
    s.param[AH_ADV_COMPRESSOR][0] = -30.f; /* threshold */
    s.param[AH_ADV_COMPRESSOR][1] = 8.f;   /* ratio */
    s.param[AH_ADV_COMPRESSOR][5] = 0.f;   /* makeup */
    tone(l, r, n, rate, 440.f, 0.85f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(peak_of(l + n / 2, r + n / 2, n / 2) < 0.40f);

    /* Bauer crossfeed keeps a sided signal finite and less isolated. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_CROSSFEED] = true;
    for (int i = 0; i < n; ++i) {
        float x = 0.4f * sinf(6.28318530718f * 400.f * (float)i / (float)rate);
        l[i] = x;
        r[i] = 0.f;
    }
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(l[300] == l[300] && r[300] == r[300]);
    CHECK(fabsf(l[300]) < 2.f && fabsf(r[300]) < 2.f);
    CHECK(peak_of(r + n / 2, r + n / 2, n / 2) > 0.02f);

    /* K-weighted loudness does not explode a quiet tone. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_LOUDNESS] = true;
    tone(l, r, n, rate, 1000.f, 0.05f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(peak_of(l, r, n) < 0.5f);
    CHECK(l[100] == l[100] && r[100] == r[100]);

    AhAdvancedMeters m;
    ah_advanced_meters(eng, &m);
    CHECK(m.peak >= 0.f);

    /* Room reverb: each channel walks its own delay line, so the first echo lands at 29 ms. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_ROOM] = true;
    memset(l, 0, sizeof(float) * (size_t)n);
    memset(r, 0, sizeof(float) * (size_t)n);
    l[0] = 1.f;
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    int first = -1;
    for (int i = 1; i < n && first < 0; ++i)
        if (fabsf(l[i]) > 1e-6f)
            first = i;
    CHECK(first == (29 * (int)rate) / 1000);

    /* De-esser turns a loud 7 kHz tone down and leaves a 300 Hz one alone. */
    for (int pass = 0; pass < 2; ++pass) {
        ah_advanced_reset(eng);
        ah_advanced_defaults(&s);
        s.on[AH_ADV_DEESSER] = true;
        s.param[AH_ADV_DEESSER][2] = 12.f; /* max reduction */
        tone(l, r, n, rate, pass ? 300.f : 7000.f, 0.5f);
        ah_advanced_apply(eng, &s, rate);
        ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
        const float pk = peak_of(l + n / 2, r + n / 2, n / 2);
        CHECK(pass ? pk > 0.48f : pk < 0.30f);
    }

    /* Spectral gate: a tone under the threshold drops by the range, one over it passes. */
    for (int pass = 0; pass < 2; ++pass) {
        ah_advanced_reset(eng);
        ah_advanced_defaults(&s);
        s.on[AH_ADV_SPEC_GATE] = true;
        s.param[AH_ADV_SPEC_GATE][0] = -40.f; /* threshold */
        tone(l, r, n, rate, 1000.f, pass ? 0.3f : 0.002f);
        ah_advanced_apply(eng, &s, rate);
        ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
        const float pk = peak_of(l + n / 2, r + n / 2, n / 2);
        CHECK(pass ? pk > 0.28f : pk < 0.0005f);
    }

    /* Saturation keeps the wave's polarity: the negative half stays negative. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_DISTORT] = true;
    tone(l, r, n, rate, 200.f, 0.5f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    float lowest = 0.f, highest = 0.f;
    for (int i = n / 2; i < n; ++i) {
        lowest = fminf(lowest, l[i]);
        highest = fmaxf(highest, l[i]);
    }
    CHECK(highest > 0.2f && fabsf(lowest + highest) < 0.02f);

    /* Mono checker at 0 % and widening at 100 % are true bypasses. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_MONO] = true;
    s.param[AH_ADV_MONO][0] = 0.f;
    s.on[AH_ADV_WIDEN] = true;
    s.param[AH_ADV_WIDEN][0] = 100.f;
    tone(l, r, n, rate, 440.f, 0.3f);
    for (int i = 0; i < n; ++i)
        r[i] *= -0.5f;
    dry0 = r[777];
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    CHECK(fabsf(r[777] - dry0) < 1e-6f);

    /* Identical L/R reads full mono compatibility. */
    ah_advanced_reset(eng);
    ah_advanced_defaults(&s);
    s.on[AH_ADV_DC] = true;
    tone(l, r, n, rate, 440.f, 0.3f);
    ah_advanced_apply(eng, &s, rate);
    ah_advanced_process_block(eng, l, r, (uint32_t)n, rate);
    ah_advanced_meters(eng, &m);
    CHECK(m.corr > 0.98f);

    ah_advanced_destroy(eng);
    free(l);
    free(r);

    /* Profile definitions check */
    const AhProfileDef *defs = ah_profile_defs();
    for (int p = 0; p < AH_PROFILE_COUNT; ++p) {
        const AhProfileDef *prof = &defs[p];
        CHECK(prof->id == (AhProfileId)p);

        /* Copy its fx, run ah_effects_clamp, assert unchanged (memcmp) */
        AhEffectsState fx_copy = prof->fx;
        ah_effects_clamp(&fx_copy);
        CHECK(memcmp(&fx_copy, &prof->fx, sizeof(AhEffectsState)) == 0);

        /* Start from ah_advanced_defaults, ah_advanced_apply_set with its adv, copy, ah_advanced_clamp, assert unchanged */
        AhAdvancedState adv_st;
        ah_advanced_defaults(&adv_st);
        ah_advanced_apply_set(&adv_st, prof->adv);
        AhAdvancedState adv_copy = adv_st;
        ah_advanced_clamp(&adv_copy);
        CHECK(memcmp(&adv_copy, &adv_st, sizeof(AhAdvancedState)) == 0);

        /* Assert every adv entry has slot < AH_ADV_SLOTS and param < ah_advanced_param_count(slot) */
        if (prof->adv) {
            for (const AhAdvSet *set = prof->adv; set->slot >= 0; ++set) {
                CHECK(set->slot < AH_ADV_SLOTS);
                if (set->param >= 0) {
                    CHECK(set->param < ah_advanced_param_count(set->slot));
                }
            }
        }
    }

    /* Assert apply_set keeps a pre-set app_name/n_apps */
    AhAdvancedState app_test;
    ah_advanced_defaults(&app_test);
    snprintf(app_test.app_name, sizeof(app_test.app_name), "test-app");
    app_test.n_apps = 5;
    ah_advanced_apply_set(&app_test, defs[0].adv);
    CHECK(strcmp(app_test.app_name, "test-app") == 0);
    CHECK(app_test.n_apps == 5);

    /* Pass -v to print how much each control moves the sound. */
    check_every_control(argc > 1 && strcmp(argv[1], "-v") == 0);

    CHECK(remove(path) == 0);
    CHECK(test_rmdir(config_dir) == 0);
    puts("All advanced DSP checks passed.");
    return 0;
}
