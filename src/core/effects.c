#include "audiohawk/effects.h"
#include "audiohawk/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const AhBassCurveDef g_bass_curves[AH_BASS_CURVE_COUNT] = {
    { AH_BASS_CURVE_BALANCED, "Balanced", "Even low-end lift without boom" },
    { AH_BASS_CURVE_SUB_BOOST, "Sub Boost", "Deeper sub extension and weight" },
    { AH_BASS_CURVE_PUNCH, "Punch", "Tighter mid-bass impact" },
};

const AhBassCurveDef *ah_bass_curve_defs(void)
{
    return g_bass_curves;
}

const AhBassCurveDef *ah_bass_curve_by_id(AhBassCurve id)
{
    if ((int)id < 0 || id >= AH_BASS_CURVE_COUNT)
        return &g_bass_curves[AH_BASS_CURVE_BALANCED];
    return &g_bass_curves[id];
}

void ah_effects_defaults(AhEffectsState *fx)
{
    memset(fx, 0, sizeof(*fx));
    fx->bass_curve = AH_BASS_CURVE_BALANCED;
    fx->bass_level = 50;
    fx->mid_level = 50;
    fx->treble_level = 50;
    fx->volume_boost = 100;
    fx->dialogue_strength = 5;
}

static int snap5_range(int v, int lo, int hi)
{
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    return lo + (((v - lo) + 2) / 5) * 5;
}

void ah_effects_clamp(AhEffectsState *fx)
{
    if (!fx)
        return;
    if ((int)fx->bass_curve < 0 || fx->bass_curve >= AH_BASS_CURVE_COUNT)
        fx->bass_curve = AH_BASS_CURVE_BALANCED;
    fx->bass_level = snap5_range(fx->bass_level, 0, 100);
    fx->mid_level = snap5_range(fx->mid_level, 0, 100);
    fx->treble_level = snap5_range(fx->treble_level, 0, 100);
    fx->volume_boost = snap5_range(fx->volume_boost, 100, 200);
    if (fx->dialogue_strength < 0)
        fx->dialogue_strength = 0;
    if (fx->dialogue_strength > 10)
        fx->dialogue_strength = 10;
}

bool ah_effects_any_active(const AhEffectsState *fx)
{
    if (!fx)
        return false;
    return fx->bass_enhancer || fx->mid_enhancer || fx->treble_enhancer ||
           fx->volume_leveler || fx->volume_boost > 100 ||
           fx->surround_virtualizer || fx->dialogue_enhancer;
}

static int parse_bool(const char *v, bool *out)
{
    if (!v)
        return -1;
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0) {
        *out = true;
        return 0;
    }
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

int ah_effects_load(AhEffectsState *fx)
{
    ah_effects_defaults(fx);

    char path[640];
    snprintf(path, sizeof(path), "%s/effects.conf", ah_config_dir());
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[256];
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

        if (strcmp(key, "bass_enhancer") == 0)
            parse_bool(val, &fx->bass_enhancer);
        else if (strcmp(key, "bass_curve") == 0)
            fx->bass_curve = (AhBassCurve)atoi(val);
        else if (strcmp(key, "bass_level") == 0)
            fx->bass_level = atoi(val);
        else if (strcmp(key, "mid_enhancer") == 0)
            parse_bool(val, &fx->mid_enhancer);
        else if (strcmp(key, "mid_level") == 0)
            fx->mid_level = atoi(val);
        else if (strcmp(key, "treble_enhancer") == 0)
            parse_bool(val, &fx->treble_enhancer);
        else if (strcmp(key, "treble_level") == 0)
            fx->treble_level = atoi(val);
        else if (strcmp(key, "volume_leveler") == 0)
            parse_bool(val, &fx->volume_leveler);
        else if (strcmp(key, "volume_boost") == 0)
            fx->volume_boost = atoi(val);
        else if (strcmp(key, "surround_virtualizer") == 0)
            parse_bool(val, &fx->surround_virtualizer);
        else if (strcmp(key, "dialogue_enhancer") == 0)
            parse_bool(val, &fx->dialogue_enhancer);
        else if (strcmp(key, "dialogue_strength") == 0)
            fx->dialogue_strength = atoi(val);
    }
    fclose(f);
    ah_effects_clamp(fx);
    return 0;
}

int ah_effects_save(const AhEffectsState *fx)
{
    if (!fx)
        return -1;
    AhEffectsState tmp = *fx;
    ah_effects_clamp(&tmp);

    char path[640];
    snprintf(path, sizeof(path), "%s/effects.conf", ah_config_dir());
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "# AudioHawk advanced effects\n");
    fprintf(f, "bass_enhancer=%s\n", tmp.bass_enhancer ? "true" : "false");
    fprintf(f, "bass_curve=%d\n", (int)tmp.bass_curve);
    fprintf(f, "bass_level=%d\n", tmp.bass_level);
    fprintf(f, "mid_enhancer=%s\n", tmp.mid_enhancer ? "true" : "false");
    fprintf(f, "mid_level=%d\n", tmp.mid_level);
    fprintf(f, "treble_enhancer=%s\n", tmp.treble_enhancer ? "true" : "false");
    fprintf(f, "treble_level=%d\n", tmp.treble_level);
    fprintf(f, "volume_leveler=%s\n", tmp.volume_leveler ? "true" : "false");
    fprintf(f, "volume_boost=%d\n", tmp.volume_boost);
    fprintf(f, "surround_virtualizer=%s\n", tmp.surround_virtualizer ? "true" : "false");
    fprintf(f, "dialogue_enhancer=%s\n", tmp.dialogue_enhancer ? "true" : "false");
    fprintf(f, "dialogue_strength=%d\n", tmp.dialogue_strength);
    fclose(f);
    return 0;
}
