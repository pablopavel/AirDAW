# AirDAW (work in progress)
Lightweight open-source DAW built around the Airwindows effects. License: AGPLv3, Copyright (C) 2026 trashmannightcrawler (see NOTICE.md).

## Layout
- `core/`   audio engine, Airwindows host, sessions, recording, export, loudness analysis (no GUI / audio-driver dependency; builds and tests headless)
- `app/`    the JUCE application: deck, mixer, insert panel, meter dock / reference suite, Listen mode, Display and Plugins panels, MIDI Learn
- `tests/`  engine tests + full-load benchmark (`engine_tests`)
- `fxlib/`  standalone build of the Airwindows library as `AirwindowsFX.dll` (used by the release workflow in `.github/workflows/`)
- `docs/`   design mockup (`airdaw-v4.html`, `AIRDAW_DESIGN_HANDOFF.md`), Windows build guide, `TO_TEST.md` (features waiting for a hands-on test)
- `third_party/airwin2rack`  Airwindows Consolidated (MIT), 514 effects; `third_party/JUCE`  GUI, audio devices, file I/O

## Build
Windows (Visual Studio 2022 or newer, x64 Native Tools prompt): see [docs/WINDOWS_BUILD.md](docs/WINDOWS_BUILD.md).

    git clone --recursive https://github.com/pablopavel/AirDAW && cd AirDAW
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAIRDAW_ASIO_SDK=C:\dev\asiosdk
    cmake --build build && build\engine_tests.exe

Without `--recursive`, `setup_third_party.sh` fetches airwin2rack and JUCE. The app is `build/AirDAW_artefacts/Release/AirDAW.exe`.

## Status
Runs on Windows 11 with ASIO (tested with a Black Lion Revolution 2x2 at 96 kHz / 64 samples).

- **Engine:** 24 tracks (mono/stereo), 4 buses, master; 8 inserts per channel (Airwindows or VST3), reorderable; fader, pan,
  gain, mute, solo, polarity; click-free ramps; lock-free insert changes; multi-core processing with bit-identical
  output for any thread count; metronome (never recorded) with count-in and tap tempo.
- **Recording:** lock-free disk writer (32-bit float WAV), punch in/out, loop passes as separate takes, input monitoring on
  armed tracks. Takes bar: keep / delete unkept, and **comping** (pick a take per loop region, adjustable crossfades,
  outside the regions the track's take or silence).
- **Projects:** JSON `.airdaw` sessions, one folder per project (audio, autosave), timed auto-save with recovery, Save As
  moves takes into the project, double-click a project file to open it. Mix export (WAV 24/32-bit, song or loop range).
- **Mixing UI:** the approved mockup ported to JUCE: deck, song overview, mixer strips, insert rack (500-series style
  modules, faceplate finish by plugin category) with every plugin parameter, presets, favourites and Airwindows
  write-ups. "Amber Night" tape reels in the deck; "Scope A" application icon (`AirDAW.exe --render-icon resources/icon`).
- **Listening:** Hit Record Meter (VST3) in the meter dock, reference suite and Listen mode sharing 12 tools (stereo field,
  31-band RTA, loudness with EBU R128 / true peak, target loudness, tonal balance, spectrogram, loudness history, band
  phase/width, headroom, listening notes, ear-break timer, reference A/B). Monitor section (Mono, Dim, Pol, L/R/Mid/Side).
  Reference A/B with level matching, own transport or song sync, side-by-side levels; each project remembers its reference.
- **Display panel:** eras and classic looks, accent and glow, readout colours, faceplates, tints, corners, screws, knob /
  fader / VU materials, reels, side trim (woods, rack ears, tolex, chrome), typefaces, OLED / standard, scale, pixel shift.
- **Plugins panel:** Airwindows can be updated without a new AirDAW: a weekly GitHub workflow builds the newest Airwindows as
  `AirwindowsFX.dll` and publishes it as a release; AirDAW checks, downloads and verifies (SHA-256) on request, activates it
  at the next launch and keeps older builds for roll back. The built-in copy is always the fallback.
- **MIDI Learn:** every control (and every plugin parameter) can be mapped; fixed knobs pick up, endless knobs are detected;
  DAW mappings are saved app-wide, plugin mappings per project.
- **Robustness:** the audio device reconnects after a dropout and a stuck driver never freezes the window; a log of startup,
  devices and MIDI is written to `%APPDATA%\AirDAW\AirDAW.log`.

Features built but not yet tried with real hardware are listed in [docs/TO_TEST.md](docs/TO_TEST.md).
Next: cable round-trip latency test and the lowest crackle-free buffer size.

## Decisions
- Hit Record Meter is not part of Consolidated; it is hosted as a VST3 via JUCE plugin hosting (download from
  github.com/airwindows/Meter/releases, then "Load Hit Record Meter..." in the meter dock).
- Airwindows effects run in-process from one table (`core/include/airdaw/fx_library.h`) whether they come from the
  built-in copy or `AirwindowsFX.dll`, so both behave identically (tested bit for bit).
- The engine never allocates, locks or does I/O on the audio thread; anything the audio thread reads (takes, comps, the
  reference) is replaced as a whole and kept alive, never edited in place.
