#ifndef AUDIOHAWK_PIPEWIRE_EQ_H
#define AUDIOHAWK_PIPEWIRE_EQ_H

#include "audiohawk/effects.h"
#include "audiohawk/equalizer.h"
#include "audiohawk/profiles.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AhPipewireEq AhPipewireEq;

AhPipewireEq *ah_pw_eq_create(void);
void ah_pw_eq_destroy(AhPipewireEq *eq);

/* Arm / disarm EasyEffects-style sink + metadata stream routing + filter graph. */
int ah_pw_eq_enable(AhPipewireEq *eq, bool enabled);
bool ah_pw_eq_is_enabled(const AhPipewireEq *eq);

/* When false, graphic EQ bands are bypassed (0 dB) but FX still run. */
void ah_pw_eq_set_eq_active(AhPipewireEq *eq, bool active);

/* Apply n peaking bands (1..AH_EQ_BANDS_MAX) to the live filter graph. */
int ah_pw_eq_apply_bands(AhPipewireEq *eq, const AhEqBand *bands, int n_bands);

/* Push advanced Settings effects into the live DSP chain. */
int ah_pw_eq_apply_effects(AhPipewireEq *eq, const AhEffectsState *fx);

const char *ah_pw_eq_last_error(const AhPipewireEq *eq);
const char *ah_pw_eq_node_name(void);

/* True when the PipeWire core dropped (e.g. after suspend) and recovery is pending. */
bool ah_pw_eq_needs_recovery(const AhPipewireEq *eq);

/* Reconnect to PipeWire and re-arm the graph if EQ was enabled. Safe to call often. */
int ah_pw_eq_recover(AhPipewireEq *eq);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_PIPEWIRE_EQ_H */
