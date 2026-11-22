#include "audiohawk/profiles.h"

#include <string.h>

/* 10-band centers: 32 64 125 250 500 1k 2k 4k 8k 16k */
static const float k_freqs[AH_EQ_BANDS] = {
    32.f, 64.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f
};

/* Two-octave bells on one-octave spacing; the EQ designer compensates overlap. */
static const float k_q = 0.667f;

#define ON(slot) { (slot), -1, 0.f }
#define END { -1, -1, 0.f }

static const AhAdvSet k_adv_music[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -20.f }, { AH_ADV_COMPRESSOR, 1, 2.0f }, { AH_ADV_COMPRESSOR, 5, 2.0f },
    ON(AH_ADV_STEREO_IMAGE), { AH_ADV_STEREO_IMAGE, 0, 115.f },
    ON(AH_ADV_EXCITER), { AH_ADV_EXCITER, 0, 4000.f }, { AH_ADV_EXCITER, 1, 6.0f }, { AH_ADV_EXCITER, 2, 15.f },
    END
};

static const AhAdvSet k_adv_movie[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -24.f }, { AH_ADV_COMPRESSOR, 1, 3.5f }, { AH_ADV_COMPRESSOR, 5, 3.0f },
    ON(AH_ADV_LOUDNESS),
    ON(AH_ADV_HALL), { AH_ADV_HALL, 0, 75.f }, { AH_ADV_HALL, 1, 1.2f }, { AH_ADV_HALL, 2, 3500.f }, { AH_ADV_HALL, 3, 8.0f },
    END
};

static const AhAdvSet k_adv_game[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_SPATIAL3D), { AH_ADV_SPATIAL3D, 0, 45.f }, { AH_ADV_SPATIAL3D, 1, 10.f }, { AH_ADV_SPATIAL3D, 2, 25.f },
    ON(AH_ADV_TRANSIENT), { AH_ADV_TRANSIENT, 0, 40.f }, { AH_ADV_TRANSIENT, 1, -10.f },
    END
};

static const AhAdvSet k_adv_work[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_HIGHPASS), { AH_ADV_HIGHPASS, 0, 80.f }, { AH_ADV_HIGHPASS, 1, 0.71f },
    ON(AH_ADV_DEESSER), { AH_ADV_DEESSER, 0, 5500.f }, { AH_ADV_DEESSER, 1, -30.f }, { AH_ADV_DEESSER, 2, 10.f },
    ON(AH_ADV_MONO), { AH_ADV_MONO, 0, 100.f },
    END
};

static const AhAdvSet k_adv_casual[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_ANALOG), { AH_ADV_ANALOG, 0, 2.5f }, { AH_ADV_ANALOG, 1, 1.0f }, { AH_ADV_ANALOG, 2, 35.f },
    END
};

static const AhAdvSet k_adv_mood[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_TUBE), { AH_ADV_TUBE, 0, 4.0f }, { AH_ADV_TUBE, 1, 40.f }, { AH_ADV_TUBE, 2, 60.f },
    ON(AH_ADV_WIDEN), { AH_ADV_WIDEN, 0, 140.f }, { AH_ADV_WIDEN, 1, 0.0f },
    ON(AH_ADV_ROOM), { AH_ADV_ROOM, 0, 110.f }, { AH_ADV_ROOM, 1, 0.7f }, { AH_ADV_ROOM, 2, 5000.f }, { AH_ADV_ROOM, 3, 15.f },
    END
};

static const AhAdvSet k_adv_study[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -20.f }, { AH_ADV_COMPRESSOR, 1, 2.0f }, { AH_ADV_COMPRESSOR, 2, 25.0f }, { AH_ADV_COMPRESSOR, 3, 120.0f }, { AH_ADV_COMPRESSOR, 4, 8.0f }, { AH_ADV_COMPRESSOR, 5, 1.5f },
    ON(AH_ADV_CROSSFEED), { AH_ADV_CROSSFEED, 0, 700.f }, { AH_ADV_CROSSFEED, 1, 4.5f },
    ON(AH_ADV_DEESSER), { AH_ADV_DEESSER, 0, 6000.f }, { AH_ADV_DEESSER, 1, -32.f }, { AH_ADV_DEESSER, 2, 8.0f },
    END
};

