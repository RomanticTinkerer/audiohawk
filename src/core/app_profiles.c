#include "audiohawk/app_profiles.h"
#include "audiohawk/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ah_app_profiles_init(AhAppProfileStore *store)
{
    memset(store, 0, sizeof(*store));
}

int ah_app_profiles_load(AhAppProfileStore *store)
{
    ah_app_profiles_init(store);

    char path[640];
    snprintf(path, sizeof(path), "%s/app_profiles.conf", ah_config_dir());

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[320];
    while (fgets(line, sizeof(line), f) && store->count < AH_MAX_APP_PROFILES) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;

        /* format: app_name|profile_id|enabled */
        char *p1 = strchr(line, '|');
        if (!p1)
            continue;
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '|');
        if (!p2)
            continue;
        *p2 = '\0';

        AhAppProfile *e = &store->entries[store->count];
        size_t namelen = strlen(line);
        if (namelen >= sizeof(e->app_name))
            namelen = sizeof(e->app_name) - 1;
        memcpy(e->app_name, line, namelen);
        e->app_name[namelen] = '\0';
        e->profile = (AhProfileId)atoi(p1 + 1);
        e->enabled = atoi(p2 + 1) != 0;
        if ((int)e->profile < 0 || (int)e->profile >= AH_PROFILE_COUNT)
            e->profile = AH_PROFILE_MUSIC;
        store->count++;
    }

    fclose(f);
    return 0;
}

int ah_app_profiles_save(const AhAppProfileStore *store)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/app_profiles.conf", ah_config_dir());

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "# AudioHawk per-app profiles: name|profile_id|enabled\n");
    for (size_t i = 0; i < store->count; ++i) {
        const AhAppProfile *e = &store->entries[i];
        fprintf(f, "%s|%d|%d\n", e->app_name, (int)e->profile, e->enabled ? 1 : 0);
    }
    fclose(f);
    return 0;
}

const AhAppProfile *ah_app_profiles_find(const AhAppProfileStore *store,
                                         const char *app_name)
{
    if (!store || !app_name)
        return NULL;
    for (size_t i = 0; i < store->count; ++i) {
        if (strcmp(store->entries[i].app_name, app_name) == 0)
            return &store->entries[i];
    }
    return NULL;
}

int ah_app_profiles_upsert(AhAppProfileStore *store,
                           const char *app_name,
                           AhProfileId profile,
                           bool enabled)
{
    if (!store || !app_name || !app_name[0])
        return -1;

    for (size_t i = 0; i < store->count; ++i) {
        if (strcmp(store->entries[i].app_name, app_name) == 0) {
            store->entries[i].profile = profile;
            store->entries[i].enabled = enabled;
            return ah_app_profiles_save(store);
        }
    }

    if (store->count >= AH_MAX_APP_PROFILES)
        return -1;

    AhAppProfile *e = &store->entries[store->count++];
    snprintf(e->app_name, sizeof(e->app_name), "%s", app_name);
    e->profile = profile;
    e->enabled = enabled;
    return ah_app_profiles_save(store);
}

int ah_app_profiles_remove(AhAppProfileStore *store, const char *app_name)
{
    if (!store || !app_name)
        return -1;

    for (size_t i = 0; i < store->count; ++i) {
        if (strcmp(store->entries[i].app_name, app_name) != 0)
            continue;
        memmove(&store->entries[i],
                &store->entries[i + 1],
                (store->count - i - 1) * sizeof(AhAppProfile));
        store->count--;
        return ah_app_profiles_save(store);
    }
    return -1;
}
