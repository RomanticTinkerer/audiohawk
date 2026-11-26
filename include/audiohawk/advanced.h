#ifndef AUDIOHAWK_ADVANCED_H
#define AUDIOHAWK_ADVANCED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AH_ADV_SLOTS 42
#define AH_ADV_PARAMS 9 /* most controls any one effect has */
#define AH_ADV_SPEC 16
#define AH_ADV_APPS 8
#define AH_ADV_PRESETS 6
#define AH_ADV_ORDERS 4
#define AH_ADV_APP_NAME 64

/* A preset or profile switches effects on and overrides a few controls; the rest stay at defaults. */
typedef struct AhAdvSet {
    int slot;
    int param; /* -1: only switch the effect on */
    float value;
} AhAdvSet;

typedef enum {
    AH_ADV_DYN_BASS = 0,
    AH_ADV_COMPRESSOR,
    AH_ADV_STEREO_IMAGE,
    AH_ADV_EXCITER,
    AH_ADV_CROSSFEED,
    AH_ADV_RES_TAMER,
    AH_ADV_LIMITER,
    AH_ADV_AGC,
    AH_ADV_DC,
    AH_ADV_NOISE,
    AH_ADV_HIGHPASS,
    AH_ADV_HEADROOM,
    AH_ADV_MONO,
    AH_ADV_SPECTRAL,
    AH_ADV_LOUDNESS,
    AH_ADV_ANALOG,
    AH_ADV_VINTAGE,
    AH_ADV_ROOM,
    AH_ADV_HALL,
    AH_ADV_WIDEN,
    AH_ADV_SPEAKERS,
    AH_ADV_TAPE,
    AH_ADV_TUBE,
    AH_ADV_VINYL,
    AH_ADV_SPATIAL3D,
    AH_ADV_PSYCHO_BASS,
    AH_ADV_DISTORT,
    AH_ADV_MB_EXCITER,
    AH_ADV_DYN_TREBLE,
    AH_ADV_LOFI,
    AH_ADV_CONVOLVE,
    AH_ADV_PARAMETRIC,
    AH_ADV_MB_COMP,
    AH_ADV_TRANSIENT,
    AH_ADV_DEESSER,
    AH_ADV_SPEC_GATE,
    AH_ADV_DYN_EQ,
    AH_ADV_ADAPT_EQ,
    AH_ADV_FD_WIDTH,
    AH_ADV_CENTRE_FOCUS,
    AH_ADV_CENTRE_PUNCH,
    AH_ADV_CENTRE_ANCHOR
} AhAdvSlot;

/* One tunable control of an effect, in real units. */
typedef struct {
    const char *key; /* saved in advanced.conf, never rename */
    const char *label;
    const char *unit;
    float min, max, step, def;
} AhAdvParam;

typedef struct {
    bool on[AH_ADV_SLOTS];
    float param[AH_ADV_SLOTS][AH_ADV_PARAMS]; /* see ah_advanced_param() */
    bool ab_bypass;
    bool gain_match;
    bool per_app;
    int order;  /* 0..AH_ADV_ORDERS-1 */
    int preset; /* 0 = custom */
    char app_name[AH_ADV_APP_NAME];
    struct {
        char name[AH_ADV_APP_NAME];
        bool on[AH_ADV_SLOTS];
        float param[AH_ADV_SLOTS][AH_ADV_PARAMS];
    } apps[AH_ADV_APPS];
    int n_apps;
} AhAdvancedState;

typedef struct {
    float peak;
    float spec[AH_ADV_SPEC];
    float cpu;
    float corr;
    float dry_rms;
    float wet_rms;
    bool clip;
} AhAdvancedMeters;

typedef struct AhAdvancedEngine AhAdvancedEngine;

void ah_advanced_defaults(AhAdvancedState *s);
void ah_advanced_clamp(AhAdvancedState *s);
bool ah_advanced_any_active(const AhAdvancedState *s);
int ah_advanced_load(AhAdvancedState *s);
int ah_advanced_save(const AhAdvancedState *s);

void ah_advanced_apply_preset(AhAdvancedState *s, int preset);
void ah_advanced_apply_set(AhAdvancedState *s, const AhAdvSet *set);
void ah_advanced_select_app(AhAdvancedState *s, const char *name);
void ah_advanced_store_app(AhAdvancedState *s);

const char *ah_advanced_slot_key(int slot);
const char *ah_advanced_slot_title(int slot);
const char *ah_advanced_slot_blurb(int slot);
const char *ah_advanced_slot_icon(int slot);
int ah_advanced_param_count(int slot);
const AhAdvParam *ah_advanced_param(int slot, int index); /* NULL when out of range */

const char *ah_advanced_preset_name(int id);
const char *ah_advanced_order_name(int id);

AhAdvancedEngine *ah_advanced_create(void);
void ah_advanced_destroy(AhAdvancedEngine *eng);
void ah_advanced_reset(AhAdvancedEngine *eng);
void ah_advanced_apply(AhAdvancedEngine *eng, const AhAdvancedState *s, uint32_t rate);
void ah_advanced_process_block(AhAdvancedEngine *eng, float *left, float *right,
                               uint32_t n, uint32_t rate);
void ah_advanced_meters(const AhAdvancedEngine *eng, AhAdvancedMeters *out);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_ADVANCED_H */
