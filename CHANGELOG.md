# Changelog

## 0.3.2b - 2026-11-05

### Added
- About tab in the dock with the developers, GitHub contributors, and credits for the projects and papers AudioHawk builds on
- Reset Configs button in the top-right corner of every tab, plus a Reset Configs item in the tray menu. It asks first, then puts profiles, EQ curves, effects, Advanced settings, and per-app mappings back to their defaults. EQ power and start-on-login stay as they are

### Fixed
- The Work profile description shows up again (a stray `&` was breaking the label)

## 0.3.2 - 2026-11-05

### Fixed
- Flat EQ is really flat now: the hidden body/air filters and the fixed +3.6 dB makeup gain are gone, so audio passes through bit-exact
- The graphic EQ hits the curve it draws (+6 dB on every band used to come out as +10 dB)
- Pitch shifting no longer clicks, and turning it on or off fades instead of cutting
- Dragging sliders can no longer swap filters halfway through an audio block
- The Advanced limiter's true-peak check works and its gain moves smoothly

### Changed
- An automatic preamp keeps EQ and enhancer boosts from clipping, and a lookahead limiter replaces the soft clipper
- Output is quieter than 0.3.1 because boosts no longer clip; raise the system volume or Volume Boost to match
- Bass, mid, treble, and dialogue enhancers use honest levels; dialogue lifts only the center and bass gets a rumble filter
- The volume leveler rides loudness over seconds instead of pumping
- The Game profile favors footsteps and positional cues instead of heavy bass

## 0.3.1 - 2026-10-30

### Changed
- Advanced effects now use studio-grade DSP: Bauer bs2b HighEC crossfeed, ITU-R BS.1770-5 K-weighted loudness, soft-knee RMS compression, and a lookahead true-peak limiter
- Analog, tube, tape, and distortion use anti-aliased saturation so harmonics stay cleaner
- Dynamic bass, de-esser, multiband compressor, and transient shaper follow EasyEffects-style time constants
- Gain matching is smoothed over each block so it no longer pumps

## 0.3.0 - 2026-10-30

### Added
- Advanced dock tab with Enhancement, Fun, Processing, and Monitor pages
- Per-effect bypass and amount for tone, dynamics, loudness, stereo, cleanup, and space effects
- Fun character effects: analog warmth, vintage Hi-Fi, tape, tube, vinyl, distortion, lo-fi
- Room, hall, convolution reverb, 3D spatial, and stereo widening
- Monitor tools: spectrum, clip light, A/B compare, gain matching, CPU, chain presets, processing order, per-app chains

### Changed
- Advanced settings are grouped by category instead of one long list

## 0.2.0 - 2026-10-29

### Added
- System pitch control from -15 to +15 semitones (Settings; Equalizer must be on)
- Draggable equalizer curve view
- One-command curl installer and a binary tarball
- RPM and AppImage packages

### Changed
- GTK4 is the only UI. The Qt dashboard is gone
- System tray uses the full hawk SVG instead of a cropped PNG

## 0.1.0 - 2026-09-27

First packaged release: PipeWire equalizer, listening profiles, GTK and Qt UIs, stereo surround.
