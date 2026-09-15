#include "audiohawk/profiles.h"

#include <string.h>

/* 10-band centers: 32 64 125 250 500 1k 2k 4k 8k 16k */
static const float k_freqs[AH_EQ_BANDS] = {
    32.f, 64.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f
};

static const float k_q = 1.1f;

/*
 * Listening profiles — dB targets from common headphone/PC EQ guides
 * (Hearably, Freedom251, Progressive Radio Network gaming/movie/voice charts).
 */
static const AhProfileDef g_profiles[AH_PROFILE_COUNT] = {
    {
        .id = AH_PROFILE_MUSIC,
        .name = "Music",
        .blurb = "Mild V-shape — fuller bass and clearer air for listening.",
        .bands = {
            {32, 1.1f, 3.0f}, {64, 1.1f, 2.5f}, {125, 1.1f, 1.0f},
            {250, 1.1f, 0.0f}, {500, 1.1f, -0.5f}, {1000, 1.1f, 0.0f},
            {2000, 1.1f, 1.0f}, {4000, 1.1f, 2.0f}, {8000, 1.1f, 2.5f},
            {16000, 1.1f, 2.5f}
        }
    },
    {
        .id = AH_PROFILE_MOVIE,
        .name = "Movie/Video",
        .blurb = "Cinema weight plus dialogue lift around 1–4 kHz.",
        .bands = {
            {32, 1.1f, 3.0f}, {64, 1.1f, 2.0f}, {125, 1.1f, 1.0f},
            {250, 1.1f, -1.0f}, {500, 1.1f, 0.0f}, {1000, 1.1f, 2.0f},
            {2000, 1.1f, 3.0f}, {4000, 1.1f, 2.0f}, {8000, 1.1f, 1.0f},
            {16000, 1.1f, 1.0f}
        }
    },
    {
        .id = AH_PROFILE_GAME,
        .name = "Game",
        .blurb = "Mood curve pumped hard — punch, air, and space for play.",
        /* Mood × 1.9 with extra punch/air bands (clamped to ±12 dB). */
        .bands = {
            {32, 1.1f, 8.6f}, {64, 1.1f, 7.5f}, {125, 1.1f, 5.8f},
            {250, 1.1f, 4.0f}, {500, 1.1f, 2.2f}, {1000, 1.1f, 1.3f},
            {2000, 1.1f, 2.2f}, {4000, 1.1f, 3.2f}, {8000, 1.1f, 4.5f},
            {16000, 1.1f, 3.8f}
        }
    },
    {
        .id = AH_PROFILE_WORK,
        .name = "Work",
        .blurb = "Meetings & podcasts — cut boom, boost speech presence.",
        .bands = {
            {32, 1.1f, -4.0f}, {64, 1.1f, -2.0f}, {125, 1.1f, 0.0f},
            {250, 1.1f, -1.0f}, {500, 1.1f, -1.0f}, {1000, 1.1f, 1.0f},
            {2000, 1.1f, 3.0f}, {4000, 1.1f, 3.0f}, {8000, 1.1f, 2.0f},
            {16000, 1.1f, 1.5f}
        }
    },
    {
        .id = AH_PROFILE_CASUAL,
        .name = "Casual",
        .blurb = "Easy everyday tone — soft warmth with air kept open.",
        .bands = {
            {32, 1.1f, 2.0f}, {64, 1.1f, 1.5f}, {125, 1.1f, 1.0f},
            {250, 1.1f, 0.5f}, {500, 1.1f, 0.0f}, {1000, 1.1f, 0.0f},
            {2000, 1.1f, 0.5f}, {4000, 1.1f, 0.5f}, {8000, 1.1f, 1.5f},
            {16000, 1.1f, 1.5f}
        }
    },
    {
        .id = AH_PROFILE_MOOD,
        .name = "Mood",
        .blurb = "Immersive and emotional — deep lows with open airy highs.",
        .bands = {
            {32, 1.1f, 4.5f}, {64, 1.1f, 3.8f}, {125, 1.1f, 2.8f},
            {250, 1.1f, 1.8f}, {500, 1.1f, 1.0f}, {1000, 1.1f, 0.6f},
            {2000, 1.1f, 1.0f}, {4000, 1.1f, 1.4f}, {8000, 1.1f, 2.0f},
            {16000, 1.1f, 1.6f}
        }
    }
};

