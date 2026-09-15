#ifndef AUDIOHAWK_PROFILES_H
#define AUDIOHAWK_PROFILES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AH_EQ_BANDS 10
#define AH_PROFILE_COUNT 6
#define AH_INTEL_COUNT 4

typedef enum {
    AH_PROFILE_MUSIC = 0,
    AH_PROFILE_MOVIE,
    AH_PROFILE_GAME,
    AH_PROFILE_WORK,
    AH_PROFILE_CASUAL,
    AH_PROFILE_MOOD
} AhProfileId;

typedef enum {
    AH_INTEL_OFF = 0,
    AH_INTEL_DETAILED,
    AH_INTEL_WARM,
    AH_INTEL_BALANCED
} AhIntelMode;

typedef struct {
    float freq_hz;
    float q;
    float gain_db;
} AhEqBand;

typedef struct {
    AhProfileId id;
    const char *name;
    const char *blurb;
    AhEqBand bands[AH_EQ_BANDS];
} AhProfileDef;

typedef struct {
    AhIntelMode id;
    const char *name;
    const char *blurb;
} AhIntelDef;

const AhProfileDef *ah_profile_defs(void);
const AhIntelDef *ah_intel_defs(void);

const AhProfileDef *ah_profile_by_id(AhProfileId id);
const AhIntelDef *ah_intel_by_id(AhIntelMode id);

/* Compose final band gains from profile + intelligent mode. */
void ah_compose_eq(AhProfileId profile, AhIntelMode intel, AhEqBand out[AH_EQ_BANDS]);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_PROFILES_H */
