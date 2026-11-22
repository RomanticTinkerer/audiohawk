#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "audiohawk/advanced.h"
#include "audiohawk/config.h"
#include "audiohawk/dsp.h"
#include "audiohawk/spatial.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ponytail: fixed buffers sized for 48/96 kHz. Above that, the longest reverb
 * and reflection settings clamp; size them from the rate if that matters. */
#define AH_ADV_IR 1024    /* early-reflection taps, 21 ms at 48 kHz */
#define AH_ADV_COMB 16384 /* reverb delay line, 340 ms at 48 kHz */
#define AH_ADV_XDEL 96
#define AH_ADV_WOW 1024  /* tape wow delay line, power of two */
#define AH_ADV_NF 4      /* biquads one effect may own */
#define AH_ADV_NC 8      /* precomputed constants one effect may own */

#define EACH_CH for (int ch = 0; ch < 2; ++ch)

typedef struct {
    float b0, b1, b2, a1, a2;
} AhBq;

typedef struct {
    float z1, z2;
} AhBqZ;

typedef struct {
    int len[4];
    float fb[4];
    float damp, wet;
} AhReverbCfg;

typedef struct {
    bool on[AH_ADV_SLOTS];
    float p[AH_ADV_SLOTS][AH_ADV_PARAMS]; /* the user's values */
    bool ab;
    bool gain_match;
    int order;
    uint32_t rate;
    AhBq f[AH_ADV_SLOTS][AH_ADV_NF];
    float c[AH_ADV_SLOTS][AH_ADV_NC]; /* derived per effect in design_slot() */
    AhBq spec_bp[AH_ADV_SPEC];        /* log-spaced 40 Hz..16 kHz analyzer bands */
    AhSpatialParams spatial;
    AhReverbCfg room, hall;
    float ir[AH_ADV_IR]; /* time-reversed, direct sound excluded */
    int ir_n;
} AhAdvBank;

/* Every effect owns its filter memory and envelopes, so two effects that are
 * on together can never step the same state. */
typedef struct {
    AhBqZ z[2][AH_ADV_NF];
    float env[12];
    float adaa[4];
} AhSlotMem;

/* One per reverb so Room and Hall never share delay lines; positions are per channel. */
typedef struct {
    float comb[2][4][AH_ADV_COMB];
    float ap[2][2][AH_ADV_COMB];
    int comb_pos[2][4];
    int ap_pos[2][2];
    float lp[2][4];
} AhReverb;

struct AhAdvancedEngine {
    AhAdvBank banks[2];
    atomic_int idx;
    atomic_int busy;
    uint32_t rate;
    AhSlotMem mem[AH_ADV_SLOTS];
    AhSpatialState spatial;
    AhReverb room, hall;
    float ir_buf[2][2 * AH_ADV_IR]; /* written twice so the taps read contiguously */
    int ir_pos;
    float xdel[2][AH_ADV_XDEL];
    int xpos;
    float tape[2][AH_ADV_WOW];
    int tape_pos;
    AhLimiter lim;
    float gm_g;
    uint32_t rng;
    float spec_e[AH_ADV_SPEC];
    AhBqZ spec_z[AH_ADV_SPEC];
    float peak;
    float clip_hold;
    float dry_ms;
    float wet_ms;
    float corr_n;
    float corr_d;
    float cpu;
    atomic_int pub_clip;
    atomic_int pub_peak;
    atomic_int pub_cpu;
    atomic_int pub_corr;
    atomic_int pub_dry;
    atomic_int pub_wet;
    atomic_int pub_spec[AH_ADV_SPEC];
};

typedef struct {
    const char *key;
    const char *title;
    const char *blurb;
    const char *icon;
    const char *group;
    int tab;
    AhAdvParam p[AH_ADV_PARAMS]; /* { key, label, unit, min, max, step, default } */
} AhAdvMeta;

