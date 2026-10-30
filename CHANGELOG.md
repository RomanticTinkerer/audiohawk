# Changelog

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
