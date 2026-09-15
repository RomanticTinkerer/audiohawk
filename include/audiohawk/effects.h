#ifndef AUDIOHAWK_EFFECTS_H
#define AUDIOHAWK_EFFECTS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AH_BASS_CURVE_BALANCED = 0,
    AH_BASS_CURVE_SUB_BOOST,
    AH_BASS_CURVE_PUNCH,
    AH_BASS_CURVE_COUNT
} AhBassCurve;

typedef struct {
    bool bass_enhancer;
    AhBassCurve bass_curve;
    int bass_level; /* 0..100, step 5 */

    bool mid_enhancer;
    int mid_level; /* 0..100, step 5 */

    bool treble_enhancer;
    int treble_level; /* 0..100, step 5 */

    bool volume_leveler;
    /* Output gain like VLC: 100% = unity, up to 200% over-amplification. */
    int volume_boost; /* 100..200, step 5 */

    bool surround_virtualizer;

    bool dialogue_enhancer;
    int dialogue_strength; /* 0..10, step 1 */
} AhEffectsState;

typedef struct {
    AhBassCurve id;
    const char *name;
    const char *blurb;
} AhBassCurveDef;

const AhBassCurveDef *ah_bass_curve_defs(void);
const AhBassCurveDef *ah_bass_curve_by_id(AhBassCurve id);

void ah_effects_defaults(AhEffectsState *fx);
void ah_effects_clamp(AhEffectsState *fx);
bool ah_effects_any_active(const AhEffectsState *fx);

/* ~/.config/audiohawk/effects.conf */
int ah_effects_load(AhEffectsState *fx);
int ah_effects_save(const AhEffectsState *fx);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_EFFECTS_H */
