#ifndef AUDIOHAWK_CONFIG_H
#define AUDIOHAWK_CONFIG_H

#include "audiohawk/profiles.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool eq_enabled;
    AhProfileId profile;
    AhIntelMode intel;
    bool auto_switch_profiles;
    bool per_device_memory;
    bool start_on_startup;
} AhSettings;

void ah_settings_defaults(AhSettings *s);

/* ~/.config/audiohawk/settings.conf */
int ah_settings_load(AhSettings *s);
int ah_settings_save(const AhSettings *s);

const char *ah_config_dir(void);

/* XDG autostart (~/.config/autostart/audiohawk.desktop). */
bool ah_autostart_is_installed(void);
int ah_autostart_set(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_CONFIG_H */
