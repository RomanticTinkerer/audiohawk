# AudioHawk

PipeWire equalizer and listening-profile controller for GNU/Linux.

AudioHawk is written in **C** (core + GTK UI + launcher) with a **C++/Qt** UI for Plasma. The launcher picks the toolkit from your desktop environment:

| Desktop | UI |
|---------|----|
| GNOME, Cinnamon, MATE, XFCE, Budgie, Pantheon, … | GTK4 + libadwaita |
| KDE Plasma, LXQt, Deepin, … | Qt6 |

Force a toolkit with `AUDIOHAWK_UI=gtk` or `AUDIOHAWK_UI=qt`.

## Dashboard (this release)

- **EQ Settings** on/off — arms a PipeWire filter-chain virtual sink (`audiohawk.eq`)
- **Profiles** (horizontal scroll): Music, Movie/Video, Game, Work, Casual, Mood
- **Intelligent Equalizer**: Off, Detailed, Warm, Balanced
- **Per App Audio Profiles**: auto-switch, per-device memory, manage/seed app mappings
- **Start on system startup** — XDG autostart (Settings); launches hidden with `--background`
- **Suspend survival** — reconnects to PipeWire after sleep and re-arms EQ automatically

## Dependencies

**Required**

- CMake ≥ 3.20
- GCC/Clang with C11 + C++17
- PipeWire (`libpipewire-0.3` / `pipewire-devel`)

**GTK UI**

- `gtk4-devel` + `libadwaita-devel` (Fedora)
- `libgtk-4-dev` + `libadwaita-1-dev` (Debian/Ubuntu)

**Qt UI**

- `qt6-qtbase-devel` (Fedora) / `qt6-base-dev` (Debian/Ubuntu)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

If GTK packages are missing, CMake still builds the core, launcher, and Qt UI.

```bash
# Fedora example for the GTK binary
sudo dnf install gtk4-devel libadwaita-devel
```

## Run

```bash
./build/audiohawk          # auto UI
./build/audiohawk-qt       # Qt dashboard
./build/audiohawk-gtk      # GTK dashboard (when built)
```

Turn **Equalizer** on. AudioHawk creates a virtual sink and moves output streams to it via PipeWire metadata (EasyEffects-style), then runs EQ/effects into your real speakers. The system default sink is left alone.

Closing the window keeps AudioHawk running so EQ stays active. After suspend/resume, AudioHawk reconnects to PipeWire and re-arms the graph automatically.

Enable **Start on system startup** in Settings to launch hidden at login (`--background` via XDG autostart).

## Package (.deb)

```bash
./scripts/build-deb.sh
sudo dpkg -i dist/audiohawk_0.1.0_amd64.deb
```

After install, AudioHawk appears in GNOME Overview / KDE Application Launcher as **AudioHawk** — *A simple audio enhancer.* Closing the window keeps it in the system tray (Show / Quit from the tray menu).

## Stereo virtual surround

Enable **Stereo Surround Virtualizer** under GTK Settings, or **Stereo surround**
in the Qt dashboard. Spatial strength defaults to 50%. Bass and treble lift each
default to +25% amplitude, approximately +1.94 dB, and can be adjusted independently
from 0 to 100% extra amplitude in 5% steps. These shelves add to the existing EQ
and enhancer tuning only while surround is enabled. Strength at zero removes
spatial distribution but keeps the selected tone lift; disable surround to bypass both.
Existing configuration files retain their surround on/off setting and receive the
new defaults when these keys are absent.

The processor preserves the direct left and right signals and adds band-limited
width, a 0.32 ms opposite-ear delay, and a 12 ms opposite-channel room reflection.
Filtering the added signal approximates the quieter, darker sound reaching the
opposite ear. The channels use identical processing, keeping centered vocals centered.
The previous implementation high-passed the entire side signal, losing stereo bass,
and applied reflections even with surround disabled. This version filters only the
added spatial signal and fades surround on/off with a 20 ms time constant.

This is a stereo virtualizer inspired by binaural principles, not a Dolby Atmos
decoder, licensed Dolby renderer, or measured HRTF renderer. Dolby describes
[HRTF filtering and spatial metadata](https://professional.dolby.com/categories/mobile/dolby-atmos-for-mobile-devices/)
and [binaural spatialization settings](https://professionalsupport.dolby.com/s/article/What-is-Binaural-Render-Mode-and-how-do-the-settings-affect-my-mix).
AudioHawk receives two channels, so it cannot recover original object positions or
height channels. Headphones provide the most controlled result; speakers also mix
acoustically in the room.

There is no fixed attenuation in the new spatial stage. A shared stereo soft ceiling
reduces peaks above 0.97 to keep sample amplitudes at or below 1.0 while preserving
the L/R ratio. Boosts on already loud material can engage that ceiling, and delayed
signals can interfere at some frequencies, so +25% describes the shelf gain before
limiting, not a guarantee of 25% greater perceived loudness. The ceiling is not a
true-peak oversampling limiter.

Settings persist in `~/.config/audiohawk/effects.conf`:

```ini
surround_virtualizer=true
surround_amount=50
surround_bass=25
surround_treble=25
```

### DSP tests on Windows or GNU/Linux

The portable C DSP tests require no PipeWire or desktop libraries:

```sh
cmake -S . -B build-dsp -DAUDIOHAWK_DSP_TESTS_ONLY=ON
cmake --build build-dsp
ctest --test-dir build-dsp --output-on-failure
```

On GNU/Linux, also build the full application using the earlier commands and run
`ctest --test-dir build --output-on-failure`. Listen to centered speech, hard-panned
audio, bass-heavy stereo, and mono material while toggling surround and adjusting
the three controls. Verify settings after relaunch and check playback after a
device or sample-rate change. The portable tests cover shelf gain, stereo symmetry,
cross-channel output, bass retention, bypass, sample ceilings, and settings migration
at 44.1, 48, 96, 192, and 384 kHz; they do not validate live PipeWire routing or UI rendering.

## Profile intent

| Profile | Curve idea |
|---------|------------|
| Music | Smiling curve — bass + air |
| Movie/Video | Dialog clarity, controlled rumble |
| Game | Competitive presence (footsteps/cues) |
| Work | Speech-first, low fatigue |
| Casual | Soft warmth |
| Mood | Deep lows + airy highs |

Intelligent modes reshape the active profile (detail boost, warmth, or balanced compression).