static const AhIntelDef g_intel[AH_INTEL_COUNT] = {
    {AH_INTEL_OFF, "Off", "Use the listening profile curve as-is."},
    {AH_INTEL_DETAILED, "Detailed", "Extra presence (1–8 kHz) for micro-detail and clarity."},
    {AH_INTEL_WARM, "Warm", "Fuller low-mids for long sessions — air stays open."},
    {AH_INTEL_BALANCED, "Balanced", "Ease body extremes while keeping air open."}
};

const AhProfileDef *ah_profile_defs(void)
{
    return g_profiles;
}

const AhIntelDef *ah_intel_defs(void)
{
    return g_intel;
}

const AhProfileDef *ah_profile_by_id(AhProfileId id)
{
    if ((int)id < 0 || (int)id >= AH_PROFILE_COUNT)
        return &g_profiles[0];
    return &g_profiles[id];
}

const AhIntelDef *ah_intel_by_id(AhIntelMode id)
{
    if ((int)id < 0 || (int)id >= AH_INTEL_COUNT)
        return &g_intel[0];
    return &g_intel[id];
}

void ah_compose_eq(AhProfileId profile, AhIntelMode intel, AhEqBand out[AH_EQ_BANDS])
{
    const AhProfileDef *p = ah_profile_by_id(profile);
    memcpy(out, p->bands, sizeof(p->bands));

    switch (intel) {
    case AH_INTEL_OFF:
        break;
    case AH_INTEL_DETAILED:
        /* Presence / clarity stack used in voice & gaming guides */
        out[4].gain_db -= 0.5f; /* 500 — less box */
        out[5].gain_db += 1.0f; /* 1 kHz */
        out[6].gain_db += 1.5f; /* 2 kHz */
        out[7].gain_db += 2.0f; /* 4 kHz */
        out[8].gain_db += 1.5f; /* 8 kHz */
        out[9].gain_db += 0.5f; /* 16 kHz */
        break;
    case AH_INTEL_WARM:
        out[1].gain_db += 1.0f;  /* 64 */
        out[2].gain_db += 1.5f;  /* 125 */
        out[3].gain_db += 1.5f;  /* 250 */
        out[4].gain_db += 0.8f;  /* 500 */
        /* Do not cut air / top end. */
        break;
    case AH_INTEL_BALANCED:
        /* Ease body; leave 8 kHz / 16 kHz air untouched. */
        out[0].gain_db *= 0.78f;
        out[1].gain_db *= 0.78f;
        out[2].gain_db *= 0.72f;
        out[3].gain_db *= 0.70f;
        out[4].gain_db *= 0.68f;
        out[5].gain_db *= 0.65f;
        out[6].gain_db *= 0.62f;
        out[7].gain_db *= 0.58f;
        break;
    }

    /* Soft headroom: ease hot bands, but never pull air down. */
    float peak = 0.f;
    for (int i = 0; i < AH_EQ_BANDS; ++i) {
        float a = out[i].gain_db;
        if (a < 0.f)
            a = -a;
        if (a > peak)
            peak = a;
    }
    if (peak > 6.f) {
        float shift = peak - 6.f;
        for (int i = 0; i < AH_EQ_BANDS; ++i) {
            if (i >= 8) /* 8 kHz / 16 kHz — keep air */
                continue;
            out[i].gain_db -= shift * 0.5f;
        }
    }

    for (int i = 0; i < AH_EQ_BANDS; ++i) {
        if (out[i].gain_db > 12.f)
            out[i].gain_db = 12.f;
        if (out[i].gain_db < -12.f)
            out[i].gain_db = -12.f;
        out[i].freq_hz = k_freqs[i];
        out[i].q = k_q;
    }

    /* Hard floor: no preset/intel path may clear air. */
    if (out[8].gain_db < 0.f)
        out[8].gain_db = 0.f;
    if (out[9].gain_db < 0.f)
        out[9].gain_db = 0.f;
}
