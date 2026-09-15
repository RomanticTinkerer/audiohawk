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
