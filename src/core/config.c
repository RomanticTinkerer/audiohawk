#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "audiohawk/config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_config_dir[512];

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
        return -1;
    }
    if (mkdir(path, 0700) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

const char *ah_config_dir(void)
{
    if (g_config_dir[0] != '\0')
        return g_config_dir;

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(g_config_dir, sizeof(g_config_dir), "%s/audiohawk", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home)
            home = ".";
        snprintf(g_config_dir, sizeof(g_config_dir), "%s/.config/audiohawk", home);
    }
    ensure_dir(g_config_dir);
    return g_config_dir;
}

void ah_settings_defaults(AhSettings *s)
{
    s->eq_enabled = false;
    s->profile = AH_PROFILE_MUSIC;
    s->intel = AH_INTEL_OFF;
    s->auto_switch_profiles = false;
    s->per_device_memory = true;
    s->start_on_startup = false;
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

int ah_settings_load(AhSettings *s)
{
    ah_settings_defaults(s);

    char path[640];
    snprintf(path, sizeof(path), "%s/settings.conf", ah_config_dir());

    FILE *f = fopen(path, "r");
    if (!f) {
        /* First run: mirror any existing XDG autostart entry. */
        s->start_on_startup = ah_autostart_is_installed();
        return 0;
    }

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

        if (strcmp(key, "eq_enabled") == 0)
            parse_bool(val, &s->eq_enabled);
        else if (strcmp(key, "profile") == 0)
            s->profile = (AhProfileId)atoi(val);
        else if (strcmp(key, "intel") == 0)
            s->intel = (AhIntelMode)atoi(val);
        else if (strcmp(key, "auto_switch_profiles") == 0)
            parse_bool(val, &s->auto_switch_profiles);
        else if (strcmp(key, "per_device_memory") == 0)
            parse_bool(val, &s->per_device_memory);
        else if (strcmp(key, "start_on_startup") == 0)
            parse_bool(val, &s->start_on_startup);
    }

    fclose(f);

    if ((int)s->profile < 0 || (int)s->profile >= AH_PROFILE_COUNT)
        s->profile = AH_PROFILE_MUSIC;
    if ((int)s->intel < 0 || (int)s->intel >= AH_INTEL_COUNT)
        s->intel = AH_INTEL_OFF;

    return 0;
}

int ah_settings_save(const AhSettings *s)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/settings.conf", ah_config_dir());

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "# AudioHawk settings\n");
    fprintf(f, "eq_enabled=%s\n", s->eq_enabled ? "true" : "false");
    fprintf(f, "profile=%d\n", (int)s->profile);
    fprintf(f, "intel=%d\n", (int)s->intel);
    fprintf(f, "auto_switch_profiles=%s\n", s->auto_switch_profiles ? "true" : "false");
    fprintf(f, "per_device_memory=%s\n", s->per_device_memory ? "true" : "false");
    fprintf(f, "start_on_startup=%s\n", s->start_on_startup ? "true" : "false");
    fclose(f);
    return 0;
}

/* ---------- XDG autostart ---------- */

static void autostart_path(char *out, size_t n)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
        snprintf(out, n, "%s/autostart/audiohawk.desktop", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home)
            home = ".";
        snprintf(out, n, "%s/.config/autostart/audiohawk.desktop", home);
    }
}

static void autostart_dir(char *out, size_t n)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
        snprintf(out, n, "%s/autostart", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home)
            home = ".";
        snprintf(out, n, "%s/.config/autostart", home);
    }
}

static int resolve_launcher_exec(char *out, size_t n)
{
    char self[4096];
    ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len > 0) {
        self[len] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = '\0';
            char candidate[4200];
            snprintf(candidate, sizeof(candidate), "%s/audiohawk", self);
            if (access(candidate, X_OK) == 0) {
                snprintf(out, n, "%s", candidate);
                return 0;
            }
        }
    }

    const char *path = getenv("PATH");
    if (path) {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s", path);
        for (char *tok = strtok(buf, ":"); tok; tok = strtok(NULL, ":")) {
            char candidate[4200];
            snprintf(candidate, sizeof(candidate), "%s/audiohawk", tok);
            if (access(candidate, X_OK) == 0) {
                snprintf(out, n, "%s", candidate);
                return 0;
            }
        }
    }

    snprintf(out, n, "audiohawk");
    return 0;
}

bool ah_autostart_is_installed(void)
{
    char path[640];
    autostart_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    bool hidden = false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Hidden=", 7) == 0) {
            const char *v = line + 7;
            if (strncmp(v, "true", 4) == 0 || v[0] == '1')
                hidden = true;
        }
    }
    fclose(f);
    return !hidden;
}

int ah_autostart_set(bool enabled)
{
    char dir[640];
    char path[640];
    autostart_dir(dir, sizeof(dir));
    autostart_path(path, sizeof(path));

    if (!enabled) {
        if (unlink(path) != 0 && errno != ENOENT)
            return -1;
        return 0;
    }

    if (ensure_dir(dir) != 0)
        return -1;

    char exec_path[4200];
    resolve_launcher_exec(exec_path, sizeof(exec_path));

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f,
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=AudioHawk\n"
            "Comment=PipeWire equalizer and listening profiles\n"
            "Exec=%s --background\n"
            "Icon=audio-equalizer\n"
            "Terminal=false\n"
            "Categories=AudioVideo;Audio;\n"
            "X-GNOME-Autostart-enabled=true\n"
            "X-GNOME-Autostart-Delay=3\n"
            "StartupNotify=false\n"
            "Hidden=false\n",
            exec_path);
    fclose(f);
    return 0;
}
