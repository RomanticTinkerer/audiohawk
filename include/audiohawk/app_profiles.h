#ifndef AUDIOHAWK_APP_PROFILES_H
#define AUDIOHAWK_APP_PROFILES_H

#include "audiohawk/profiles.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AH_MAX_APP_PROFILES 64
#define AH_APP_NAME_MAX 128

typedef struct {
    char app_name[AH_APP_NAME_MAX];
    AhProfileId profile;
    bool enabled;
} AhAppProfile;

typedef struct {
    AhAppProfile entries[AH_MAX_APP_PROFILES];
    size_t count;
} AhAppProfileStore;

void ah_app_profiles_init(AhAppProfileStore *store);
int ah_app_profiles_load(AhAppProfileStore *store);
int ah_app_profiles_save(const AhAppProfileStore *store);

int ah_app_profiles_upsert(AhAppProfileStore *store,
                           const char *app_name,
                           AhProfileId profile,
                           bool enabled);

int ah_app_profiles_remove(AhAppProfileStore *store, const char *app_name);

const AhAppProfile *ah_app_profiles_find(const AhAppProfileStore *store,
                                         const char *app_name);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOHAWK_APP_PROFILES_H */
