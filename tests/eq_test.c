#include "audiohawk/equalizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define test_pid _getpid
#define test_mkdir(p) _mkdir(p)
#define test_rmdir _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#define test_pid getpid
#define test_mkdir(p) mkdir(p, 0700)
#define test_rmdir rmdir
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static char config_dir[128];
const char *ah_config_dir(void) { return config_dir; }

static void write_preset(const char *path, const char *val)
{
    FILE *f = fopen(path, "w");
    CHECK(f);
    fprintf(f, "band_count=10\npreset=%s\nfollow_profile=false\ncustom10_0=4.000\n", val);
    fclose(f);
}

int main(void)
{
    snprintf(config_dir, sizeof(config_dir), "audiohawk-eq-%ld", (long)test_pid());
    CHECK(test_mkdir(config_dir) == 0);
    char path[256];
    snprintf(path, sizeof(path), "%s/equalizer.conf", config_dir);

    /* Every preset has a unique key and survives a save/load. */
    const AhEqPresetDef *d = ah_eq_preset_defs();
    for (int i = 0; i < AH_EQ_PRESET_COUNT; ++i) {
        CHECK(d[i].id == (AhEqPresetId)i);
        for (int j = 0; j < i; ++j)
            CHECK(strcmp(d[i].key, d[j].key) != 0);
        AhEqualizerState st;
        ah_eq_state_defaults(&st);
        ah_eq_apply_preset(&st, (AhEqPresetId)i);
        CHECK(ah_eq_state_save(&st) == 0);
        AhEqualizerState back;
        CHECK(ah_eq_state_load(&back) == 0);
        CHECK(back.preset == (AhEqPresetId)i);
    }

    /* Configs from 0.3.3 and older saved numbers, with Custom at 10. */
    AhEqualizerState st;
    write_preset(path, "10");
    CHECK(ah_eq_state_load(&st) == 0);
    CHECK(st.preset == AH_EQ_PRESET_CUSTOM);
    CHECK(st.bands[0].gain_db == 4.f);
    write_preset(path, "2");
    CHECK(ah_eq_state_load(&st) == 0);
    CHECK(st.preset == AH_EQ_PRESET_ROCK);
    write_preset(path, "nonsense");
    CHECK(ah_eq_state_load(&st) == 0);
    CHECK(st.preset == AH_EQ_PRESET_FLAT);

    remove(path);
    CHECK(test_rmdir(config_dir) == 0);
    printf("eq tests passed\n");
    return 0;
}