/* design_slot() and run_slot() read these by position: p[0] is the first row. */
static const AhAdvMeta k_meta[AH_ADV_SLOTS] = {
    [AH_ADV_DYN_BASS] = { "dyn_bass", "Dynamic Bass Enhancement",
        "Lifts the low band and adds harmonics, only while bass is playing",
        "effect-bass", "Tone", 0, {
        { "freq", "Frequency", "Hz", 40, 200, 5, 90 },
        { "boost", "Boost", "dB", 0, 12, 0.5f, 4 },
        { "harmonics", "Harmonics", "%", 0, 100, 5, 30 },
        { "threshold", "Threshold", "dB", -60, -10, 1, -40 } } },
    [AH_ADV_COMPRESSOR] = { "compressor", "Dynamic Range Compressor",
        "Soft-knee RMS compressor. Both channels move together",
        "effect-leveler", "Dynamics", 0, {
        { "threshold", "Threshold", "dB", -60, 0, 0.5f, -18 },
        { "ratio", "Ratio", ":1", 1, 20, 0.1f, 3 },
        { "attack", "Attack", "ms", 0.1f, 200, 0.1f, 20 },
        { "release", "Release", "ms", 10, 2000, 10, 100 },
        { "knee", "Knee", "dB", 0, 24, 0.5f, 6 },
        { "makeup", "Makeup Gain", "dB", 0, 24, 0.5f, 3 } } },
    [AH_ADV_STEREO_IMAGE] = { "stereo_image", "Stereo Image Enhancer",
        "Mid/side width with the bass folded to mono",
        "effect-surround", "Stereo", 0, {
        { "width", "Width", "%", 0, 200, 5, 120 },
        { "mono_below", "Mono Bass Below", "Hz", 20, 300, 5, 100 } } },
    [AH_ADV_EXCITER] = { "exciter", "Harmonic Exciter",
        "Saturates the band above the frequency and mixes it back for air",
        "effect-treble", "Tone", 0, {
        { "freq", "Frequency", "Hz", 1000, 10000, 100, 3500 },
        { "drive", "Drive", "dB", 0, 24, 0.5f, 8 },
        { "mix", "Mix", "%", 0, 100, 1, 20 } } },
    [AH_ADV_CROSSFEED] = { "crossfeed", "Headphone Crossfeed",
        "Bleeds a low-passed copy of each side into the other ear (Bauer bs2b)",
        "effect-surround", "Stereo", 0, {
        { "cutoff", "Cutoff", "Hz", 300, 2000, 10, 700 },
        { "feed", "Feed", "dB", 1, 15, 0.5f, 4.5f } } },
    [AH_ADV_RES_TAMER] = { "res_tamer", "Resonance Tamer",
        "Dips one narrow band, only while it is loud",
        "effect-mid", "Cleanup", 0, {
        { "freq", "Frequency", "Hz", 1000, 8000, 50, 3200 },
        { "q", "Q", "", 0.5f, 8, 0.1f, 2.4f },
        { "threshold", "Threshold", "dB", -60, 0, 0.5f, -30 },
        { "max_cut", "Max Cut", "dB", 0, 18, 0.5f, 6 } } },
    [AH_ADV_LIMITER] = { "limiter", "True Peak Limiter",
        "2 ms lookahead brickwall, catches inter-sample peaks",
        "volume-boost", "Loudness", 0, {
        { "gain", "Input Gain", "dB", 0, 18, 0.5f, 0 },
        { "ceiling", "Ceiling", "dB", -12, 0, 0.1f, -1 },
        { "release", "Release", "ms", 10, 1000, 10, 40 } } },
    [AH_ADV_AGC] = { "agc", "Automatic Gain Compensation",
        "Rides the volume toward a target level, holds through silence",
        "effect-leveler", "Loudness", 0, {
        { "target", "Target Level", "dB", -36, -6, 0.5f, -18 },
        { "max_boost", "Max Boost", "dB", 0, 24, 0.5f, 9 },
        { "max_cut", "Max Cut", "dB", 0, 24, 0.5f, 9 },
        { "speed", "Speed", "ms", 50, 5000, 50, 400 } } },
    [AH_ADV_DC] = { "dc_remove", "DC Offset Removal",
        "Removes any constant offset with a gentle high-pass below hearing",
        "flat", "Cleanup", 0, {
        { "cutoff", "Cutoff", "Hz", 2, 30, 1, 10 } } },
    [AH_ADV_NOISE] = { "noise_reduce", "Adaptive Noise Reduction",
        "Learns the hiss level in three bands and turns down what sits near it",
        "work", "Cleanup", 0, {
        { "reduction", "Reduction", "dB", 0, 30, 0.5f, 12 },
        { "sensitivity", "Sensitivity", "dB", 0, 24, 0.5f, 6 },
        { "release", "Release", "ms", 20, 1000, 10, 150 } } },
    [AH_ADV_HIGHPASS] = { "highpass", "High-Pass Filter",
        "12 dB/octave rumble cut",
        "bass", "Cleanup", 0, {
        { "cutoff", "Cutoff", "Hz", 20, 300, 5, 40 },
        { "q", "Resonance", "Q", 0.5f, 2, 0.01f, 0.71f } } },
    [AH_ADV_HEADROOM] = { "headroom", "Output Headroom Control",
        "Plain gain trim before the limiter",
        "volume-boost", "Loudness", 0, {
        { "trim", "Trim", "dB", -12, 0, 0.1f, -3 } } },
    [AH_ADV_MONO] = { "mono_check", "Mono Compatibility Checker",
        "Folds toward mono so you can hear what a single speaker would play",
        "bands", "Check", 0, {
        { "mix", "Mono Mix", "%", 0, 100, 5, 100 } } },
    [AH_ADV_SPECTRAL] = { "spectral", "Spectral Enhancement",
        "Presence bell plus an air shelf",
        "treble", "Tone", 0, {
        { "presence_freq", "Presence Frequency", "Hz", 1000, 6000, 50, 2800 },
        { "presence", "Presence", "dB", -6, 6, 0.5f, 2 },
        { "air_freq", "Air Frequency", "Hz", 6000, 16000, 100, 9000 },
        { "air", "Air", "dB", -6, 6, 0.5f, 2 } } },
    [AH_ADV_LOUDNESS] = { "loudness", "Intelligent Loudness Normalization",
        "Measures loudness the broadcast way (BS.1770) and steers it to a LUFS target",
        "effect-leveler", "Loudness", 0, {
        { "target", "Target", "LUFS", -30, -10, 0.5f, -18 },
        { "max_boost", "Max Boost", "dB", 0, 20, 0.5f, 10 },
        { "max_cut", "Max Cut", "dB", 0, 20, 0.5f, 10 },
        { "window", "Window", "ms", 400, 5000, 100, 3000 } } },
    [AH_ADV_ANALOG] = { "analog", "Analog Warmth",
        "A low-end lift into soft, slightly uneven saturation",
        "intel-warm", "Character", 1, {
        { "drive", "Drive", "dB", 0, 18, 0.5f, 4 },
        { "warmth", "Warmth", "dB", 0, 6, 0.5f, 1.5f },
        { "mix", "Mix", "%", 0, 100, 1, 50 } } },
    [AH_ADV_VINTAGE] = { "vintage", "Vintage Hi-Fi",
        "Loudness-button shelves plus gentle saturation",
        "mood", "Character", 1, {
        { "bass", "Bass", "dB", 0, 8, 0.5f, 3 },
        { "treble", "Treble", "dB", 0, 8, 0.5f, 2.5f },
        { "saturation", "Saturation", "%", 0, 100, 5, 30 } } },
    [AH_ADV_ROOM] = { "room", "Room Simulator",
        "Small-room reverb. Decay is the time the tail takes to fade by 60 dB",
        "movie", "Space", 1, {
        { "size", "Size", "%", 50, 150, 5, 100 },
        { "decay", "Decay", "s", 0.1f, 2, 0.05f, 0.5f },
        { "high_cut", "High Cut", "Hz", 500, 16000, 100, 4000 },
        { "wet", "Wet", "%", 0, 100, 1, 10 } } },
    [AH_ADV_HALL] = { "hall", "Concert Hall Reverb",
        "Large-hall reverb. Decay is the time the tail takes to fade by 60 dB",
        "movie", "Space", 1, {
        { "size", "Size", "%", 50, 150, 5, 100 },
        { "decay", "Decay", "s", 0.5f, 8, 0.1f, 1.8f },
        { "high_cut", "High Cut", "Hz", 500, 16000, 100, 3000 },
        { "wet", "Wet", "%", 0, 100, 1, 15 } } },
    [AH_ADV_WIDEN] = { "widen", "Stereo Widening",
        "Raises the side signal and can pull the center back",
        "effect-surround", "Space", 1, {
        { "width", "Width", "%", 100, 300, 5, 150 },
        { "center", "Center", "dB", -6, 0, 0.5f, 0 } } },
    [AH_ADV_SPEAKERS] = { "speakers", "Virtual Speaker Placement",
        "Each ear also hears the far speaker, later and duller, as in a room",
        "effect-surround", "Space", 1, {
        { "angle", "Speaker Angle", "deg", 10, 60, 1, 30 },
        { "level", "Far-Ear Level", "dB", -24, -3, 0.5f, -10 },
        { "shadow", "Head Shadow", "Hz", 500, 8000, 100, 2000 } } },
    [AH_ADV_TAPE] = { "tape", "Tape Saturation",
        "Saturation, real pitch wow, hiss and a head-gap rolloff",
        "intel-warm", "Character", 1, {
        { "drive", "Drive", "dB", 0, 18, 0.5f, 4 },
        { "wow", "Wow", "%", 0, 0.5f, 0.01f, 0.15f },
        { "hiss", "Hiss", "dB", -96, -50, 1, -72 },
        { "rolloff", "Rolloff", "Hz", 4000, 20000, 500, 12000 } } },
    [AH_ADV_TUBE] = { "tube", "Tube Amplifier Emulation",
        "Tube-style soft clipping. More bias adds more even harmonics",
        "intel-warm", "Character", 1, {
        { "drive", "Drive", "dB", 0, 24, 0.5f, 6 },
        { "bias", "Bias", "%", 0, 100, 5, 40 },
        { "mix", "Mix", "%", 0, 100, 1, 100 } } },
    [AH_ADV_VINYL] = { "vinyl", "Vinyl Simulation",
        "Crackle, turntable rumble and a worn top end",
        "mood", "Character", 1, {
        { "crackle", "Crackle", "%", 0, 100, 5, 30 },
        { "rumble", "Rumble", "%", 0, 100, 5, 20 },
        { "wear", "Wear", "%", 0, 100, 5, 30 } } },
    [AH_ADV_SPATIAL3D] = { "spatial3d", "3D Spatial Audio",
        "Binaural-style width and room, with its own tone lift",
        "effect-surround", "Space", 1, {
        { "strength", "Strength", "%", 0, 100, 5, 40 },
        { "bass", "Bass Lift", "%", 0, 100, 5, 20 },
        { "treble", "Treble Lift", "%", 0, 100, 5, 20 } } },
    [AH_ADV_PSYCHO_BASS] = { "psycho_bass", "Psychoacoustic Bass Boost",
        "Adds harmonics of the low band so small speakers hint at bass they can't play",
        "effect-bass", "Tone", 1, {
        { "freq", "Frequency", "Hz", 40, 200, 5, 90 },
        { "drive", "Drive", "dB", 0, 24, 0.5f, 12 },
        { "harmonics", "Harmonics", "%", 0, 100, 5, 40 } } },
    [AH_ADV_DISTORT] = { "distort", "Harmonic Distortion",
        "Heavy anti-aliased overdrive with a tone filter",
        "effect-mid", "Character", 1, {
        { "drive", "Drive", "dB", 0, 36, 0.5f, 12 },
        { "tone", "Tone", "Hz", 1000, 20000, 100, 8000 },
        { "output", "Output", "dB", -24, 0, 0.5f, -6 },
        { "mix", "Mix", "%", 0, 100, 1, 100 } } },
    [AH_ADV_MB_EXCITER] = { "mb_exciter", "Multi-band Exciter",
        "Separate saturation for the mids (200 Hz to 2.5 kHz) and the highs",
        "effect-treble", "Tone", 1, {
        { "mid_drive", "Mid Drive", "dB", 0, 24, 0.5f, 6 },
        { "mid_mix", "Mid Mix", "%", 0, 100, 1, 30 },
        { "high_drive", "High Drive", "dB", 0, 24, 0.5f, 9 },
        { "high_mix", "High Mix", "%", 0, 100, 1, 40 } } },
    [AH_ADV_DYN_TREBLE] = { "dyn_treble", "Dynamic Treble Enhancement",
        "Boosts the top only while it is above the threshold, so hiss stays down",
        "treble", "Tone", 1, {
        { "freq", "Frequency", "Hz", 3000, 12000, 100, 6000 },
        { "boost", "Boost", "dB", 0, 12, 0.5f, 4 },
        { "threshold", "Threshold", "dB", -60, -20, 1, -45 } } },
    [AH_ADV_LOFI] = { "lofi", "Lo-Fi Mode",
        "Bit crusher, sample-rate reducer and a dulling low-pass",
        "casual", "Character", 1, {
        { "bits", "Bit Depth", "bit", 4, 16, 1, 8 },
        { "rate", "Sample Rate", "Hz", 2000, 48000, 500, 12000 },
        { "tone", "Tone", "Hz", 1000, 16000, 100, 6000 } } },
    [AH_ADV_CONVOLVE] = { "convolve", "Convolution Reverb",
        "Convolves with a generated burst of early reflections",
        "movie", "Space", 2, {
        { "size", "Size", "ms", 2, 20, 0.5f, 8 },
        { "diffusion", "Diffusion", "%", 0, 100, 5, 60 },
        { "wet", "Wet", "%", 0, 100, 1, 25 } } },
    [AH_ADV_PARAMETRIC] = { "parametric", "Parametric Equalizer",
        "Three fully parametric bells",
        "eq", "Equalizer", 2, {
        { "low_freq", "Low Frequency", "Hz", 20, 500, 5, 80 },
        { "low_gain", "Low Gain", "dB", -12, 12, 0.5f, 0 },
        { "low_q", "Low Q", "", 0.3f, 8, 0.1f, 0.9f },
        { "mid_freq", "Mid Frequency", "Hz", 200, 5000, 50, 1000 },
        { "mid_gain", "Mid Gain", "dB", -12, 12, 0.5f, 0 },
        { "mid_q", "Mid Q", "", 0.3f, 8, 0.1f, 0.9f },
        { "high_freq", "High Frequency", "Hz", 2000, 18000, 100, 8000 },
        { "high_gain", "High Gain", "dB", -12, 12, 0.5f, 0 },
        { "high_q", "High Q", "", 0.3f, 8, 0.1f, 0.9f } } },
    [AH_ADV_MB_COMP] = { "mb_comp", "Multi-band Compressor",
        "Three bands, each with its own threshold and ratio",
        "bands", "Dynamics", 2, {
        { "low_split", "Low Crossover", "Hz", 60, 500, 10, 200 },
        { "high_split", "High Crossover", "Hz", 1000, 8000, 100, 2500 },
        { "low_threshold", "Low Threshold", "dB", -60, 0, 0.5f, -20 },
        { "low_ratio", "Low Ratio", ":1", 1, 10, 0.1f, 2.5f },
        { "mid_threshold", "Mid Threshold", "dB", -60, 0, 0.5f, -22 },
        { "mid_ratio", "Mid Ratio", ":1", 1, 10, 0.1f, 3 },
        { "high_threshold", "High Threshold", "dB", -60, 0, 0.5f, -24 },
        { "high_ratio", "High Ratio", ":1", 1, 10, 0.1f, 3 },
        { "makeup", "Makeup Gain", "dB", 0, 12, 0.5f, 2 } } },
    [AH_ADV_TRANSIENT] = { "transients", "Transient Shaper",
        "Level-independent attack and sustain, like a transient designer",
        "view-curve", "Dynamics", 2, {
        { "attack", "Attack", "%", -100, 100, 5, 30 },
        { "sustain", "Sustain", "%", -100, 100, 5, 0 } } },
    [AH_ADV_DEESSER] = { "deesser", "De-Esser",
        "Turns down the band above the frequency while it is harsh",
        "effect-dialogue", "Cleanup", 2, {
        { "freq", "Frequency", "Hz", 3000, 10000, 100, 6000 },
        { "threshold", "Threshold", "dB", -60, 0, 0.5f, -32 },
        { "reduction", "Max Reduction", "dB", 0, 24, 0.5f, 8 } } },
    [AH_ADV_SPEC_GATE] = { "spec_gate", "Spectral Gate",
        "Three-band gate: each band closes on its own when it drops below the threshold",
        "work", "Cleanup", 2, {
        { "threshold", "Threshold", "dB", -80, -20, 1, -55 },
        { "range", "Range", "dB", 0, 60, 1, 24 },
        { "release", "Release", "ms", 20, 1000, 10, 150 } } },
    [AH_ADV_DYN_EQ] = { "dyn_eq", "Dynamic EQ",
        "A bell that moves with level: negative range ducks, positive lifts",
        "eq", "Equalizer", 2, {
        { "freq", "Frequency", "Hz", 200, 10000, 50, 3000 },
        { "q", "Q", "", 0.5f, 8, 0.1f, 1.4f },
        { "threshold", "Threshold", "dB", -60, 0, 0.5f, -30 },
        { "ratio", "Ratio", ":1", 1, 10, 0.1f, 3 },
        { "range", "Range", "dB", -18, 18, 0.5f, -6 } } },
    [AH_ADV_ADAPT_EQ] = { "adapt_eq", "Adaptive Equalizer",
        "Slowly steers lows, mids and highs toward a target tilt",
        "intel-balanced", "Equalizer", 2, {
        { "strength", "Strength", "%", 0, 100, 5, 50 },
        { "tilt", "Target Tilt", "dB", 0, 12, 0.5f, 6 },
        { "max_change", "Max Change", "dB", 0, 12, 0.5f, 6 },
        { "speed", "Speed", "s", 0.5f, 10, 0.5f, 3 } } },
    [AH_ADV_FD_WIDTH] = { "fd_width", "Frequency-Dependent Stereo Width",
        "Separate width below and above a crossover",
        "effect-surround", "Space", 2, {
        { "split", "Crossover", "Hz", 100, 2000, 10, 300 },
        { "low_width", "Low Width", "%", 0, 200, 5, 60 },
        { "high_width", "High Width", "%", 0, 250, 5, 140 } } },
};

static const char *k_presets[AH_ADV_PRESETS] = {
    "Custom", "Off", "Music Glue", "Headphones", "Movie Night", "Lo-Fi"
};

static const char *k_orders[AH_ADV_ORDERS] = {
    "Default", "Space last", "Color first", "Fun first"
};