static const AhAdvSet k_adv_sleep[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER), { AH_ADV_LIMITER, 1, -6.0f },
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -24.f }, { AH_ADV_COMPRESSOR, 1, 3.0f }, { AH_ADV_COMPRESSOR, 2, 30.0f }, { AH_ADV_COMPRESSOR, 3, 150.0f }, { AH_ADV_COMPRESSOR, 4, 6.0f }, { AH_ADV_COMPRESSOR, 5, 0.0f },
    ON(AH_ADV_CROSSFEED), { AH_ADV_CROSSFEED, 0, 650.f }, { AH_ADV_CROSSFEED, 1, 6.0f },
    END
};

static const AhAdvSet k_adv_classes[] = {
    ON(AH_ADV_DC),
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_HIGHPASS), { AH_ADV_HIGHPASS, 0, 95.f }, { AH_ADV_HIGHPASS, 1, 0.71f },
    ON(AH_ADV_NOISE), { AH_ADV_NOISE, 0, 7.0f }, { AH_ADV_NOISE, 1, 6.0f },
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -22.f }, { AH_ADV_COMPRESSOR, 1, 2.5f }, { AH_ADV_COMPRESSOR, 5, 2.0f },
    ON(AH_ADV_DEESSER), { AH_ADV_DEESSER, 0, 5500.f }, { AH_ADV_DEESSER, 1, -30.f }, { AH_ADV_DEESSER, 2, 8.0f },
    ON(AH_ADV_MONO), { AH_ADV_MONO, 0, 100.f },
    END
};

/*
 * Listening profiles — dB targets from common headphone/PC EQ guides
 * (Hearably, Freedom251, Progressive Radio Network gaming/movie/voice charts).
 */
