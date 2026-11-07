# Changelog

## 0.3.5b - 2026-11-07

### Changed
- Updates install on their own now, with no password. The `.deb` and `.rpm` ship a small update helper plus a polkit rule that lets the person at the desktop run just that helper. AudioHawk restarts the way it was running, so a login update stays hidden in the tray
- Updates install the package with the helper that's already on your system instead of running a script downloaded from the internet as root
- `install.sh` skips the download when you already have the latest version

### Fixed
- A broken apt hook somewhere else on the system no longer makes an update look failed when the package installed fine

## 0.3.5 - 2026-11-07

### Added
- Updates: AudioHawk checks GitHub for a new version each time it starts, including at login. The status shows in the top-left corner of the window, and a notification pops up if AudioHawk started hidden. Click **Update to x.y.z**, type your password, and it installs the new version and restarts
- AppImages update themselves in place, no password needed

### Changed
- The `curl ... install.sh | sudo sh` command installs the `.deb` on apt distros and the `.rpm` on dnf, zypper, or rpm distros, instead of always copying the tarball to `/usr/local`
- The `.deb` recommends `curl` and `pkexec`, which updates use

## 0.3.4 - 2026-11-07

### Added
- 11 new EQ presets in the Equalizer tab: Hip-Hop, R&B, Metal, Piano, Loudness, Spoken Word, Cinema, Gaming, Late Night, Small Speakers, and Bass Reducer

### Changed
- The chosen preset is saved by name, so adding presets later can't scramble it. Older configs still load, and Custom stays Custom

## 0.3.3 - 2026-11-07

### Fixed
- EQ and effects kick in right away at login. If PipeWire was still starting, AudioHawk used to flip the EQ switch off while audio still ran through it, so nothing matched until you poked a few buttons
- Switching outputs (headphones, Bluetooth, HDMI) while the EQ is on now moves the sound to the new device instead of leaving it on the old one
- Room and Hall reverb no longer bleed into each other or smear the left and right channels
- The stereo correlation meter reads 1.0 for mono audio (it used to top out at 0.5)
- The Advanced spectrum shows real per-band levels
- Gain match ramps smoothly instead of stepping once per audio block

### Changed
- Advanced tab: the mouse wheel scrolls the page instead of nudging sliders, tab titles show how many effects are on, touching a control switches the preset to Custom, and meters read in dB

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