/* Default processing groups. Output slots always run last. */
static const int k_cleanup[] = {
    AH_ADV_DC, AH_ADV_HIGHPASS, AH_ADV_NOISE, AH_ADV_SPEC_GATE,
    AH_ADV_RES_TAMER, AH_ADV_DEESSER
};
static const int k_tone[] = {
    AH_ADV_DYN_BASS, AH_ADV_PSYCHO_BASS, AH_ADV_COMPRESSOR, AH_ADV_MB_COMP,
    AH_ADV_TRANSIENT, AH_ADV_DYN_EQ, AH_ADV_ADAPT_EQ, AH_ADV_PARAMETRIC,
    AH_ADV_SPECTRAL, AH_ADV_DYN_TREBLE, AH_ADV_EXCITER, AH_ADV_MB_EXCITER
};
static const int k_color[] = {
    AH_ADV_ANALOG, AH_ADV_TUBE, AH_ADV_TAPE, AH_ADV_DISTORT, AH_ADV_VINYL,
    AH_ADV_VINTAGE, AH_ADV_LOFI
};
static const int k_space[] = {
    AH_ADV_STEREO_IMAGE, AH_ADV_WIDEN, AH_ADV_FD_WIDTH, AH_ADV_CROSSFEED,
    AH_ADV_SPEAKERS, AH_ADV_SPATIAL3D, AH_ADV_ROOM, AH_ADV_HALL, AH_ADV_CONVOLVE
};
static const int k_output[] = {
    AH_ADV_AGC, AH_ADV_LOUDNESS, AH_ADV_HEADROOM,
    AH_ADV_MONO, AH_ADV_LIMITER
};

const char *ah_advanced_slot_key(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].key;
}

const char *ah_advanced_slot_title(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].title;
}

const char *ah_advanced_slot_blurb(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].blurb;
}

const char *ah_advanced_slot_icon(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "eq";
    return k_meta[slot].icon;
}

const char *ah_advanced_slot_group(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return "";
    return k_meta[slot].group;
}

int ah_advanced_slot_tab(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return 0;
    return k_meta[slot].tab;
}

const char *ah_advanced_preset_name(int id)
{
    if (id < 0 || id >= AH_ADV_PRESETS)
        return k_presets[0];
    return k_presets[id];
}

const char *ah_advanced_order_name(int id)
{
    if (id < 0 || id >= AH_ADV_ORDERS)
        return k_orders[0];
    return k_orders[id];
}

int ah_advanced_param_count(int slot)
{
    if (slot < 0 || slot >= AH_ADV_SLOTS)
        return 0;
    int n = 0;
    while (n < AH_ADV_PARAMS && k_meta[slot].p[n].key)
        n++;
    return n;
}

const AhAdvParam *ah_advanced_param(int slot, int index)
{
    if (index < 0 || index >= ah_advanced_param_count(slot))
        return NULL;
    return &k_meta[slot].p[index];
}

static void chain_defaults(float (*param)[AH_ADV_PARAMS])
{
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot)
        for (int i = 0; i < AH_ADV_PARAMS; ++i)
            param[slot][i] = k_meta[slot].p[i].key ? k_meta[slot].p[i].def : 0.f;
}

static void chain_clamp(float (*param)[AH_ADV_PARAMS])
{
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot) {
        for (int i = 0; i < AH_ADV_PARAMS; ++i) {
            const AhAdvParam *d = &k_meta[slot].p[i];
            float v = d->key ? param[slot][i] : 0.f;
            if (!(v >= d->min)) /* also catches NaN */
                v = d->min;
            if (v > d->max)
                v = d->max;
            param[slot][i] = v;
        }
    }
}

static int find_app(AhAdvancedState *s, const char *name)
{
    if (!s || !name || !name[0])
        return -1;
    for (int i = 0; i < s->n_apps; ++i) {
        if (strcmp(s->apps[i].name, name) == 0)
            return i;
    }
    return -1;
}

void ah_advanced_defaults(AhAdvancedState *s)
{
    static const char *names[] = { "firefox", "spotify", "steam" };
    memset(s, 0, sizeof(*s));
    chain_defaults(s->param);
    for (int a = 0; a < AH_ADV_APPS; ++a)
        chain_defaults(s->apps[a].param);
    s->n_apps = 3;
    for (int i = 0; i < s->n_apps; ++i)
        snprintf(s->apps[i].name, AH_ADV_APP_NAME, "%s", names[i]);
    snprintf(s->app_name, AH_ADV_APP_NAME, "%s", names[0]);
}

void ah_advanced_clamp(AhAdvancedState *s)
{
    if (!s)
        return;
    chain_clamp(s->param);
    if (s->order < 0 || s->order >= AH_ADV_ORDERS)
        s->order = 0;
    if (s->preset < 0 || s->preset >= AH_ADV_PRESETS)
        s->preset = 0;
    s->app_name[AH_ADV_APP_NAME - 1] = '\0';
    if (s->n_apps < 0)
        s->n_apps = 0;
    if (s->n_apps > AH_ADV_APPS)
        s->n_apps = AH_ADV_APPS;
    for (int a = 0; a < AH_ADV_APPS; ++a) {
        s->apps[a].name[AH_ADV_APP_NAME - 1] = '\0';
        chain_clamp(s->apps[a].param);
    }
}

bool ah_advanced_any_active(const AhAdvancedState *s)
{
    if (!s || s->ab_bypass)
        return false;
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        if (s->on[i])
            return true;
    }
    return false;
}

void ah_advanced_store_app(AhAdvancedState *s)
{
    if (!s || !s->per_app)
        return;
    int i = find_app(s, s->app_name);
    if (i < 0 && s->n_apps < AH_ADV_APPS && s->app_name[0]) {
        i = s->n_apps++;
        snprintf(s->apps[i].name, AH_ADV_APP_NAME, "%s", s->app_name);
    }
    if (i < 0)
        return;
    memcpy(s->apps[i].on, s->on, sizeof(s->on));
    memcpy(s->apps[i].param, s->param, sizeof(s->param));
}

void ah_advanced_select_app(AhAdvancedState *s, const char *name)
{
    if (!s || !name)
        return;
    ah_advanced_store_app(s);
    snprintf(s->app_name, AH_ADV_APP_NAME, "%s", name);
    int i = find_app(s, name);
    if (i < 0 && s->n_apps < AH_ADV_APPS) {
        i = s->n_apps++;
        snprintf(s->apps[i].name, AH_ADV_APP_NAME, "%s", name);
    }
    if (i >= 0) {
        memcpy(s->on, s->apps[i].on, sizeof(s->on));
        memcpy(s->param, s->apps[i].param, sizeof(s->param));
    }
}

/* A preset switches effects on and overrides a few controls; the rest stay at defaults. */
#define ON(slot) { slot, -1, 0 }
#define END { -1, -1, 0 }

static const AhAdvSet k_music[] = {
    ON(AH_ADV_DC), ON(AH_ADV_LIMITER),
    ON(AH_ADV_DYN_BASS), { AH_ADV_DYN_BASS, 1, 3 },
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -20 }, { AH_ADV_COMPRESSOR, 1, 2 },
    { AH_ADV_COMPRESSOR, 5, 2 },
    ON(AH_ADV_STEREO_IMAGE), { AH_ADV_STEREO_IMAGE, 0, 115 },
    ON(AH_ADV_EXCITER), { AH_ADV_EXCITER, 2, 15 },
    ON(AH_ADV_SPECTRAL), { AH_ADV_SPECTRAL, 1, 1.5f }, { AH_ADV_SPECTRAL, 3, 1.5f },
    END
};
static const AhAdvSet k_headphones[] = {
    ON(AH_ADV_DC), ON(AH_ADV_LIMITER), ON(AH_ADV_HIGHPASS), ON(AH_ADV_CROSSFEED),
    ON(AH_ADV_ANALOG), { AH_ADV_ANALOG, 0, 3 }, { AH_ADV_ANALOG, 2, 30 },
    ON(AH_ADV_SPATIAL3D), { AH_ADV_SPATIAL3D, 0, 30 },
    END
};
static const AhAdvSet k_movie[] = {
    ON(AH_ADV_DC), ON(AH_ADV_LIMITER),
    ON(AH_ADV_COMPRESSOR), { AH_ADV_COMPRESSOR, 0, -24 }, { AH_ADV_COMPRESSOR, 5, 4 },
    ON(AH_ADV_LOUDNESS),
    ON(AH_ADV_SPECTRAL), { AH_ADV_SPECTRAL, 1, 3 },
    ON(AH_ADV_HALL), { AH_ADV_HALL, 3, 8 },
    END
};
static const AhAdvSet k_lofi[] = {
    ON(AH_ADV_LIMITER),
    ON(AH_ADV_LOFI), { AH_ADV_LOFI, 1, 11000 }, { AH_ADV_LOFI, 2, 4500 },
    ON(AH_ADV_VINYL),
    ON(AH_ADV_TAPE), { AH_ADV_TAPE, 0, 6 }, { AH_ADV_TAPE, 1, 0.2f },
    END
};
static const AhAdvSet k_off[] = { END };
static const AhAdvSet *const k_preset_sets[AH_ADV_PRESETS] = {
    NULL, k_off, k_music, k_headphones, k_movie, k_lofi
};

void ah_advanced_apply_set(AhAdvancedState *s, const AhAdvSet *set)
{
    if (!s)
        return;
    memset(s->on, 0, sizeof(s->on));
    chain_defaults(s->param);
    s->ab_bypass = false;
    s->order = 0;
    s->preset = 0;
    s->gain_match = false;
    if (!set)
        return;
    for (; set->slot >= 0; ++set) {
        if (set->param < 0) {
            if (set->slot < AH_ADV_SLOTS)
                s->on[set->slot] = true;
        } else {
            if (set->slot < AH_ADV_SLOTS && set->param < AH_ADV_PARAMS)
                s->param[set->slot][set->param] = set->value;
        }
    }
}

