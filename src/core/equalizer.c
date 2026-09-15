#include "audiohawk/equalizer.h"
#include "audiohawk/config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ISO-ish centers for each band configuration. */
static const float k_freq_10[10] = {
    32.f, 64.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f
};
static const float k_freq_15[15] = {
    25.f, 40.f, 63.f, 100.f, 160.f, 250.f, 400.f, 630.f,
    1000.f, 1600.f, 2500.f, 4000.f, 6300.f, 10000.f, 16000.f
};
static const float k_freq_20[20] = {
    31.f, 44.f, 63.f, 88.f, 125.f, 180.f, 250.f, 355.f, 500.f, 710.f,
    1000.f, 1400.f, 2000.f, 2800.f, 4000.f, 5600.f, 8000.f, 11000.f, 14000.f, 16000.f
};

/*
 * Canonical 10-band preset gains (dB), based on common graphic-EQ charts
 * (audioutilities / consumer EQ preset tables), lightly rounded.
 */
/* Genre presets — common consumer 10-band charts, rounded to 0.5 dB. */
static const float k_preset_10[AH_EQ_PRESET_COUNT - 1][10] = {
    /* Flat */        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* Bass Boost */  { 5.5f, 4.5f, 3.0f, 1.0f, 0, 0, 0, 0.5f, 1.5f, 1.5f },
    /* Rock */        { 4.5f, 4.0f, 3.0f, 1.5f, -1.5f, -1.0f, 0.5f, 2.0f, 3.5f, 4.0f },
    /* Pop */         { -1.5f, -1.0f, 0, 1.0f, 2.5f, 3.0f, 2.5f, 1.0f, 1.5f, 1.5f },
    /* Acoustic */    { 3.5f, 3.0f, 2.0f, 1.0f, 0.5f, 1.0f, 2.0f, 2.5f, 3.0f, 2.0f },
    /* Classical */   { 3.0f, 2.5f, 2.0f, 1.5f, -1.0f, -0.5f, 1.0f, 2.0f, 3.0f, 2.5f },
    /* Electronic */  { 5.0f, 4.0f, 1.5f, 0, -1.5f, 0.5f, 0, 2.0f, 4.0f, 5.0f },
    /* Vocal */       { -3.0f, -2.0f, -1.0f, -0.5f, 0.5f, 2.0f, 3.5f, 3.5f, 2.0f, 1.5f },
    /* Treble Boost */{ 0, 0, 0, 0, 0.5f, 1.0f, 2.0f, 3.5f, 5.0f, 5.5f },
    /* Jazz */        { 3.5f, 2.5f, 1.5f, 0.5f, -2.0f, -1.0f, 0.5f, 2.0f, 3.5f, 4.5f },
};

static const AhEqPresetDef k_presets[AH_EQ_PRESET_COUNT] = {
    { AH_EQ_PRESET_FLAT, "Flat", "Neutral reference — all bands at 0 dB.", "flat" },
    { AH_EQ_PRESET_BASS_BOOST, "Bass Boost", "Extra weight in the sub and bass bands.", "bass" },
    { AH_EQ_PRESET_ROCK, "Rock", "V-shaped energy for guitars and drums.", "rock" },
    { AH_EQ_PRESET_POP, "Pop", "Forward mids with controlled extremes.", "pop" },
    { AH_EQ_PRESET_ACOUSTIC, "Acoustic", "Warm body and natural air for unplugged tracks.", "acoustic" },
    { AH_EQ_PRESET_CLASSICAL, "Classical", "Wide natural balance with gentle sparkle.", "classical" },
    { AH_EQ_PRESET_ELECTRONIC, "Electronic", "Deep lows and bright highs for synths and EDM.", "electronic" },
    { AH_EQ_PRESET_VOCAL, "Vocal Booster", "Presence focus for voices and podcasts.", "vocal" },
    { AH_EQ_PRESET_TREBLE, "Treble Boost", "Lifted brilliance and air.", "treble" },
    { AH_EQ_PRESET_JAZZ, "Jazz", "Warm lows with clear cymbals and horns.", "jazz" },
    { AH_EQ_PRESET_CUSTOM, "Custom", "Your saved curve — edits switch here automatically.", "custom" },
};

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static float interp_gain(const float *src_f, const float *src_g, int n_src, float freq)
{
    if (freq <= src_f[0])
        return src_g[0];
    if (freq >= src_f[n_src - 1])
        return src_g[n_src - 1];

    for (int i = 0; i < n_src - 1; ++i) {
        if (freq >= src_f[i] && freq <= src_f[i + 1]) {
            float a = logf(src_f[i]);
            float b = logf(src_f[i + 1]);
            float t = (logf(freq) - a) / (b - a);
            return src_g[i] + t * (src_g[i + 1] - src_g[i]);
        }
    }
    return 0.f;
}

