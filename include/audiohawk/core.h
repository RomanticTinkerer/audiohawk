#ifndef AUDIOHAWK_CORE_H
#define AUDIOHAWK_CORE_H

#include "audiohawk/app_profiles.h"
#include "audiohawk/config.h"
#include "audiohawk/effects.h"
#include "audiohawk/equalizer.h"
#include "audiohawk/pipewire_eq.h"
#include "audiohawk/profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    AhSettings settings;
    AhAppProfileStore app_profiles;
    AhEqualizerState equalizer;
    AhEffectsState effects;
    AhPipewireEq *eq;
} AhCore;

int ah_core_init(AhCore *core);
void ah_core_shutdown(AhCore *core);

int ah_core_set_eq_enabled(AhCore *core, bool enabled);
int ah_core_set_profile(AhCore *core, AhProfileId profile);
int ah_core_set_intel(AhCore *core, AhIntelMode intel);
int ah_core_set_auto_switch(AhCore *core, bool enabled);
int ah_core_set_per_device_memory(AhCore *core, bool enabled);
int ah_core_set_start_on_startup(AhCore *core, bool enabled);

int ah_core_set_eq_preset(AhCore *core, AhEqPresetId preset);
int ah_core_set_band_count(AhCore *core, AhBandCount count);
int ah_core_set_eq_view(AhCore *core, AhEqViewMode view);
int ah_core_set_band_gain(AhCore *core, int index, float gain_db);

/* Advanced Settings effects — applies live and persists. */
int ah_core_set_effects(AhCore *core, const AhEffectsState *fx);
int ah_core_apply_effects(AhCore *core);

/* Rebuild live bands from Home profile + Intelligent EQ and push to PipeWire. */
int ah_core_apply_listening(AhCore *core);

int ah_core_refresh_eq(AhCore *core);
int ah_core_sync_pipeline(AhCore *core);
int ah_core_persist(AhCore *core);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_CORE_H */
