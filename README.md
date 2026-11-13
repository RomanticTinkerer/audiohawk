# AudioHawk

**A simple audio enhancer for GNU/Linux.**

AudioHawk is a PipeWire-based audio equalizer and listening-profile manager, built in C with GTK4 and libadwaita.

## Features

- **Equalizer:** Enable or disable system-wide audio enhancement with one switch.
- **Listening Profiles:** Switch between Music, Movie/Video, Game, Work, Casual, and Mood presets.
- **Intelligent Equalizer:** Choose between Off, Detailed, Warm, and Balanced sound adjustments.
- **Per-App Profiles:** Automatically apply different audio settings to individual applications.
- **Device Memory:** Remember your application audio preferences for different output devices.
- **System Pitch:** Adjust the pitch of routed system audio by up to 15 semitones in either direction.
- **Virtual Surround:** Add a wider, more spatial sound to ordinary stereo audio.
- **Automatic Recovery:** AudioHawk reconnects and restores your audio setup after your computer wakes from sleep.
- **Startup Support:** Start AudioHawk automatically when you log in.
- **Persistent Settings:** Your preferences are saved between sessions.

## Listening Profiles

AudioHawk comes with six profiles, each designed around a different listening experience.

| Profile | Designed for |
|---|---|
| Music | Rich bass and clearer highs |
| Movie/Video | Clear dialogue and controlled bass |
| Game | Greater emphasis on important in-game sounds |
| Work | Comfortable, speech-focused audio |
| Casual | A softer, warmer sound |
| Mood | Deeper bass and brighter highs |

You can also use Intelligent Equalizer modes to further adjust your selected profile.

## Getting Started

### Requirements

- GNU/Linux
- PipeWire
- CMake 3.20 or newer
- GCC or Clang with C11 support

For the graphical interface:

- GTK4
- libadwaita

### Install

The easiest way to install AudioHawk is through its installation script.

```bash
curl -fsSL https://github.com/RomanticTinkerer/audiohawk/releases/latest/download/install.sh | sudo sh
```

The installer automatically selects the appropriate package for supported distributions.

AudioHawk supports:
- Debian and Ubuntu-based distributions (`.deb`)
- Fedora and other RPM-based distributions (`.rpm`)
- Other distributions through a portable installation

Run the same command again to update an existing installation.

You can also download packages directly from [GitHub Releases](https://github.com/RomanticTinkerer/audiohawk/releases).

### Build from Source

Install the required development dependencies, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Launch AudioHawk:

```bash
./build/audiohawk
```

Or launch the graphical dashboard directly:

```bash
./build/audiohawk-gtk
```

## How It Works

AudioHawk creates its own virtual audio output using PipeWire.

When you enable the equalizer, your audio is routed through AudioHawk before reaching your speakers or headphones. This allows AudioHawk to apply your selected sound settings without changing your system's default audio device.

You can close the dashboard without stopping AudioHawk. It continues