const AhEqPresetDef *ah_eq_preset_defs(void)
{
    return k_presets;
}

const AhEqPresetDef *ah_eq_preset_by_id(AhEqPresetId id)
{
    if ((int)id < 0 || (int)id >= AH_EQ_PRESET_COUNT)
        return &k_presets[0];
    return &k_presets[id];
}

void ah_eq_fill_freqs(AhBandCount count, AhEqBand out[AH_EQ_BANDS_MAX])
{
    const float *freqs = k_freq_10;
    int n = 10;
    float q = 1.1f;

    if (count == AH_BANDS_15) {
        freqs = k_freq_15;
        n = 15;
        q = 1.4f;
    } else if (count == AH_BANDS_20) {
        freqs = k_freq_20;
        n = 20;
        q = 1.7f;
    }

    memset(out, 0, sizeof(AhEqBand) * AH_EQ_BANDS_MAX);
    for (int i = 0; i < n; ++i) {
        out[i].freq_hz = freqs[i];
        out[i].q = q;
        out[i].gain_db = 0.f;
    }
}

void ah_eq_apply_gains10(AhEqualizerState *st, const float gains10[10])
{
    if (!st || !gains10)
        return;

    ah_eq_fill_freqs(st->band_count, st->bands);
    int n = (int)st->band_count;

    if (n == 10) {
        for (int i = 0; i < 10; ++i)
            st->bands[i].gain_db = clampf(gains10[i], -12.f, 12.f);
    } else {
        const float *dst_f = (n == 15) ? k_freq_15 : k_freq_20;
        for (int i = 0; i < n; ++i)
            st->bands[i].gain_db = clampf(
                interp_gain(k_freq_10, gains10, 10, dst_f[i]), -12.f, 12.f);
    }

    /* Presets must not clear air (≥ ~7 kHz). */
    for (int i = 0; i < n; ++i) {
        if (st->bands[i].freq_hz >= 7000.f && st->bands[i].gain_db < 0.f)
            st->bands[i].gain_db = 0.f;
    }
}

AhEqPresetId ah_eq_preset_for_profile(AhProfileId profile)
{
    switch (profile) {
    case AH_PROFILE_MUSIC:  return AH_EQ_PRESET_ROCK;
    case AH_PROFILE_MOVIE:  return AH_EQ_PRESET_VOCAL;
    case AH_PROFILE_GAME:   return AH_EQ_PRESET_TREBLE;
    case AH_PROFILE_WORK:   return AH_EQ_PRESET_VOCAL;
    case AH_PROFILE_CASUAL: return AH_EQ_PRESET_ACOUSTIC;
    case AH_PROFILE_MOOD:   return AH_EQ_PRESET_ELECTRONIC;
    }
    return AH_EQ_PRESET_FLAT;
}

void ah_eq_state_defaults(AhEqualizerState *st)
{
    memset(st, 0, sizeof(*st));
    st->band_count = AH_BANDS_10;
    st->view = AH_EQ_VIEW_SLIDERS;
    st->preset = AH_EQ_PRESET_FLAT;
    st->follow_profile = true;
    ah_eq_apply_preset(st, AH_EQ_PRESET_FLAT);
}

void ah_eq_apply_preset(AhEqualizerState *st, AhEqPresetId preset)
{
    if (!st)
        return;

    st->follow_profile = false;

    if (preset == AH_EQ_PRESET_CUSTOM) {
        st->preset = AH_EQ_PRESET_CUSTOM;
        ah_eq_fill_freqs(st->band_count, st->bands);
        int n = (int)st->band_count;
        const float *src = st->custom_10;
        if (n == 15)
            src = st->custom_15;
        else if (n == 20)
            src = st->custom_20;
        for (int i = 0; i < n; ++i)
            st->bands[i].gain_db = clampf(src[i], -12.f, 12.f);
        return;
    }

    st->preset = preset;
    ah_eq_apply_gains10(st, k_preset_10[preset]);
}

