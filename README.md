# AirDAW (work in progress)
Lightweight open-source DAW built around the Airwindows effects. License: AGPLv3, Copyright (C) 2026 trashmannightcrawler (see NOTICE.md).

## Layout
- `core/`   audio engine + Airwindows host (no GUI / audio-driver dependency; builds and tests headless)
- `tests/`  engine tests + full-load benchmark (`engine_tests`)
- `third_party/airwin2rack`  Airwindows Consolidated (MIT), compiled in-process; 514 effects via `AirwinRegistry`
- `third_party/JUCE`         GUI, audio devices (ASIO/WASAPI/JACK/ALSA), file I/O (next milestone)

## Build (Linux/Windows)
    git clone --recursive <repo> && cd airdaw
    cmake -B build -G Ninja && cmake --build build --target engine_tests && build/engine_tests
Third-party sources are cloned into third_party/ (airwin2rack: github.com/baconpaul/airwin2rack, JUCE: github.com/juce-framework/JUCE).

## Status
Done: 24 tracks (mono/stereo) + 4 buses + master, pan/fader/trim with click-free ramps, mute/solo,
bus routing, 8 inserts per channel, lock-free hot insert/swap/remove, denormal protection, dynamic parameters.
App shell (app/): JUCE window with device selector (ASIO/WASAPI/JACK/ALSA), live buffer/round-trip/DSP readout, test tone, master meter. NOT yet compiled or run: needs a machine with full JUCE dependencies (Windows: Visual Studio; set -DAIRDAW_ASIO_SDK=path).
Recording (core/): lock-free disk writer, 32-bit float WAV, transport with punch in/out and loop-pass takes (each loop pass = own take file), take playback; tests verify bit-exact audio and sample-accurate punch/loop points.
Sessions (core/): JSON `.airdaw` files (mixer, insert parameters, takes, loop/punch), atomic save with `.bak`; timed auto-save (30 s to 1 h, rolling copies, skips while recording, skips if unchanged, crash-recovery prompt at startup). Tested headless.
App window: transport, arm 1-2, session New/Open/Save, auto-save toggle + interval. Syntax-checked against JUCE only (no linking or running here).
VST3 hosting (app/Vst3Insert.h, PluginWindow.h): any VST3 can sit in an insert slot (stereo in/out, state saved in sessions); the app has a "Hit Record Meter..." button that loads it into master insert 8 and opens its own GUI. Engine side tested with a stand-in plugin; the JUCE hosting code is syntax-checked only (no real VST3 available here).
Multi-core (core/work_pool.*): tracks and buses are processed in parallel on a real-time-safe fork/join pool (atomics only on the hot path; workers park when idle). Summing happens in fixed order, so output is bit-identical for any thread count (tested). `Engine::setWorkerThreads(-1)` = cores-1 (max 7). The app enables it (cores-1 workers). Verified clean under ThreadSanitizer (`AIRDAW_QUICK=1` runs the suite short for sanitizers).
Next: GUI port of the mockup, mixer controls in the app.

## Decisions
- Hit Record Meter is not part of Consolidated; it is hosted as a VST3 via JUCE plugin hosting (download from github.com/airwindows/Meter/releases: VST3 for Windows/Linux/Mac; then use the app's Hit Record Meter button).
- Single-threaded engine hit ~96% of a core at 108 inserts, 96 kHz, 64-sample blocks on a slow test box: multithreading is planned.
