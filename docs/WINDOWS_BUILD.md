# AirDAW: Windows build and first-run guide

How to build AirDAW on Windows, check that the engine tests pass, and get a Black Lion Revolution 2x2 (or another
interface) running through ASIO at low latency. AirDAW builds and runs on Windows 11 with Visual Studio 2022 Build Tools;
Claude Code on your PC is the fastest way to fix anything that breaks (see "Using Claude Code" at the end).

## 1. Install the tools (one time)

1. **Visual Studio 2022 Community** or newer (free): https://visualstudio.microsoft.com/downloads/
   In the installer choose the workload **Desktop development with C++**. It includes the compiler, the Windows SDK,
   CMake and Ninja. (Build Tools only is fine too.)
2. **Git for Windows**: https://git-scm.com/download/win
3. **Black Lion Revolution 2x2 driver**: install the latest Windows driver from Black Lion Audio's support page.
   Open its control panel and set the sample rate to **96 kHz** (AirDAW's default session rate).
4. **Steinberg ASIO SDK** (needed to compile ASIO support): https://www.steinberg.net/developers/asiosdk-open/
   Steinberg now offers it under GPLv3+ (alongside their proprietary license), which fits AirDAW's AGPLv3.
   Download it and unzip to e.g. `C:\dev\asiosdk`. Inside you should see a `common` folder containing
   `asio.h` and `iasiodrv.h`. (If the layout differs, note it; the build only needs the folder holding those headers.)
5. Optional: **Claude Code** for Windows, to fix build errors interactively.

## 2. Get the source

Clone it with the libraries (PowerShell):

    git clone --recursive https://github.com/pablopavel/AirDAW C:\dev\airdaw

From a zip without the libraries, fetch them instead:

    cd C:\dev\airdaw
    git clone --depth 1 https://github.com/juce-framework/JUCE third_party/JUCE
    git clone --depth 1 https://github.com/baconpaul/airwin2rack third_party/airwin2rack

## 3. Build

Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu (this puts the compiler on your PATH), then:

    cd C:\dev\airdaw
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAIRDAW_ASIO_SDK=C:\dev\asiosdk
    cmake --build build --target engine_tests

The first build compiles about 2,100 Airwindows source files: expect **10 to 30 minutes**. Later builds are fast.
If CMake prints "AIRDAW_ASIO_SDK not set", the path was wrong and ASIO will be missing.

Run the engine tests (no audio hardware needed):

    build\engine_tests.exe

You want `ALL TESTS PASSED`. The output also prints your machine's real full-load benchmark:
the line starting `workers=-1` shows how much of the 0.667 ms block budget (96 kHz, 64 samples) the
heaviest session uses on your CPU. Please keep that line.

Then build the app:

    cmake --build build --target AirDAW

The program is `build\AirDAW_artefacts\Release\AirDAW.exe`.

## 4. First-run checklist (Black Lion Revolution 2x2)

Run `AirDAW.exe` and work down this list. Note what works and what does not.

1. **Device:** click the rate / latency / DSP readouts at the top right (or **Project > Audio settings**). Choose type
   **ASIO** and device **Black Lion Audio Driver** (that is the Revolution 2x2's ASIO driver name).
   Set sample rate **96000** and buffer **64** (try 128 if you hear crackles). The readouts show rate, buffer, latency
   and DSP %, and the footer shows the device. AirDAW remembers the device for the next launch.
   If ASIO is missing from the type list, the SDK path was not applied (re-run the cmake configure step).
2. **Output:** in Audio settings tick **Test tone on track 1**. You should hear a clean 440 Hz tone and see the meter move.
3. **Input and recording:** plug a mic or instrument into input 1, press **R** on track 1 (it picks In 1), press **Rec**,
   play for a few seconds, press **Stop**. Look in `Documents\AirDAW\Untitled\audio\`: you should find
   `TRACK_1_take001.wav`. Press **|<** then **Play** to hear it back.
4. **Save and auto-save:** **Project > Save As...** creates a project folder; turn on **Auto-save every** in Audio settings
   and check that an `autosave` folder appears in the project after the interval.
5. **Hit Record Meter:** download the VST3 from https://github.com/airwindows/Meter/releases, press
   **Load Hit Record Meter...** in the meter dock, select the `.vst3` file. Its display should react to audio.
6. **Latency reality check (optional but valuable):** connect output 1 to input 1 with a cable, record a click,
   and measure the offset in any audio editor. That is your true round trip.

## 5. What to send back

- The `workers=-1` benchmark line and `ALL TESTS PASSED` (or the failure text).
- The status line text from step 1 (driver, buffer, round-trip, DSP%).
- Anything that failed, with the exact error message or a screenshot.
- Whether crackles/dropouts appear at 64 samples, and at what buffer they stop.

## Troubleshooting

- **`cmake` not found:** you are not in the "x64 Native Tools Command Prompt for VS 2022".
- **Linker error mentioning `RuntimeLibrary` / `MT_StaticRelease` vs `MD_DynamicRelease`:** delete the `build` folder
  and configure again; all targets are meant to use the static runtime.
- **ASIO device missing or fails to open:** close other programs using the interface, confirm the Black Lion control
  panel shows the same sample rate, and try 128 or 256 samples. The device is listed as **Black Lion Audio Driver**.
- **AirDAW is slow to open, then has no audio** (footer: "AUDIO DEVICE NOT ANSWERING", log: "Can't create i/o buffers"):
  the interface's driver is stuck, typically after a USB hiccup such as plugging another device into the same hub.
  Close every audio program, unplug the interface (or switch it off) for 10 seconds, plug it back in, and choose it under
  Audio. If that does not help, restart Windows. Plug MIDI controllers into a different USB port than the interface.
- **Something odd at startup, with devices or MIDI:** AirDAW writes `%APPDATA%\AirDAW\AirDAW.log` at every launch (how
  long the audio device took to open, errors, MIDI controllers switched on). Include it when reporting a problem.
- **Crackles at low buffer sizes:** try raising the buffer first; then tell us the DSP% shown.
- **VST3 will not load:** note the message shown in the notice line; some plugins need 64-bit builds and the
  correct folder structure (the whole `.vst3` folder or file must be selected).

## Using Claude Code on your PC

Install Claude Code, open a terminal in `C:\dev\airdaw`, start `claude`, and give it this brief:

> This is AirDAW, a C++ JUCE and CMake project. Follow docs/WINDOWS_BUILD.md. Build with the commands in section 3 using
> the VS 2022 x64 native tools environment, fix any compile or link errors on Windows (keep changes minimal and keep
> `build\engine_tests.exe` passing), then walk me through the first-run checklist in section 4. Do not change
> the audio engine design without asking.