void ah_advanced_apply_preset(AhAdvancedState *s, int preset)
{
    if (!s)
        return;
    if (preset <= 0 || preset >= AH_ADV_PRESETS) {
        s->preset = 0;
        return;
    }
    bool keep_ab = s->ab_bypass;
    bool keep_gm = s->gain_match;
    int keep_order = s->order;
    ah_advanced_apply_set(s, k_preset_sets[preset]);
    s->ab_bypass = keep_ab;
    s->gain_match = keep_gm;
    s->order = keep_order;
    s->preset = preset;
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

static int slot_by_key(const char *key, size_t n)
{
    for (int i = 0; i < AH_ADV_SLOTS; ++i) {
        if (strlen(k_meta[i].key) == n && strncmp(k_meta[i].key, key, n) == 0)
            return i;
    }
    return -1;
}

/* "on.<effect>" or "<effect>.<control>", for the main chain and each app chain. */
static void parse_chain(const char *key, const char *val, bool *on,
                        float (*param)[AH_ADV_PARAMS])
{
    if (strncmp(key, "on.", 3) == 0) {
        const int slot = slot_by_key(key + 3, strlen(key + 3));
        if (slot >= 0)
            parse_bool(val, &on[slot]);
        return;
    }
    /* Up to 0.3.5b each effect saved one 0..100 amount. Those don't map onto real
     * units, so they fall back to defaults. The mono checker is the exception:
     * below 70 it did nothing, and it must not start folding to mono now. */
    if (strcmp(key, "amt.mono_check") == 0) {
        if (atoi(val) < 70)
            param[AH_ADV_MONO][0] = 0.f;
        return;
    }
    const char *dot = strchr(key, '.');
    if (!dot)
        return;
    const int slot = slot_by_key(key, (size_t)(dot - key));
    if (slot < 0)
        return;
    for (int i = 0; i < AH_ADV_PARAMS && k_meta[slot].p[i].key; ++i) {
        if (strcmp(dot + 1, k_meta[slot].p[i].key) == 0)
            param[slot][i] = strtof(val, NULL);
    }
}

int ah_advanced_load(AhAdvancedState *s)
{
    ah_advanced_defaults(s);
    char path[640];
    snprintf(path, sizeof(path), "%s/advanced.conf", ah_config_dir());
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[512];
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
        if (strcmp(key, "ab_bypass") == 0)
            parse_bool(val, &s->ab_bypass);
        else if (strcmp(key, "gain_match") == 0)
            parse_bool(val, &s->gain_match);
        else if (strcmp(key, "per_app") == 0)
            parse_bool(val, &s->per_app);
        else if (strcmp(key, "order") == 0)
            s->order = atoi(val);
        else if (strcmp(key, "preset") == 0)
            s->preset = atoi(val);
        else if (strcmp(key, "app_name") == 0)
            snprintf(s->app_name, sizeof(s->app_name), "%s", val);
        else if (strncmp(key, "app.", 4) == 0) {
            /* app.<idx>.name, then the same keys as the main chain */
            int idx = atoi(key + 4);
            const char *dot = strchr(key + 4, '.');
            if (!dot || idx < 0 || idx >= AH_ADV_APPS)
                continue;
            if (idx + 1 > s->n_apps)
                s->n_apps = idx + 1;
            if (strcmp(dot + 1, "name") == 0)
                snprintf(s->apps[idx].name, AH_ADV_APP_NAME, "%s", val);
            else
                parse_chain(dot + 1, val, s->apps[idx].on, s->apps[idx].param);
        } else {
            parse_chain(key, val, s->on, s->param);
        }
    }
    fclose(f);
    ah_advanced_clamp(s);
    if (s->per_app && s->app_name[0]) {
        int i = find_app(s, s->app_name);
        if (i >= 0) {
            memcpy(s->on, s->apps[i].on, sizeof(s->on));
            memcpy(s->param, s->apps[i].param, sizeof(s->param));
        }
    }
    return 0;
}

static void save_chain(FILE *f, const char *prefix, const bool *on,
                       float (*param)[AH_ADV_PARAMS])
{
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot) {
        fprintf(f, "%son.%s=%s\n", prefix, k_meta[slot].key, on[slot] ? "true" : "false");
        for (int i = 0; i < AH_ADV_PARAMS && k_meta[slot].p[i].key; ++i)
            fprintf(f, "%s%s.%s=%.3f\n", prefix, k_meta[slot].key, k_meta[slot].p[i].key,
                    param[slot][i]);
    }
}

int ah_advanced_save(const AhAdvancedState *s)
{
    if (!s)
        return -1;
    AhAdvancedState tmp = *s;
    ah_advanced_clamp(&tmp);
    if (tmp.per_app)
        ah_advanced_store_app(&tmp);

    char path[640];
    snprintf(path, sizeof(path), "%s/advanced.conf", ah_config_dir());
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# AudioHawk advanced effects\n");
    fprintf(f, "ab_bypass=%s\n", tmp.ab_bypass ? "true" : "false");
    fprintf(f, "gain_match=%s\n", tmp.gain_match ? "true" : "false");
    fprintf(f, "per_app=%s\n", tmp.per_app ? "true" : "false");
    fprintf(f, "order=%d\n", tmp.order);
    fprintf(f, "preset=%d\n", tmp.preset);
    fprintf(f, "app_name=%s\n", tmp.app_name);
    save_chain(f, "", tmp.on, tmp.param);
    for (int a = 0; a < tmp.n_apps; ++a) {
        char prefix[24];
        snprintf(prefix, sizeof(prefix), "app.%d.", a);
        fprintf(f, "%sname=%s\n", prefix, tmp.apps[a].name);
        save_chain(f, prefix, tmp.apps[a].on, tmp.apps[a].param);
    }
    fclose(f);
    return 0;
}

/* ---------- DSP ---------- */

static void design_unity(AhBq *bq)
{
    bq->b0 = 1.f;
    bq->b1 = 0.f;
    bq->b2 = 0.f;
    bq->a1 = 0.f;
    bq->a2 = 0.f;
}

static void clamp_freq_lo(float *freq, uint32_t rate, float lo)
{
    if (*freq < lo)
        *freq = lo;
    if (*freq > (float)rate * 0.45f)
        *freq = (float)rate * 0.45f;
}

static void clamp_freq(float *freq, uint32_t rate)
{
    clamp_freq_lo(freq, rate, 20.f);
}

