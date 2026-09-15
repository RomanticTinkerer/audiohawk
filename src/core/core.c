#include "audiohawk/core.h"

#include <string.h>

int ah_core_sync_pipeline(AhCore *core)
{
    if (!core || !core->eq)
        return -1;

    /* Master gate: Equalizer toggle. Effects only run while EQ is on. */
    const bool want = core->settings.eq_enabled;

    ah_pw_eq_set_eq_active(core->eq, want);
    (void)ah_pw_eq_apply_bands(core->eq,
                               core->equalizer.bands,
                               (int)core->equalizer.band_count);

    if (want) {
        (void)ah_pw_eq_apply_effects(core->eq, &core->effects);
    } else {
        AhEffectsState bypass;
        ah_effects_defaults(&bypass);
        (void)ah_pw_eq_apply_effects(core->eq, &bypass);
    }

    if (ah_pw_eq_enable(core->eq, want) != 0) {
        if (want)
            core->settings.eq_enabled = false;
        return -1;
    }
    return 0;
}

int ah_core_apply_listening(AhCore *core)
{
    if (!core)
        return -1;

    AhEqBand curve[AH_EQ_BANDS];
    ah_compose_eq(core->settings.profile, core->settings.intel, curve);
    ah_eq_from_profile_curve(&core->equalizer, curve);
    return ah_core_refresh_eq(core);
}

int ah_core_init(AhCore *core)
{
    if (!core)
        return -1;

    memset(core, 0, sizeof(*core));
    ah_settings_load(&core->settings);
    ah_app_profiles_load(&core->app_profiles);
    ah_eq_state_load(&core->equalizer);
    ah_effects_load(&core->effects);

    core->eq = ah_pw_eq_create();
    if (!core->eq)
        return -1;

    /* Keep XDG autostart file in sync with the saved preference. */
    (void)ah_autostart_set(core->settings.start_on_startup);

    if (core->equalizer.follow_profile)
        (void)ah_core_apply_listening(core);
    else
        (void)ah_core_refresh_eq(core);

    (void)ah_core_sync_pipeline(core);
    return 0;
}

void ah_core_shutdown(AhCore *core)
{
    if (!core)
        return;
    ah_core_persist(core);
    if (core->eq) {
        ah_pw_eq_destroy(core->eq);
        core->eq = NULL;
    }
}

int ah_core_refresh_eq(AhCore *core)
{
    if (!core || !core->eq)
        return -1;

    ah_pw_eq_set_eq_active(core->eq, core->settings.eq_enabled);
    return ah_pw_eq_apply_bands(core->eq,
                                core->equalizer.bands,
                                (int)core->equalizer.band_count);
}

int ah_core_apply_effects(AhCore *core)
{
    if (!core || !core->eq)
        return -1;
    return ah_core_sync_pipeline(core);
}

int ah_core_set_effects(AhCore *core, const AhEffectsState *fx)
{
    if (!core || !fx)
        return -1;
    core->effects = *fx;
    ah_effects_clamp(&core->effects);
    int r = ah_core_sync_pipeline(core);
    ah_effects_save(&core->effects);
    return r;
}

int ah_core_persist(AhCore *core)
{
    if (!core)
        return -1;
    int a = ah_settings_save(&core->settings);
    int b = ah_app_profiles_save(&core->app_profiles);
    int c = ah_eq_state_save(&core->equalizer);
    int d = ah_effects_save(&core->effects);
    return (a == 0 && b == 0 && c == 0 && d == 0) ? 0 : -1;
}

int ah_core_set_eq_enabled(AhCore *core, bool enabled)
{
    if (!core || !core->eq)
        return -1;

    core->settings.eq_enabled = enabled;
    if (enabled) {
        if (core->equalizer.follow_profile)
            ah_core_apply_listening(core);
        else
            ah_core_refresh_eq(core);
    }

    if (ah_core_sync_pipeline(core) != 0) {
        if (enabled) {
            core->settings.eq_enabled = false;
            return -1;
        }
    }
    return ah_settings_save(&core->settings);
}

int ah_core_set_profile(AhCore *core, AhProfileId profile)
{
    if (!core)
        return -1;
    core->settings.profile = profile;
    int r = ah_core_apply_listening(core);
    ah_settings_save(&core->settings);
    ah_eq_state_save(&core->equalizer);
    return r;
}

int ah_core_set_intel(AhCore *core, AhIntelMode intel)
{
    if (!core)
        return -1;
    core->settings.intel = intel;
    int r = ah_core_apply_listening(core);
    ah_settings_save(&core->settings);
    ah_eq_state_save(&core->equalizer);
    return r;
}

int ah_core_set_auto_switch(AhCore *core, bool enabled)
{
    if (!core)
        return -1;
    core->settings.auto_switch_profiles = enabled;
    return ah_settings_save(&core->settings);
}

int ah_core_set_per_device_memory(AhCore *core, bool enabled)
{
    if (!core)
        return -1;
    core->settings.per_device_memory = enabled;
    return ah_settings_save(&core->settings);
}

int ah_core_set_start_on_startup(AhCore *core, bool enabled)
{
    if (!core)
        return -1;
    if (ah_autostart_set(enabled) != 0)
        return -1;
    core->settings.start_on_startup = enabled;
    return ah_settings_save(&core->settings);
}

int ah_core_set_eq_preset(AhCore *core, AhEqPresetId preset)
{
    if (!core)
        return -1;
    ah_eq_apply_preset(&core->equalizer, preset);
    int r = ah_core_refresh_eq(core);
    ah_eq_state_save(&core->equalizer);
    return r;
}

int ah_core_set_band_count(AhCore *core, AhBandCount count)
{
    if (!core)
        return -1;

    bool follow = core->equalizer.follow_profile;
    ah_eq_set_band_count(&core->equalizer, count);

    int r = follow ? ah_core_apply_listening(core) : ah_core_refresh_eq(core);
    ah_eq_state_save(&core->equalizer);
    return r;
}

int ah_core_set_eq_view(AhCore *core, AhEqViewMode view)
{
    if (!core)
        return -1;
    core->equalizer.view = view;
    return ah_eq_state_save(&core->equalizer);
}

int ah_core_set_band_gain(AhCore *core, int index, float gain_db)
{
    if (!core)
        return -1;
    if (index < 0 || index >= (int)core->equalizer.band_count)
        return -1;

    if (gain_db > 12.f)
        gain_db = 12.f;
    if (gain_db < -12.f)
        gain_db = -12.f;

    core->equalizer.bands[index].gain_db = gain_db;
    ah_eq_mark_custom(&core->equalizer);
    int r = ah_core_refresh_eq(core);
    ah_eq_state_save(&core->equalizer);
    return r;
}
