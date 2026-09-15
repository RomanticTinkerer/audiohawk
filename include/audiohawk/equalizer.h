#ifndef AUDIOHAWK_EQUALIZER_H
#define AUDIOHAWK_EQUALIZER_H

#include "audiohawk/profiles.h"

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AH_EQ_BANDS_MAX 20
#define AH_EQ_PRESET_COUNT 11

typedef enum {
    AH_BANDS_10 = 10,
    AH_BANDS_15 = 15,
    AH_BANDS_20 = 20
} AhBandCount;

typedef enum {
    AH_EQ_VIEW_CURVE = 0,
    AH_EQ_VIEW_SLIDERS
} AhEqViewMode;

typedef enum {
    AH_EQ_PRESET_FLAT = 0,
    AH_EQ_PRESET_BASS_BOOST,
    AH_EQ_PRESET_ROCK,
    AH_EQ_PRESET_POP,
    AH_EQ_PRESET_ACOUSTIC,
    AH_EQ_PRESET_CLASSICAL,
    AH_EQ_PRESET_ELECTRONIC,
    AH_EQ_PRESET_VOCAL,
    AH_EQ_PRESET_TREBLE,
    AH_EQ_PRESET_JAZZ,
    AH_EQ_PRESET_CUSTOM
} AhEqPresetId;

typedef struct {
    AhEqPresetId id;
    const char *name;
    const char *blurb;
    const char *icon; /* data/icons basename without .svg */
} AhEqPresetDef;

typedef struct {
    AhBandCount band_count;
    AhEqViewMode view;
    AhEqPresetId preset;
    /* When true, Home profile + Intelligent EQ drive the live curve. */
    bool follow_profile;
    AhEqBand bands[AH_EQ_BANDS_MAX];
    /* Persisted custom curve per band-count configuration. */
    float custom_10[10];
    float custom_15[15];
    float custom_20[20];
} AhEqualizerState;

const AhEqPresetDef *ah_eq_preset_defs(void);
const AhEqPresetDef *ah_eq_preset_by_id(AhEqPresetId id);

void ah_eq_state_defaults(AhEqualizerState *st);
void ah_eq_fill_freqs(AhBandCount count, AhEqBand out[AH_EQ_BANDS_MAX]);
void ah_eq_apply_preset(AhEqualizerState *st, AhEqPresetId preset);
void ah_eq_set_band_count(AhEqualizerState *st, AhBandCount count);

/* Mark current gains as Custom and remember them for this band count. */
void ah_eq_mark_custom(AhEqualizerState *st);

int ah_eq_state_load(AhEqualizerState *st);
int ah_eq_state_save(const AhEqualizerState *st);

/* Map a 10-band gain set into the current band layout. */
void ah_eq_apply_gains10(AhEqualizerState *st, const float gains10[10]);

/* Map listening-profile curve into the equalizer and follow Home options. */
void ah_eq_from_profile_curve(AhEqualizerState *st,
                              const AhEqBand src[AH_EQ_BANDS]);

/* Suggested graphic preset paired with a listening profile. */
AhEqPresetId ah_eq_preset_for_profile(AhProfileId profile);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_EQUALIZER_H */