static const AhProfileDef g_profiles[AH_PROFILE_COUNT] = {
    {
        .id = AH_PROFILE_MUSIC,
        .name = "Music",
        .blurb = "Mild V-shape: fuller bass and clearer air for listening.",
        .bands = {
            {32, 1.1f, 3.0f}, {64, 1.1f, 2.5f}, {125, 1.1f, 1.0f},
            {250, 1.1f, 0.0f}, {500, 1.1f, -0.5f}, {1000, 1.1f, 0.0f},
            {2000, 1.1f, 1.0f}, {4000, 1.1f, 2.0f}, {8000, 1.1f, 2.5f},
            {16000, 1.1f, 2.5f}
        },
        .fx = {
            .bass_enhancer = true,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 40,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = false,
            .volume_boost = 100,
            .surround_virtualizer = false,
            .surround_amount = 50,
            .surround_bass = 25,
            .surround_treble = 25,
            .dialogue_enhancer = false,
            .dialogue_strength = 5,
            .pitch = 0
        },
        .adv = k_adv_music
    },
    {
        .id = AH_PROFILE_MOVIE,
        .name = "Movie/Video",
        .blurb = "Cinema weight plus dialogue lift around 1 to 4 kHz.",
        .bands = {
            {32, 1.1f, 3.0f}, {64, 1.1f, 2.0f}, {125, 1.1f, 1.0f},
            {250, 1.1f, -1.0f}, {500, 1.1f, 0.0f}, {1000, 1.1f, 2.0f},
            {2000, 1.1f, 3.0f}, {4000, 1.1f, 2.0f}, {8000, 1.1f, 1.0f},
            {16000, 1.1f, 1.0f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 50,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = false,
            .volume_boost = 100,
            .surround_virtualizer = true,
            .surround_amount = 50,
            .surround_bass = 30,
            .surround_treble = 25,
            .dialogue_enhancer = true,
            .dialogue_strength = 6,
            .pitch = 0
        },
        .adv = k_adv_movie
    },
    {
        .id = AH_PROFILE_GAME,
        .name = "Game",
        .blurb = "Footsteps and positional cues forward, explosions kept tight.",
        .bands = {
            {32, 1.1f, 3.0f}, {64, 1.1f, 2.5f}, {125, 1.1f, 1.0f},
            {250, 1.1f, -1.5f}, {500, 1.1f, -1.5f}, {1000, 1.1f, 0.0f},
            {2000, 1.1f, 2.5f}, {4000, 1.1f, 4.0f}, {8000, 1.1f, 3.0f},
            {16000, 1.1f, 1.5f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_PUNCH,
            .bass_level = 50,
            .mid_enhancer = true,
            .mid_level = 55,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = false,
            .volume_boost = 100,
            .surround_virtualizer = true,
            .surround_amount = 60,
            .surround_bass = 15,
            .surround_treble = 35,
            .dialogue_enhancer = false,
            .dialogue_strength = 5,
            .pitch = 0
        },
        .adv = k_adv_game
    },
    {
        .id = AH_PROFILE_WORK,
        .name = "Work",
        .blurb = "Meetings and podcasts: cut boom, boost speech presence.",
        .bands = {
            {32, 1.1f, -4.0f}, {64, 1.1f, -2.0f}, {125, 1.1f, 0.0f},
            {250, 1.1f, -1.0f}, {500, 1.1f, -1.0f}, {1000, 1.1f, 1.0f},
            {2000, 1.1f, 3.0f}, {4000, 1.1f, 3.0f}, {8000, 1.1f, 2.0f},
            {16000, 1.1f, 1.5f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 50,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = true,
            .volume_boost = 100,
            .surround_virtualizer = false,
            .surround_amount = 50,
            .surround_bass = 25,
            .surround_treble = 25,
            .dialogue_enhancer = true,
            .dialogue_strength = 8,
            .pitch = 0
        },
        .adv = k_adv_work
    },
    {
        .id = AH_PROFILE_CASUAL,
        .name = "Casual",
        .blurb = "Easy everyday tone: soft warmth with air kept open.",
        .bands = {
            {32, 1.1f, 2.0f}, {64, 1.1f, 1.5f}, {125, 1.1f, 1.0f},
            {250, 1.1f, 0.5f}, {500, 1.1f, 0.0f}, {1000, 1.1f, 0.0f},
            {2000, 1.1f, 0.5f}, {4000, 1.1f, 0.5f}, {8000, 1.1f, 1.5f},
            {16000, 1.1f, 1.5f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 50,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = true,
            .volume_boost = 100,
            .surround_virtualizer = false,
            .surround_amount = 50,
            .surround_bass = 25,
            .surround_treble = 25,
            .dialogue_enhancer = false,
            .dialogue_strength = 5,
            .pitch = 0
        },
        .adv = k_adv_casual
    },
    {
        .id = AH_PROFILE_MOOD,
        .name = "Mood",
        .blurb = "Immersive and emotional: deep lows with open airy highs.",
        .bands = {
            {32, 1.1f, 4.5f}, {64, 1.1f, 3.8f}, {125, 1.1f, 2.8f},
            {250, 1.1f, 1.8f}, {500, 1.1f, 1.0f}, {1000, 1.1f, 0.6f},
            {2000, 1.1f, 1.0f}, {4000, 1.1f, 1.4f}, {8000, 1.1f, 2.0f},
            {16000, 1.1f, 1.6f}
        },
        .fx = {
            .bass_enhancer = true,
            .bass_curve = AH_BASS_CURVE_SUB_BOOST,
            .bass_level = 65,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = false,
            .volume_boost = 100,
            .surround_virtualizer = true,
            .surround_amount = 65,
            .surround_bass = 35,
            .surround_treble = 25,
            .dialogue_enhancer = false,
            .dialogue_strength = 5,
            .pitch = 0
        },
        .adv = k_adv_mood
    },
    {
        .id = AH_PROFILE_STUDY,
        .name = "Study",
        .blurb = "Low fatigue for long focus sessions, with gentle mids and calm highs.",
        .bands = {
            {32, 1.1f, 0.0f}, {64, 1.1f, 0.5f}, {125, 1.1f, 1.0f},
            {250, 1.1f, 1.0f}, {500, 1.1f, 0.5f}, {1000, 1.1f, 0.0f},
            {2000, 1.1f, -1.0f}, {4000, 1.1f, -1.5f}, {8000, 1.1f, -1.0f},
            {16000, 1.1f, -1.5f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 50,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = true,
            .volume_boost = 100,
            .surround_virtualizer = false,
            .surround_amount = 50,
            .surround_bass = 25,
            .surround_treble = 25,
            .dialogue_enhancer = false,
            .dialogue_strength = 5,
            .pitch = 0
        },
        .adv = k_adv_study
    },
    {
        .id = AH_PROFILE_SLEEP,
        .name = "Sleep",
        .blurb = "Dark and soft for falling asleep, with rolled-off treble and quiet dynamics.",
        .bands = {
            {32, 1.1f, -4.0f}, {64, 1.1f, -2.0f}, {125, 1.1f, 0.0f},
            {250, 1.1f, 0.5f}, {500, 1.1f, 0.0f}, {1000, 1.1f, -1.0f},
            {2000, 1.1f, -2.0f}, {4000, 1.1f, -3.5f}, {8000, 1.1f, -5.0f},
            {16000, 1.1f, -7.0f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 50,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = true,
            .volume_boost = 100,
            .surround_virtualizer = false,
            .surround_amount = 50,
            .surround_bass = 25,
            .surround_treble = 25,
            .dialogue_enhancer = false,
            .dialogue_strength = 5,
            .pitch = 0
        },
        .adv = k_adv_sleep
    },
    {
        .id = AH_PROFILE_CLASSES,
        .name = "Classes",
        .blurb = "Live classes and long lectures: clear, steady speech with background noise cut.",
        .bands = {
            {32, 1.1f, -5.0f}, {64, 1.1f, -3.5f}, {125, 1.1f, 0.0f},
            {250, 1.1f, -1.5f}, {500, 1.1f, -1.0f}, {1000, 1.1f, 1.5f},
            {2000, 1.1f, 3.0f}, {4000, 1.1f, 3.5f}, {8000, 1.1f, 1.5f},
            {16000, 1.1f, 0.0f}
        },
        .fx = {
            .bass_enhancer = false,
            .bass_curve = AH_BASS_CURVE_BALANCED,
            .bass_level = 50,
            .mid_enhancer = false,
            .mid_level = 50,
            .treble_enhancer = false,
            .treble_level = 50,
            .volume_leveler = true,
            .volume_boost = 100,
            .surround_virtualizer = false,
            .surround_amount = 50,
            .surround_bass = 25,
            .surround_treble = 25,
            .dialogue_enhancer = true,
            .dialogue_strength = 8,
            .pitch = 0
        },
        .adv = k_adv_classes
    }
};

static const AhIntelDef g_intel[AH_INTEL_COUNT] = {
    {AH_INTEL_OFF, "Off", "Use the listening profile curve as-is."},
    {AH_INTEL_DETAILED, "Detailed", "Extra presence (1 to 8 kHz) for micro-detail and clarity."},
    {AH_INTEL_WARM, "Warm", "Fuller low-mids for long sessions, air stays open."},
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

    /* Headroom is the engine's auto preamp; the curve stays as designed. */
    for (int i = 0; i < AH_EQ_BANDS; ++i) {
        if (out[i].gain_db > 12.f)
            out[i].gain_db = 12.f;
        if (out[i].gain_db < -12.f)
            out[i].gain_db = -12.f;
        out[i].freq_hz = k_freqs[i];
        out[i].q = k_q;
    }

    /* Air floor: intel modes may not cut air below the profile target. */
    for (int i = 8; i < AH_EQ_BANDS; ++i) {
        float floor_db = p->bands[i].gain_db < 0.f ? p->bands[i].gain_db : 0.f;
        if (out[i].gain_db < floor_db)
            out[i].gain_db = floor_db;
    }
}