static void design_peaking(AhBq *bq, float freq, float q, float gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabsf(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }
    const float A = powf(10.f, gain_db / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float alpha = sinf(w0) / (2.f * q);
    const float cosw = cosf(w0);
    const float b0 = 1.f + alpha * A;
    const float b1 = -2.f * cosw;
    const float b2 = 1.f - alpha * A;
    const float a0 = 1.f + alpha / A;
    const float a1 = -2.f * cosw;
    const float a2 = 1.f - alpha / A;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_lowshelf(AhBq *bq, float freq, float q, float gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabsf(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }
    const float A = powf(10.f, gain_db / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float sinw = sinf(w0);
    const float alpha = sinw / (2.f * q);
    const float two = 2.f * sqrtf(A) * alpha;
    const float b0 = A * ((A + 1.f) - (A - 1.f) * cosw + two);
    const float b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw);
    const float b2 = A * ((A + 1.f) - (A - 1.f) * cosw - two);
    const float a0 = (A + 1.f) + (A - 1.f) * cosw + two;
    const float a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw);
    const float a2 = (A + 1.f) + (A - 1.f) * cosw - two;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_highshelf(AhBq *bq, float freq, float q, float gain_db, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    if (fabsf(gain_db) < 0.01f) {
        design_unity(bq);
        return;
    }
    const float A = powf(10.f, gain_db / 40.f);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float sinw = sinf(w0);
    const float alpha = sinw / (2.f * q);
    const float two = 2.f * sqrtf(A) * alpha;
    const float b0 = A * ((A + 1.f) + (A - 1.f) * cosw + two);
    const float b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw);
    const float b2 = A * ((A + 1.f) + (A - 1.f) * cosw - two);
    const float a0 = (A + 1.f) - (A - 1.f) * cosw + two;
    const float a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw);
    const float a2 = (A + 1.f) - (A - 1.f) * cosw - two;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void design_lowpass(AhBq *bq, float freq, float q, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float b0 = (1.f - cosw) * 0.5f;
    const float a0 = 1.f + alpha;
    bq->b0 = b0 / a0;
    bq->b1 = (1.f - cosw) / a0;
    bq->b2 = b0 / a0;
    bq->a1 = (-2.f * cosw) / a0;
    bq->a2 = (1.f - alpha) / a0;
}

static void design_highpass(AhBq *bq, float freq, float q, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    clamp_freq(&freq, rate);
    if (q < 0.1f)
        q = 0.1f;
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float cosw = cosf(w0);
    const float alpha = sinf(w0) / (2.f * q);
    const float b0 = (1.f + cosw) * 0.5f;
    const float a0 = 1.f + alpha;
    bq->b0 = b0 / a0;
    bq->b1 = -(1.f + cosw) / a0;
    bq->b2 = b0 / a0;
    bq->a1 = (-2.f * cosw) / a0;
    bq->a2 = (1.f - alpha) / a0;
}

/* RBJ band-pass, 0 dB peak gain. */
static void design_bandpass(AhBq *bq, float freq, float q, uint32_t rate)
{
    clamp_freq(&freq, rate);
    const float w0 = 2.f * (float)M_PI * freq / (float)rate;
    const float alpha = sinf(w0) / (2.f * q);
    const float a0 = 1.f + alpha;
    bq->b0 = alpha / a0;
    bq->b1 = 0.f;
    bq->b2 = -alpha / a0;
    bq->a1 = (-2.f * cosf(w0)) / a0;
    bq->a2 = (1.f - alpha) / a0;
}

static float bq_run(const AhBq *bq, AhBqZ *m, float x)
{
    const float y = bq->b0 * x + m->z1;
    m->z1 = bq->b1 * x - bq->a1 * y + m->z2;
    m->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

static float clampf(float x, float lo, float hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static float db2lin(float db)
{
    return powf(10.f, db * 0.05f);
}

static float lin2db(float x)
{
    return 20.f * log10f(x + 1e-12f);
}

static float tau_coeff(float ms, uint32_t rate)
{
    float t = (ms * 0.001f) * (float)(rate > 0 ? rate : 48000);
    if (t < 1.f)
        t = 1.f;
    return 1.f - expf(-1.f / t);
}

/* First-order ADAA tanh. Antiderivative is log(cosh). Cuts the alias fizz of raw tanh. */
static float tanh_adaa(float *z, float x)
{
    float x1 = *z;
    *z = x;
    if (x > 20.f)
        x = 20.f;
    if (x < -20.f)
        x = -20.f;
    if (x1 > 20.f)
        x1 = 20.f;
    if (x1 < -20.f)
        x1 = -20.f;
    float d = x - x1;
    if (fabsf(d) < 1e-5f)
        return tanhf(0.5f * (x + x1));
    /* log(cosh) is even, so both halves of the wave use the same formula. */
    const float Fx = fabsf(x) + logf((1.f + expf(-2.f * fabsf(x))) * 0.5f);
    const float Fx1 = fabsf(x1) + logf((1.f + expf(-2.f * fabsf(x1))) * 0.5f);
    return (Fx - Fx1) / d;
}

static float follow(float *env, float x, float atk, float rel)
{
    float a = fabsf(x);
    if (a > *env)
        *env += (a - *env) * atk;
    else
        *env += (a - *env) * rel;
    return *env;
}

static float rms_follow(float *s, float x, float atk, float rel)
{
    float p = x * x;
    if (p > *s)
        *s += (p - *s) * atk;
    else
        *s += (p - *s) * rel;
    return sqrtf(*s + 1e-16f);
}

/* Soft-knee GR in dB. Knee sits on the threshold, same idea as LSP / EasyEffects. */
static float softknee_gr(float level_db, float thr, float ratio, float knee)
{
    if (ratio < 1.001f)
        return 0.f;
    float over = level_db - thr;
    float half = knee * 0.5f;
    float slope = 1.f - 1.f / ratio;
    if (over > half)
        return over * slope;
    if (over > -half) {
        float x = over + half;
        return (x * x) / (2.f * knee + 1e-8f) * slope;
    }
    return 0.f;
}

static float comp_gain(float *rms, float det, float atk, float rel,
                       float thr_db, float ratio, float knee_db, float makeup_db)
{
    float level = rms_follow(rms, det, atk, rel);
    float gr = softknee_gr(lin2db(level), thr_db, ratio, knee_db);
    return db2lin(-gr + makeup_db);
}

/* ITU-R BS.1770-5 / libebur128 analog prototypes, bilinear + pre-warp. */
static void design_k_shelf(AhBq *bq, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    const float f0 = 1681.974450955533f;
    const float G = 3.999843853973347f;
    const float Q = 0.7071752369554196f;
    const float K = tanf((float)M_PI * f0 / (float)rate);
    const float Vh = powf(10.f, G / 20.f);
    const float Vb = powf(Vh, 0.4996667741545416f);
    const float a0 = 1.f + K / Q + K * K;
    bq->b0 = (Vh + Vb * K / Q + K * K) / a0;
    bq->b1 = 2.f * (K * K - Vh) / a0;
    bq->b2 = (Vh - Vb * K / Q + K * K) / a0;
    bq->a1 = 2.f * (K * K - 1.f) / a0;
    bq->a2 = (1.f - K / Q + K * K) / a0;
}

static void design_k_hp(AhBq *bq, uint32_t rate)
{
    if (rate < 1)
        rate = 48000;
    const float f0 = 38.13547087602444f;
    const float Q = 0.5003270373238773f;
    const float K = tanf((float)M_PI * f0 / (float)rate);
    const float a0 = 1.f + K / Q + K * K;
    bq->b0 = 1.f / a0;
    bq->b1 = -2.f / a0;
    bq->b2 = 1.f / a0;
    bq->a1 = 2.f * (K * K - 1.f) / a0;
    bq->a2 = (1.f - K / Q + K * K) / a0;
}

/* Bauer stereophonic-to-binaural, libbs2b formulas: cut frequency and feed level.
 * c: a0 lo, b1 lo, a0 hi, a1 hi, b1 hi, with the output gain folded in. */
static void design_bs2b(float *c, float fc_lo, float feed_db, uint32_t rate)
{
    const float gb_lo = feed_db * -5.f / 6.f - 3.f;
    const float gb_hi = feed_db / 6.f - 3.f;
    const float g_lo = db2lin(gb_lo);
    const float g_hi = 1.f - db2lin(gb_hi);
    const float fc_hi = fc_lo * powf(2.f, (gb_lo - 20.f * log10f(g_hi)) / 12.f);
    const float g = 1.f / (1.f - g_hi + g_lo);
    const float xlo = expf(-2.f * (float)M_PI * fc_lo / (float)rate);
    const float xhi = expf(-2.f * (float)M_PI * fc_hi / (float)rate);
    c[0] = g_lo * (1.f - xlo) * g;
    c[1] = xlo;
    c[2] = (1.f - g_hi * (1.f - xhi)) * g;
    c[3] = -xhi * g;
    c[4] = xhi;
}

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s ? *s : 0xA341316Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static float rng_f(uint32_t *s)
{
    return (rng_next(s) / (float)UINT32_MAX) * 2.f - 1.f;
}

/* One-pole coefficient for a cutoff in Hz. */
static float pole_hz(float hz, uint32_t rate)
{
    return 1.f - expf(-2.f * (float)M_PI * hz / (float)rate);
}

/* Per-sample multiplier that decays to 1/e in `ms`. */
static float decay_ms(float ms, uint32_t rate)
{
    return 1.f - tau_coeff(ms, rate);
}

static void design_reverb(AhReverbCfg *v, const float *p, const int *base_ms, uint32_t rate)
{
    for (int i = 0; i < 4; ++i) {
        int len = (int)lroundf((float)(base_ms[i] * (int)rate / 1000) * p[0] * 0.01f);
        if (len < 8)
            len = 8;
        if (len >= AH_ADV_COMB)
            len = AH_ADV_COMB - 1;
        v->len[i] = len;
        /* Feedback that takes exactly `decay` seconds to fall 60 dB. */
        v->fb[i] = powf(10.f, -3.f * (float)len / ((float)rate * p[1]));
    }
    v->damp = pole_hz(p[2], rate);
    v->wet = p[3] * 0.01f;
}

/* Saturator constants: c[0] input drive, c[1] level compensation. */
static void design_drive(float *c, float drive_db)
{
    c[0] = db2lin(drive_db);
    c[1] = 1.f / sqrtf(c[0]);
}

static void design_slot(AhAdvBank *b, int slot)
{
    const float *p = b->p[slot];
    AhBq *f = b->f[slot];
    float *c = b->c[slot];
    const uint32_t rate = b->rate;

    switch (slot) {
    case AH_ADV_DC:
        c[0] = pole_hz(p[0], rate);
        break;
    case AH_ADV_HIGHPASS:
        design_highpass(&f[0], p[0], p[1], rate);
        break;
    case AH_ADV_NOISE:
    case AH_ADV_SPEC_GATE:
        design_lowpass(&f[0], 250.f, 0.707f, rate);
        design_highpass(&f[1], 4000.f, 0.707f, rate);
        c[0] = tau_coeff(2.f, rate);  /* detector attack, gate opening */
        c[1] = tau_coeff(p[2], rate); /* release */
        if (slot == AH_ADV_NOISE) {
            c[2] = db2lin(-p[0]);           /* lowest gain */
            c[3] = db2lin(p[1]);            /* threshold above the learned floor */
            c[4] = tau_coeff(100.f, rate);  /* floor follows dips fast */
            c[5] = tau_coeff(20000.f, rate); /* and creeps up slowly */
        } else {
            c[2] = db2lin(-p[1]);
            c[3] = db2lin(p[0]);
        }
        break;
    case AH_ADV_RES_TAMER:
        design_bandpass(&f[0], p[0], p[1], rate);
        c[0] = tau_coeff(4.f, rate);
        c[1] = tau_coeff(40.f, rate);
        break;
    case AH_ADV_DYN_EQ:
        design_bandpass(&f[0], p[0], p[1], rate);
        c[0] = tau_coeff(8.f, rate);
        c[1] = tau_coeff(80.f, rate);
        break;
    case AH_ADV_DEESSER:
        design_highpass(&f[0], p[0], 0.707f, rate);
        c[0] = tau_coeff(2.f, rate);
        c[1] = tau_coeff(60.f, rate);
        c[2] = pole_hz(p[0] * 0.5f, rate); /* shelf corner, an octave under the detector */
        break;
    case AH_ADV_DYN_BASS:
        design_lowpass(&f[0], p[0], 0.707f, rate);
        c[0] = tau_coeff(12.f, rate);
        c[1] = tau_coeff(180.f, rate);
        c[2] = db2lin(p[1]) - 1.f;
        c[3] = p[2] * 0.01f;
        break;
    case AH_ADV_PSYCHO_BASS:
        design_lowpass(&f[0], p[0], 0.707f, rate);
        design_highpass(&f[1], p[0], 0.707f, rate); /* drops DC and most of the fundamental */
        design_lowpass(&f[2], p[0] * 4.f, 0.707f, rate);
        c[0] = db2lin(p[1]);
        c[1] = p[2] * 0.01f * 1.5f / c[0];
        break;
    case AH_ADV_COMPRESSOR:
        c[0] = tau_coeff(p[2], rate);
        c[1] = tau_coeff(p[3], rate);
        break;
    case AH_ADV_MB_COMP:
        /* Two cascaded Butterworths per edge; the mid band is what's left, so
         * the bands always add back up to the input. */
        design_lowpass(&f[0], p[0], 0.707f, rate);
        design_lowpass(&f[1], p[0], 0.707f, rate);
        design_highpass(&f[2], p[1], 0.707f, rate);
        design_highpass(&f[3], p[1], 0.707f, rate);
        c[0] = tau_coeff(40.f, rate);
        c[1] = tau_coeff(200.f, rate);
        c[2] = tau_coeff(20.f, rate);
        c[3] = tau_coeff(100.f, rate);
        c[4] = tau_coeff(8.f, rate);
        c[5] = tau_coeff(60.f, rate);
        break;
    case AH_ADV_TRANSIENT:
        c[0] = decay_ms(150.f, rate); /* onset detector release */
        c[1] = tau_coeff(30.f, rate); /* its slow copy */
        c[2] = decay_ms(60.f, rate);  /* short tail */
        c[3] = decay_ms(500.f, rate); /* long tail */
        c[4] = tau_coeff(2.f, rate);  /* gain smoothing */
        break;
    case AH_ADV_PARAMETRIC:
        for (int k = 0; k < 3; ++k)
            design_peaking(&f[k], p[3 * k], p[3 * k + 2], p[3 * k + 1], rate);
        break;
    case AH_ADV_SPECTRAL:
        design_peaking(&f[0], p[0], 1.f, p[1], rate);
        design_highshelf(&f[1], p[2], 0.707f, p[3], rate);
        break;
    case AH_ADV_DYN_TREBLE:
        design_highpass(&f[0], p[0], 0.707f, rate);
        c[0] = tau_coeff(8.f, rate);
        c[1] = tau_coeff(90.f, rate);
        c[2] = db2lin(p[1]) - 1.f;
        break;
    case AH_ADV_EXCITER:
        design_highpass(&f[0], p[0], 0.707f, rate);
        c[0] = db2lin(p[1]);
        c[1] = p[2] * 0.01f * 0.5f;
        break;
    case AH_ADV_MB_EXCITER:
        design_lowpass(&f[0], 200.f, 0.707f, rate);
        design_highpass(&f[1], 2500.f, 0.707f, rate);
        design_drive(&c[0], p[0]);
        c[2] = p[1] * 0.01f;
        design_drive(&c[3], p[2]);
        c[5] = p[3] * 0.01f;
        break;
    case AH_ADV_ANALOG:
    case AH_ADV_TUBE:
        /* c: drive, compensation, bias, tanh(bias), mix, DC blocker */
        design_drive(&c[0], p[0]);
        if (slot == AH_ADV_ANALOG) {
            design_lowshelf(&f[0], 220.f, 0.707f, p[1], rate);
            c[2] = 0.15f;
        } else {
            c[2] = p[1] * 0.01f * 0.6f;
        }
        c[3] = tanhf(c[2]);
        c[4] = p[2] * 0.01f;
        c[5] = pole_hz(5.f, rate);
        break;
    case AH_ADV_TAPE:
        design_drive(&c[0], p[0]);
        /* Delay swing in samples that gives this much pitch deviation at 0.6 Hz. */
        c[2] = clampf(p[1] * 0.01f * (float)rate / (2.f * (float)M_PI * 0.6f), 0.f, 480.f);
        c[3] = p[2] > -95.5f ? db2lin(p[2]) : 0.f;
        c[4] = 2.f * (float)M_PI * 0.6f / (float)rate;
        design_lowpass(&f[0], p[3], 0.707f, rate);
        break;
    case AH_ADV_DISTORT:
        c[0] = db2lin(p[0]);
        design_lowpass(&f[0], p[1], 0.707f, rate);
        c[1] = db2lin(p[2]);
        c[2] = p[3] * 0.01f;
        break;
    case AH_ADV_VINYL:
        design_lowpass(&f[0], 40.f, 0.707f, rate);
        design_highshelf(&f[1], 7000.f, 0.707f, -10.f * p[2] * 0.01f, rate);
        c[0] = p[0] * 0.01f;
        c[1] = p[1] * 0.01f * 0.6f;
        c[2] = c[0] * 20.f / (float)rate; /* up to 20 clicks a second */
        break;
    case AH_ADV_VINTAGE:
        design_lowshelf(&f[0], 90.f, 0.707f, p[0], rate);
        design_highshelf(&f[1], 7000.f, 0.707f, p[1], rate);
        c[0] = p[2] * 0.01f;
        c[1] = 1.f / sqrtf(1.5f);
        break;
    case AH_ADV_LOFI:
        c[0] = fmaxf(1.f, roundf((float)rate / p[1])); /* samples to hold */
        c[1] = powf(2.f, p[0] - 1.f);                 /* steps per polarity */
        design_lowpass(&f[0], p[2], 0.707f, rate);
        break;
    case AH_ADV_STEREO_IMAGE:
        design_lowpass(&f[0], p[1], 0.707f, rate);
        c[0] = p[0] * 0.01f;
        break;
    case AH_ADV_WIDEN:
        c[0] = p[0] * 0.01f;
        c[1] = db2lin(p[1]);
        break;
    case AH_ADV_FD_WIDTH:
        design_lowpass(&f[0], p[0], 0.707f, rate);
        c[0] = p[1] * 0.01f;
        c[1] = p[2] * 0.01f;
        break;
    case AH_ADV_CROSSFEED:
        design_bs2b(c, p[0], p[1], rate);
        break;
    case AH_ADV_SPEAKERS: {
        /* Woodworth: extra path around an 8.75 cm head to the far ear. */
        const float th = p[0] * (float)M_PI / 180.f;
        c[0] = clampf(roundf((float)rate * 0.0875f / 343.f * (th + sinf(th))), 1.f,
                      AH_ADV_XDEL - 1);
        c[1] = db2lin(p[1]);
        c[2] = pole_hz(p[2], rate);
        c[3] = 1.f / (1.f + c[1]);
        break;
    }
    case AH_ADV_SPATIAL3D:
        ah_spatial_configure(&b->spatial, rate, true, (int)p[0], (int)p[1], (int)p[2]);
        break;
    case AH_ADV_ROOM: {
        static const int ms[4] = { 29, 37, 41, 53 };
        design_reverb(&b->room, p, ms, rate);
        break;
    }
    case AH_ADV_HALL: {
        static const int ms[4] = { 43, 67, 89, 97 };
        design_reverb(&b->hall, p, ms, rate);
        break;
    }
    case AH_ADV_CONVOLVE: {
        /* Noise burst that falls 60 dB over `size`. Diffusion is how many taps
         * are filled: few gives distinct echoes, all gives a smooth wash. */
        int n = (int)(p[0] * 0.001f * (float)rate);
        if (n < 8)
            n = 8;
        if (n > AH_ADV_IR - 1)
            n = AH_ADV_IR - 1;
        uint32_t seed = 0x51A7EEDEu;
        float e2 = 0.f;
        for (int d = 1; d <= n; ++d) {
            const float pick = (float)rng_next(&seed) / (float)UINT32_MAX * 100.f;
            const float v = rng_f(&seed);
            const float tap =
                pick < 5.f + p[1] * 0.95f ? v * powf(10.f, -3.f * (float)d / (float)n) : 0.f;
            b->ir[n - d] = tap;
            e2 += tap * tap;
        }
        const float norm = e2 > 1e-12f ? 1.f / sqrtf(e2) : 0.f;
        for (int i = 0; i < n; ++i)
            b->ir[i] *= norm;
        b->ir_n = n;
        c[0] = p[2] * 0.01f;
        break;
    }
    case AH_ADV_AGC:
        c[0] = db2lin(p[0]);
        c[1] = db2lin(p[1]);
        c[2] = db2lin(-p[2]);
        c[3] = tau_coeff(p[3] * 0.5f, rate);
        c[4] = tau_coeff(p[3] * 2.f, rate);
        c[5] = tau_coeff(p[3], rate);
        break;
    case AH_ADV_LOUDNESS:
        design_k_shelf(&f[0], rate);
        design_k_hp(&f[1], rate);
        c[0] = tau_coeff(p[3], rate);
        c[1] = tau_coeff(p[3] * 0.5f, rate);
        break;
    case AH_ADV_ADAPT_EQ:
        design_lowpass(&f[0], 250.f, 0.707f, rate);
        design_highpass(&f[1], 5000.f, 0.707f, rate);
        c[0] = tau_coeff(p[3] * 1000.f, rate);
        c[1] = p[0] * 0.01f;
        break;
    case AH_ADV_HEADROOM:
        c[0] = db2lin(p[0]);
        break;
    case AH_ADV_MONO:
        c[0] = p[0] * 0.01f;
        break;
    case AH_ADV_LIMITER:
        c[0] = db2lin(p[0]);
        c[1] = db2lin(p[1]);
        c[2] = 1.f - expf(-1.f / (p[2] * 0.001f * (float)rate));
        break;
    default:
        break;
    }
}

static void design_bank(AhAdvBank *b, const AhAdvancedState *s, uint32_t rate)
{
    memset(b, 0, sizeof(*b));
    if (rate < 1)
        rate = 48000;
    b->rate = rate;
    b->ab = s->ab_bypass;
    b->gain_match = s->gain_match;
    b->order = s->order;
    memcpy(b->on, s->on, sizeof(b->on));
    memcpy(b->p, s->param, sizeof(b->p));

    for (int k = 0; k < AH_ADV_SPEC; ++k)
        design_bandpass(&b->spec_bp[k], 40.f * powf(400.f, (float)k / (AH_ADV_SPEC - 1)), 2.5f,
                        rate);
    for (int slot = 0; slot < AH_ADV_SLOTS; ++slot) {
        for (int k = 0; k < AH_ADV_NF; ++k)
            design_unity(&b->f[slot][k]);
        design_slot(b, slot);
    }
}

static void ms_width(float *l, float *r, float width)
{
    float mid = 0.5f * (*l + *r);
    float side = 0.5f * (*l - *r) * width;
    *l = mid + side;
    *r = mid - side;
}

static float comb_tick(float *buf, int len, int *pos, float x, float fb,
                       float *damp_z, float damp)
{
    if (len < 2)
        len = 2;
    if (len >= AH_ADV_COMB)
        len = AH_ADV_COMB - 1;
    int i = *pos;
    if (i < 0 || i >= len)
        i = 0;
    float y = buf[i];
    *damp_z += damp * (y - *damp_z);
    buf[i] = x + *damp_z * fb;
    *pos = (i + 1) % len;
    return y;
}

static float ap_tick(float *buf, int len, int *pos, float x, float g)
{
    if (len < 2)
        len = 2;
    if (len >= AH_ADV_COMB)
        len = AH_ADV_COMB - 1;
    int i = *pos;
    if (i < 0 || i >= len)
        i = 0;
    float d = buf[i];
    float y = -g * x + d;
    buf[i] = x + g * y;
    *pos = (i + 1) % len;
    return y;
}

static void run_reverb(AhReverb *v, const AhReverbCfg *cfg, float *x)
{
    EACH_CH {
        float acc = 0.f;
        for (int c = 0; c < 4; ++c)
            acc += comb_tick(v->comb[ch][c], cfg->len[c], &v->comb_pos[ch][c], x[ch],
                             cfg->fb[c], &v->lp[ch][c], cfg->damp);
        acc *= 0.25f;
        acc = ap_tick(v->ap[ch][0], cfg->len[0] / 3, &v->ap_pos[ch][0], acc, 0.5f);
        acc = ap_tick(v->ap[ch][1], cfg->len[1] / 4, &v->ap_pos[ch][1], acc, 0.5f);
        x[ch] += acc * cfg->wet;
    }
}

static float stereo_level(float *rms, const float *x, float atk, float rel)
{
    return rms_follow(rms, 0.7071f * sqrtf(x[0] * x[0] + x[1] * x[1]), atk, rel);
}

/* Three bands, each with its own gate. Adaptive: the threshold rides a learned
 * noise floor. Otherwise it is fixed. The bands add back up to the input, so an
 * open gate is transparent. */
static void band_gate(const AhBq *f, const float *c, AhSlotMem *m, bool adaptive, float *x)
{
    float band[3][2];
    EACH_CH {
        band[0][ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
        band[2][ch] = bq_run(&f[1], &m->z[ch][1], x[ch]);
        band[1][ch] = x[ch] - band[0][ch] - band[2][ch];
    }
    x[0] = 0.f;
    x[1] = 0.f;
    for (int k = 0; k < 3; ++k) {
        const float env =
            follow(&m->env[k], 0.5f * (fabsf(band[k][0]) + fabsf(band[k][1])), c[0], c[1]);
        float thr = c[3];
        if (adaptive) {
            float *floor = &m->env[3 + k];
            *floor += (env - *floor) * (env < *floor ? c[4] : c[5]);
            /* Nothing above -46 dBFS counts as noise, or steady music would get ducked. */
            if (*floor > 0.005f)
                *floor = 0.005f;
            thr = *floor * c[3];
        }
        const float want = env >= thr ? 1.f : fmaxf(c[2], (env * env) / (thr * thr + 1e-20f));
        float *g = &m->env[6 + k];
        *g += (want - *g) * (want > *g ? c[0] : c[1]);
        EACH_CH x[ch] += band[k][ch] * *g;
    }
}

/* A bell at the band-pass center whose gain follows the band's own level.
 * range < 0 ducks by up to that much, range > 0 lifts. */
static void dyn_band(const AhBq *f, const float *c, AhSlotMem *m, float *x, float thr,
                     float ratio, float range)
{
    float band[2];
    EACH_CH band[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
    const float level = stereo_level(&m->env[0], band, c[0], c[1]);
    const float move = fminf(softknee_gr(lin2db(level), thr, ratio, 6.f), fabsf(range));
    const float g = db2lin(range < 0.f ? -move : move) - 1.f;
    EACH_CH x[ch] += band[ch] * g;
}

static void run_slot(AhAdvancedEngine *e, const AhAdvBank *b, int slot, float *l, float *r)
{
    if (!b->on[slot])
        return;
    const float *p = b->p[slot];
    const float *c = b->c[slot];
    const AhBq *f = b->f[slot];
    AhSlotMem *m = &e->mem[slot];
    float x[2] = { *l, *r };

    switch (slot) {
    case AH_ADV_DC:
        /* One pole on purpose: a biquad this close to 0 Hz goes unstable in
         * 32-bit floats at high sample rates. */
        EACH_CH {
            m->env[ch] += c[0] * (x[ch] - m->env[ch]);
            x[ch] -= m->env[ch];
        }
        break;
    case AH_ADV_HIGHPASS:
        EACH_CH x[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
        break;
    case AH_ADV_NOISE:
    case AH_ADV_SPEC_GATE:
        band_gate(f, c, m, slot == AH_ADV_NOISE, x);
        break;
    case AH_ADV_RES_TAMER:
        dyn_band(f, c, m, x, p[2], 4.f, -p[3]);
        break;
    case AH_ADV_DYN_EQ:
        dyn_band(f, c, m, x, p[2], p[3], p[4]);
        break;
    case AH_ADV_DEESSER: {
        /* A steep high-pass listens for the hiss. The cut itself is a first-order
         * shelf (dry minus part of a one-pole high band), which can't boost anywhere. */
        float s[2];
        EACH_CH s[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
        const float level = stereo_level(&m->env[0], s, c[0], c[1]);
        const float g = db2lin(-fminf(softknee_gr(lin2db(level), p[1], 4.f, 6.f), p[2]));
        EACH_CH {
            m->env[1 + ch] += c[2] * (x[ch] - m->env[1 + ch]);
            x[ch] -= (x[ch] - m->env[1 + ch]) * (1.f - g);
        }
        break;
    }
    case AH_ADV_DYN_BASS: {
        float lo[2];
        EACH_CH lo[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
        const float level = stereo_level(&m->env[0], lo, c[0], c[1]);
        /* Fades in over the 12 dB above the threshold. */
        const float open = clampf((lin2db(level) - p[3]) / 12.f, 0.f, 1.f);
        EACH_CH {
            const float harm = tanh_adaa(&m->adaa[ch], lo[ch] * 6.f) / 6.f;
            x[ch] += open * (lo[ch] * c[2] + harm * c[3]);
        }
        break;
    }
    case AH_ADV_PSYCHO_BASS:
        EACH_CH {
            const float lo = bq_run(&f[0], &m->z[ch][0], x[ch]) * c[0];
            /* tanh gives odd harmonics, the rectifier even ones. */
            float h = tanhf(lo) + 0.5f * fabsf(lo);
            h = bq_run(&f[1], &m->z[ch][1], h);
            h = bq_run(&f[2], &m->z[ch][2], h);
            x[ch] += h * c[1];
        }
        break;
    case AH_ADV_COMPRESSOR: {
        const float g = comp_gain(&m->env[0], 0.7071f * sqrtf(x[0] * x[0] + x[1] * x[1]),
                                  c[0], c[1], p[0], p[1], p[4], p[5]);
        EACH_CH x[ch] *= g;
        break;
    }
    case AH_ADV_MB_COMP: {
        float band[3][2];
        EACH_CH {
            band[0][ch] = bq_run(&f[1], &m->z[ch][1], bq_run(&f[0], &m->z[ch][0], x[ch]));
            band[2][ch] = bq_run(&f[3], &m->z[ch][3], bq_run(&f[2], &m->z[ch][2], x[ch]));
            band[1][ch] = x[ch] - band[0][ch] - band[2][ch];
        }
        x[0] = 0.f;
        x[1] = 0.f;
        for (int k = 0; k < 3; ++k) {
            const float det =
                0.7071f * sqrtf(band[k][0] * band[k][0] + band[k][1] * band[k][1]);
            const float g = comp_gain(&m->env[k], det, c[2 * k], c[2 * k + 1], p[2 + 2 * k],
                                      p[3 + 2 * k], 6.f, p[8]);
            EACH_CH x[ch] += band[k][ch] * g;
        }
        break;
    }
    case AH_ADV_TRANSIENT: {
        /* Onset = a peak detector running ahead of its own smoothed copy.
         * Tail = a long-release detector sitting above a short-release one.
         * Both are level ratios, so quiet and loud hits get the same treatment. */
        const float det = fmaxf(fabsf(x[0]), fabsf(x[1]));
        float *pk = m->env;
        pk[0] = fmaxf(det, pk[0] * c[0]);
        pk[1] += c[1] * (pk[0] - pk[1]);
        pk[2] = fmaxf(det, pk[2] * c[2]);
        pk[3] = fmaxf(det, pk[3] * c[3]);
        const float onset = fmaxf(0.f, lin2db(pk[0] + 1e-4f) - lin2db(pk[1] + 1e-4f));
        const float tail = lin2db(pk[3] + 1e-4f) - lin2db(pk[2] + 1e-4f);
        const float want = clampf((onset * p[0] + tail * p[1]) * 0.01f, -12.f, 12.f);
        pk[4] += c[4] * (want - pk[4]);
        const float g = db2lin(pk[4]);
        EACH_CH x[ch] *= g;
        break;
    }
    case AH_ADV_PARAMETRIC:
        for (int k = 0; k < 3; ++k)
            EACH_CH x[ch] = bq_run(&f[k], &m->z[ch][k], x[ch]);
        break;
    case AH_ADV_SPECTRAL:
        for (int k = 0; k < 2; ++k)
            EACH_CH x[ch] = bq_run(&f[k], &m->z[ch][k], x[ch]);
        break;
    case AH_ADV_DYN_TREBLE: {
        float hi[2];
        EACH_CH hi[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
        const float level = stereo_level(&m->env[0], hi, c[0], c[1]);
        const float g = c[2] * clampf((lin2db(level) - p[2]) / 12.f, 0.f, 1.f);
        EACH_CH x[ch] += hi[ch] * g;
        break;
    }
    case AH_ADV_EXCITER:
        EACH_CH x[ch] += tanh_adaa(&m->adaa[ch],
                                   bq_run(&f[0], &m->z[ch][0], x[ch]) * c[0]) * c[1];
        break;
    case AH_ADV_MB_EXCITER:
        EACH_CH {
            const float lo = bq_run(&f[0], &m->z[ch][0], x[ch]);
            float hi = bq_run(&f[1], &m->z[ch][1], x[ch]);
            float mid = x[ch] - lo - hi;
            mid += c[2] * (tanh_adaa(&m->adaa[ch], mid * c[0]) * c[1] - mid);
            hi += c[5] * (tanh_adaa(&m->adaa[2 + ch], hi * c[3]) * c[4] - hi);
            x[ch] = lo + mid + hi;
        }
        break;
    case AH_ADV_ANALOG:
    case AH_ADV_TUBE:
        EACH_CH {
            const float in = bq_run(&f[0], &m->z[ch][0], x[ch]);
            /* The bias makes the curve lopsided, which is where even harmonics
             * come from. It also leaves a level-dependent offset, so block DC. */
            const float wet = (tanh_adaa(&m->adaa[ch], in * c[0] + c[2]) - c[3]) * c[1];
            m->env[ch] += c[5] * (wet - m->env[ch]);
            x[ch] = in + c[4] * (wet - m->env[ch] - in);
        }
        break;
    case AH_ADV_TAPE: {
        /* Wow is a slowly swinging delay, which is what bends the pitch. */
        m->env[0] += c[4];
        if (m->env[0] > 2.f * (float)M_PI)
            m->env[0] -= 2.f * (float)M_PI;
        const float at = (float)e->tape_pos - (2.f + c[2] * (1.f + sinf(m->env[0])));
        const int i0 = (int)floorf(at);
        const float frac = at - (float)i0;
        EACH_CH {
            e->tape[ch][e->tape_pos] = x[ch];
            const float a = e->tape[ch][i0 & (AH_ADV_WOW - 1)];
            const float bb = e->tape[ch][(i0 + 1) & (AH_ADV_WOW - 1)];
            const float sat = tanh_adaa(&m->adaa[ch], (a + (bb - a) * frac) * c[0]) * c[1];
            x[ch] = bq_run(&f[0], &m->z[ch][0], sat) + rng_f(&e->rng) * c[3];
        }
        e->tape_pos = (e->tape_pos + 1) & (AH_ADV_WOW - 1);
        break;
    }
    case AH_ADV_DISTORT:
        EACH_CH {
            const float wet =
                bq_run(&f[0], &m->z[ch][0], tanh_adaa(&m->adaa[ch], x[ch] * c[0])) * c[1];
            x[ch] += c[2] * (wet - x[ch]);
        }
        break;
    case AH_ADV_VINYL: {
        const float rumble = bq_run(&f[0], &m->z[0][0], rng_f(&e->rng)) * c[1];
        if ((float)rng_next(&e->rng) / (float)UINT32_MAX < c[2])
            m->env[0] = rng_f(&e->rng) * 0.3f * c[0];
        x[0] = bq_run(&f[1], &m->z[0][1], x[0]) + rumble + m->env[0];
        x[1] = bq_run(&f[1], &m->z[1][1], x[1]) + rumble + m->env[0] * 0.8f;
        m->env[0] *= 0.6f; /* a click rings for a few samples */
        break;
    }
    case AH_ADV_VINTAGE:
        EACH_CH {
            float v = bq_run(&f[0], &m->z[ch][0], x[ch]);
            v = bq_run(&f[1], &m->z[ch][1], v);
            x[ch] = v + c[0] * (tanh_adaa(&m->adaa[ch], v * 1.5f) * c[1] - v);
        }
        break;
    case AH_ADV_LOFI:
        if (m->env[0] <= 0.f) {
            m->env[0] = c[0];
            m->env[1] = x[0];
            m->env[2] = x[1];
        }
        m->env[0] -= 1.f;
        EACH_CH x[ch] = bq_run(&f[0], &m->z[ch][0], roundf(m->env[1 + ch] * c[1]) / c[1]);
        break;
    case AH_ADV_STEREO_IMAGE: {
        float lo[2];
        EACH_CH {
            lo[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
            x[ch] -= lo[ch];
        }
        ms_width(&x[0], &x[1], c[0]);
        const float bass = 0.5f * (lo[0] + lo[1]);
        EACH_CH x[ch] += bass;
        break;
    }
    case AH_ADV_WIDEN: {
        const float mid = 0.5f * (x[0] + x[1]) * c[1];
        const float side = 0.5f * (x[0] - x[1]) * c[0];
        x[0] = mid + side;
        x[1] = mid - side;
        break;
    }
    case AH_ADV_FD_WIDTH: {
        float lo[2];
        EACH_CH {
            lo[ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
            x[ch] -= lo[ch];
        }
        ms_width(&lo[0], &lo[1], c[0]);
        ms_width(&x[0], &x[1], c[1]);
        EACH_CH x[ch] += lo[ch];
        break;
    }
    case AH_ADV_CROSSFEED: {
        float lo[2], hi[2];
        EACH_CH {
            lo[ch] = c[0] * x[ch] + m->env[ch];
            m->env[ch] = c[1] * lo[ch];
            hi[ch] = c[2] * x[ch] + m->env[2 + ch];
            m->env[2 + ch] = c[3] * x[ch] + c[4] * hi[ch];
        }
        x[0] = hi[0] + lo[1];
        x[1] = hi[1] + lo[0];
        break;
    }
    case AH_ADV_SPEAKERS: {
        e->xdel[0][e->xpos] = x[0];
        e->xdel[1][e->xpos] = x[1];
        const int d = (e->xpos - (int)c[0] + AH_ADV_XDEL) % AH_ADV_XDEL;
        /* env[ch] is the far speaker as this ear hears it: late and shadowed. */
        EACH_CH m->env[ch] += c[2] * (e->xdel[1 - ch][d] - m->env[ch]);
        EACH_CH x[ch] = (x[ch] + m->env[ch] * c[1]) * c[3];
        e->xpos = (e->xpos + 1) % AH_ADV_XDEL;
        break;
    }
    case AH_ADV_SPATIAL3D:
        ah_spatial_process(&e->spatial, &b->spatial, &x[0], &x[1]);
        break;
    case AH_ADV_ROOM:
        run_reverb(&e->room, &b->room, x);
        break;
    case AH_ADV_HALL:
        run_reverb(&e->hall, &b->hall, x);
        break;
    case AH_ADV_CONVOLVE: {
        const int n = b->ir_n;
        EACH_CH {
            float *buf = e->ir_buf[ch];
            buf[e->ir_pos] = x[ch];
            buf[e->ir_pos + AH_ADV_IR] = x[ch];
            const float *h = buf + e->ir_pos + AH_ADV_IR - n; /* the n samples before this one */
            float acc[4] = { 0.f, 0.f, 0.f, 0.f };
            int i = 0;
            for (; i + 4 <= n; i += 4) {
                acc[0] += h[i] * b->ir[i];
                acc[1] += h[i + 1] * b->ir[i + 1];
                acc[2] += h[i + 2] * b->ir[i + 2];
                acc[3] += h[i + 3] * b->ir[i + 3];
            }
            for (; i < n; ++i)
                acc[0] += h[i] * b->ir[i];
            x[ch] += (acc[0] + acc[1] + acc[2] + acc[3]) * c[0];
        }
        e->ir_pos = (e->ir_pos + 1) % AH_ADV_IR;
        break;
    }
    case AH_ADV_AGC: {
        const float level = stereo_level(&m->env[0], x, c[3], c[4]);
        /* env[1] is gain minus one, so a fresh engine starts at unity. Silence
         * holds the gain instead of pumping the noise floor up. */
        if (level > 0.001f)
            m->env[1] += c[5] * (clampf(c[0] / level, c[2], c[1]) - 1.f - m->env[1]);
        EACH_CH x[ch] *= 1.f + m->env[1];
        break;
    }
    case AH_ADV_LOUDNESS: {
        float ms = 0.f;
        EACH_CH {
            const float k = bq_run(&f[1], &m->z[ch][1], bq_run(&f[0], &m->z[ch][0], x[ch]));
            ms += k * k;
        }
        m->env[0] += (ms - m->env[0]) * c[0];
        const float lufs = -0.691f + 10.f * log10f(m->env[0] + 1e-12f);
        if (lufs > -60.f)
            m->env[1] += c[1] * (db2lin(clampf(p[0] - lufs, -p[2], p[1])) - 1.f - m->env[1]);
        EACH_CH x[ch] *= 1.f + m->env[1];
        break;
    }
    case AH_ADV_ADAPT_EQ: {
        float band[3][2], db[3], mean = 0.f;
        EACH_CH {
            band[0][ch] = bq_run(&f[0], &m->z[ch][0], x[ch]);
            band[2][ch] = bq_run(&f[1], &m->z[ch][1], x[ch]);
            band[1][ch] = x[ch] - band[0][ch] - band[2][ch];
        }
        for (int k = 0; k < 3; ++k) {
            const float pw = band[k][0] * band[k][0] + band[k][1] * band[k][1];
            m->env[k] += (pw - m->env[k]) * c[0];
            db[k] = 10.f * log10f(m->env[k] + 1e-12f);
            mean += db[k] / 3.f;
        }
        x[0] = 0.f;
        x[1] = 0.f;
        for (int k = 0; k < 3; ++k) {
            /* Target: lows `tilt` above the average, highs `tilt` below. */
            const float want = mean + (float)(1 - k) * p[1];
            const float g =
                mean > -70.f ? db2lin(clampf((want - db[k]) * c[1], -p[2], p[2])) : 1.f;
            EACH_CH x[ch] += band[k][ch] * g;
        }
        break;
    }
    case AH_ADV_HEADROOM:
        EACH_CH x[ch] *= c[0];
        break;
    case AH_ADV_MONO: {
        const float mid = 0.5f * (x[0] + x[1]);
        EACH_CH x[ch] += c[0] * (mid - x[ch]);
        break;
    }
    case AH_ADV_LIMITER:
        EACH_CH x[ch] *= c[0];
        e->lim.ceil = c[1];
        e->lim.rel = c[2];
        ah_limiter_tick(&e->lim, &x[0], &x[1]);
        break;
    default:
        break;
    }

    *l = x[0];
    *r = x[1];
}

static void run_group(AhAdvancedEngine *e, const AhAdvBank *b,
                      const int *ids, int n, float *l, float *r)
{
    for (int i = 0; i < n; ++i)
        run_slot(e, b, ids[i], l, r);
}

static void run_chain(AhAdvancedEngine *e, const AhAdvBank *b, float *l, float *r)
{
    const int *first = k_tone;
    int nfirst = (int)(sizeof(k_tone) / sizeof(k_tone[0]));
    const int *second = k_color;
    int nsecond = (int)(sizeof(k_color) / sizeof(k_color[0]));
    const int *third = k_space;
    int nthird = (int)(sizeof(k_space) / sizeof(k_space[0]));

    if (b->order == 2) {
        first = k_color;
        nfirst = (int)(sizeof(k_color) / sizeof(k_color[0]));
        second = k_tone;
        nsecond = (int)(sizeof(k_tone) / sizeof(k_tone[0]));
    } else if (b->order == 3) {
        first = k_color;
        nfirst = (int)(sizeof(k_color) / sizeof(k_color[0]));
        second = k_space;
        nsecond = (int)(sizeof(k_space) / sizeof(k_space[0]));
        third = k_tone;
        nthird = (int)(sizeof(k_tone) / sizeof(k_tone[0]));
    }

    run_group(e, b, k_cleanup, (int)(sizeof(k_cleanup) / sizeof(k_cleanup[0])), l, r);
    run_group(e, b, first, nfirst, l, r);
    run_group(e, b, second, nsecond, l, r);
    run_group(e, b, third, nthird, l, r);
    run_group(e, b, k_output, (int)(sizeof(k_output) / sizeof(k_output[0])), l, r);
}

AhAdvancedEngine *ah_advanced_create(void)
{
    AhAdvancedEngine *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    atomic_init(&e->idx, 0);
    atomic_init(&e->busy, -1);
    e->rng = 0xC001D00Du;
    ah_limiter_init(&e->lim, 48000, -1.f, 2.f, 40.f, true);
    e->gm_g = 1.f;
    AhAdvancedState s;
    ah_advanced_defaults(&s);
    design_bank(&e->banks[0], &s, 48000);
    return e;
}

void ah_advanced_destroy(AhAdvancedEngine *eng)
{
    free(eng);
}

void ah_advanced_reset(AhAdvancedEngine *eng)
{
    if (!eng)
        return;
    memset(eng->mem, 0, sizeof(eng->mem));
    memset(&eng->spatial, 0, sizeof(eng->spatial));
    memset(&eng->room, 0, sizeof(eng->room));
    memset(&eng->hall, 0, sizeof(eng->hall));
    memset(eng->ir_buf, 0, sizeof(eng->ir_buf));
    memset(eng->xdel, 0, sizeof(eng->xdel));
    memset(eng->tape, 0, sizeof(eng->tape));
    ah_limiter_init(&eng->lim, eng->rate ? eng->rate : 48000, -1.f, 2.f, 40.f, true);
    memset(eng->spec_e, 0, sizeof(eng->spec_e));
    memset(eng->spec_z, 0, sizeof(eng->spec_z));
    eng->xpos = 0;
    eng->ir_pos = 0;
    eng->tape_pos = 0;
    eng->gm_g = 1.f;
    eng->peak = 0.f;
    eng->clip_hold = 0.f;
}

void ah_advanced_apply(AhAdvancedEngine *eng, const AhAdvancedState *s, uint32_t rate)
{
    if (!eng || !s)
        return;
    const int wi = ah_ui_slot(&eng->idx, &eng->busy);
    AhAdvancedState tmp = *s;
    ah_advanced_clamp(&tmp);
    design_bank(&eng->banks[wi], &tmp, rate ? rate : 48000);
    atomic_store(&eng->idx, wi);
}

void ah_advanced_process_block(AhAdvancedEngine *eng, float *left, float *right,
                               uint32_t n, uint32_t rate)
{
    if (!eng || !left || !right || n == 0)
        return;

    const AhAdvBank *b = &eng->banks[ah_rt_acquire(&eng->idx, &eng->busy)];

    if (rate > 0 && rate != eng->rate) {
        eng->rate = rate;
        ah_advanced_reset(eng);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    float peak = 0.f;
    float dry_acc = 0.f, wet_acc = 0.f, corr = 0.f, energy = 0.f;
    float spec[AH_ADV_SPEC];
    memcpy(spec, eng->spec_e, sizeof(spec));

    for (uint32_t i = 0; i < n; ++i) {
        float l = left[i];
        float r = right[i];
        float dry_l = l, dry_r = r;
        dry_acc += dry_l * dry_l + dry_r * dry_r;

        if (!b->ab)
            run_chain(eng, b, &l, &r);

        wet_acc += l * l + r * r;
        corr += l * r;
        energy += 0.5f * (l * l + r * r);
        float p = fmaxf(fabsf(l), fabsf(r));
        if (p > peak)
            peak = p;

        const float mono = 0.5f * (l + r);
        for (int k = 0; k < AH_ADV_SPEC; ++k) {
            const float y = fabsf(bq_run(&b->spec_bp[k], &eng->spec_z[k], mono));
            spec[k] += 0.0005f * (y - spec[k]);
        }

        left[i] = l;
        right[i] = r;
    }

    if (b->gain_match && !b->ab) {
        float g = (wet_acc > 1e-10f) ? sqrtf(dry_acc / wet_acc) : 1.f;
        g = clampf(g, 0.4f, 2.f);
        const float g0 = eng->gm_g;
        eng->gm_g += 0.18f * (g - eng->gm_g);
        const float step = (eng->gm_g - g0) / (float)n;
        wet_acc = 0.f;
        corr = 0.f;
        energy = 0.f;
        peak = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            g = g0 + step * (float)(i + 1);
            left[i] *= g;
            right[i] *= g;
            wet_acc += left[i] * left[i] + right[i] * right[i];
            corr += left[i] * right[i];
            energy += 0.5f * (left[i] * left[i] + right[i] * right[i]);
            float p = fmaxf(fabsf(left[i]), fabsf(right[i]));
            if (p > peak)
                peak = p;
        }
    }
    ah_rt_release(&eng->busy);

    memcpy(eng->spec_e, spec, sizeof(spec));
    if (peak > 0.99f)
        eng->clip_hold = (float)rate * 0.8f;
    else if (eng->clip_hold > 0.f)
        eng->clip_hold -= (float)n;
    eng->peak += 0.25f * (peak - eng->peak);
    eng->dry_ms = sqrtf(dry_acc / (float)(n * 2u) + 1e-12f);
    eng->wet_ms = sqrtf(wet_acc / (float)(n * 2u) + 1e-12f);
    eng->corr_n = corr;
    eng->corr_d = energy;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double used = (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    double budget = rate > 0 ? (double)n / (double)rate : 1.0;
    float cpu = (float)(used / (budget > 1e-9 ? budget : 1e-9));
    eng->cpu += 0.2f * (cpu - eng->cpu);

    atomic_store(&eng->pub_peak, (int)(eng->peak * 1000.f));
    atomic_store(&eng->pub_clip, eng->clip_hold > 0.f ? 1 : 0);
    atomic_store(&eng->pub_cpu, (int)(eng->cpu * 1000.f));
    float corr_n = energy > 1e-12f ? (corr / energy) : 1.f;
    if (corr_n > 1.f)
        corr_n = 1.f;
    if (corr_n < -1.f)
        corr_n = -1.f;
    atomic_store(&eng->pub_corr, (int)(corr_n * 100.f));
    atomic_store(&eng->pub_dry, (int)(eng->dry_ms * 1000.f));
    atomic_store(&eng->pub_wet, (int)(eng->wet_ms * 1000.f));
    for (int k = 0; k < AH_ADV_SPEC; ++k) {
        /* -60..0 dB onto the bar height. */
        const float v = clampf((20.f * log10f(spec[k] + 1e-9f) + 60.f) / 60.f, 0.f, 1.f);
        atomic_store(&eng->pub_spec[k], (int)(v * 1000.f));
    }
}

void ah_advanced_meters(const AhAdvancedEngine *eng, AhAdvancedMeters *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!eng)
        return;
    out->peak = atomic_load(&eng->pub_peak) / 1000.f;
    out->cpu = atomic_load(&eng->pub_cpu) / 1000.f;
    out->corr = atomic_load(&eng->pub_corr) / 100.f;
    out->dry_rms = atomic_load(&eng->pub_dry) / 1000.f;
    out->wet_rms = atomic_load(&eng->pub_wet) / 1000.f;
    out->clip = atomic_load(&eng->pub_clip) != 0;
    for (int k = 0; k < AH_ADV_SPEC; ++k)
        out->spec[k] = atomic_load(&eng->pub_spec[k]) / 1000.f;
}