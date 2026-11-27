# Features waiting for a hands-on test

Built and covered by the engine tests and snapshot checks, but not yet tried with real hardware and real recordings.
When something works, it is removed from this list; when it doesn't, it gets a note and a fix.

Last updated: 2026-11-27

## Reference A/B (from step 2 notes)
- [ ] Sync: the reference plays, stops and jumps with the song.
- [ ] Each project remembers its own reference (Full Test1 picks up the one used before; a new project starts without one).

## Tool sizes per view (new)
- [ ] Resize the reference suite tools (and the meter column) in the main view, switch to Listen mode and resize them
      there: each view keeps its own sizes, also after a restart. Listen mode starts from the main view's sizes.

## Metronome defaults (new)
- [ ] Set the click sound, beats per bar, count-in and click level (Audio settings, or the Click key's right-click menu),
      then restart AirDAW or make a new project: they start the same. A saved project still opens with its own.

## Undo / redo (new)
- [ ] A plugin you didn't touch keeps running through an undo (no dropout). Undo after recording takes the new take
      out of the list (its file stays); redo brings it back. Not while recording.
- [ ] Opening or starting a project starts a fresh history. Typing in a name box: Ctrl+Z undoes the typing only.

## Mixer extras
- [ ] Channel polarity (⌀) on tracks and buses: flips the sound; takes recorded with ⌀ on play back normally.
- [ ] Tap tempo (deck button and Audio settings) with the click on.

## MIDI Learn
- [ ] Fixed knobs / faders (Samson, Korg): the control waits until the knob passes its value, then follows.
- [ ] Pads / buttons on the Samson and Korg (Play, mute, solo, arm, monitor buttons).
- [ ] A plugin knob mapping is saved with the project and comes back after reopening it.
- [ ] Mappings survive a restart of AirDAW; the Mappings window can change a mapping's mode and remove it.

## Plugins panel
The first plugin-library release exists (airwindows-2026.11.22-91b3343, the same build as the built-in copy), and a
check from AirDAW against GitHub answers "up to date". Installing can only be tried once Airwindows publishes something
newer (the workflow checks every Monday).
- [ ] When Plugins shows an amber dot / "Update available": Install update, restart AirDAW, the panel shows the new build.
- [ ] Roll back, restart: the previous build is back.

## Audio device
- [ ] If the interface drops out (USB), AirDAW reconnects by itself; if the driver is stuck, the footer says so and the window stays responsive.
- [ ] Switching the audio driver / sample rate with the Hit Record Meter loaded no longer crashes.

## Projects
- [ ] Double-clicking a .airdaw file opens that project; with AirDAW already open, it opens in the running window.

## Deferred (not testing VST3 plugins for now)
- [ ] A hosted VST3 module: "Open plugin window" still opens it.

## To review later
- Hit Record Meter (i) guide: review the wording (app/ui/tools/HitMeterInfo.h).

## Not built yet (roadmap)
- Cable round-trip latency test and the lowest crackle-free buffer size (needs a patch cable from an output to an input).