void ah_eq_set_band_count(AhEqualizerState *st, AhBandCount count)
{
    if (!st)
        return;
    if (count != AH_BANDS_10 && count != AH_BANDS_15 && count != AH_BANDS_20)
        count = AH_BANDS_10;

    bool follow = st->follow_profile;
    AhEqPresetId preset = st->preset;
    st->band_count = count;

    if (follow) {
        /* Caller (core) will recompose from listening profile. */
        st->follow_profile = true;
        st->preset = AH_EQ_PRESET_CUSTOM;
        return;
    }

    ah_eq_apply_preset(st, preset);
}

void ah_eq_mark_custom(AhEqualizerState *st)
{
    if (!st)
        return;
    st->preset = AH_EQ_PRESET_CUSTOM;
    st->follow_profile = false;
    int n = (int)st->band_count;
    float *dst = st->custom_10;
    if (n == 15)
        dst = st->custom_15;
    else if (n == 20)
        dst = st->custom_20;
    for (int i = 0; i < n; ++i)
        dst[i] = st->bands[i].gain_db;
}

void ah_eq_from_profile_curve(AhEqualizerState *st, const AhEqBand src[AH_EQ_BANDS])
{
    float gains[10];
    for (int i = 0; i < 10; ++i)
        gains[i] = src[i].gain_db;
    ah_eq_apply_gains10(st, gains);
    st->follow_profile = true;
    st->preset = AH_EQ_PRESET_CUSTOM;
    /* Keep custom slots in sync so band-count changes preserve the curve. */
    int n = (int)st->band_count;
    float *dst = st->custom_10;
    if (n == 15)
        dst = st->custom_15;
    else if (n == 20)
        dst = st->custom_20;
    for (int i = 0; i < n; ++i)
        dst[i] = st->bands[i].gain_db;
}

int ah_eq_state_load(AhEqualizerState *st)
{
    ah_eq_state_defaults(st);

    char path[640];
    snprintf(path, sizeof(path), "%s/equalizer.conf", ah_config_dir());
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "band_count") == 0)
            st->band_count = (AhBandCount)atoi(val);
        else if (strcmp(key, "view") == 0)
            st->view = (AhEqViewMode)atoi(val);
        else if (strcmp(key, "preset") == 0)
            st->preset = (AhEqPresetId)atoi(val);
        else if (strcmp(key, "follow_profile") == 0)
            st->follow_profile = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (strncmp(key, "custom10_", 9) == 0) {
            int i = atoi(key + 9);
            if (i >= 0 && i < 10)
                st->custom_10[i] = strtof(val, NULL);
        } else if (strncmp(key, "custom15_", 9) == 0) {
            int i = atoi(key + 9);
            if (i >= 0 && i < 15)
                st->custom_15[i] = strtof(val, NULL);
        } else if (strncmp(key, "custom20_", 9) == 0) {
            int i = atoi(key + 9);
            if (i >= 0 && i < 20)
                st->custom_20[i] = strtof(val, NULL);
        }
    }
    fclose(f);

    if (st->band_count != AH_BANDS_10 && st->band_count != AH_BANDS_15 &&
        st->band_count != AH_BANDS_20)
        st->band_count = AH_BANDS_10;
    if ((int)st->view < 0 || (int)st->view > 1)
        st->view = AH_EQ_VIEW_SLIDERS;
    if ((int)st->preset < 0 || (int)st->preset >= AH_EQ_PRESET_COUNT)
        st->preset = AH_EQ_PRESET_FLAT;

    if (st->follow_profile) {
        /* Listening options will be applied by ah_core_apply_listening(). */
        st->preset = AH_EQ_PRESET_CUSTOM;
    } else {
        ah_eq_apply_preset(st, st->preset);
    }
    return 0;
}

int ah_eq_state_save(const AhEqualizerState *st)
{
    if (!st)
        return -1;

    char path[640];
    snprintf(path, sizeof(path), "%s/equalizer.conf", ah_config_dir());
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "# AudioHawk equalizer\n");
    fprintf(f, "band_count=%d\n", (int)st->band_count);
    fprintf(f, "view=%d\n", (int)st->view);
    fprintf(f, "preset=%d\n", (int)st->preset);
    fprintf(f, "follow_profile=%s\n", st->follow_profile ? "true" : "false");
    for (int i = 0; i < 10; ++i)
        fprintf(f, "custom10_%d=%.3f\n", i, st->custom_10[i]);
    for (int i = 0; i < 15; ++i)
        fprintf(f, "custom15_%d=%.3f\n", i, st->custom_15[i]);
    for (int i = 0; i < 20; ++i)
        fprintf(f, "custom20_%d=%.3f\n", i, st->custom_20[i]);
    fclose(f);
    return 0;
}
