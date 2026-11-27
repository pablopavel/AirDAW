# Features waiting for a hands-on test

Built and covered by the engine tests and snapshot checks, but not yet tried with real hardware and real recordings.
When something works, it is removed from this list; when it doesn't, it gets a note and a fix.

Last updated: 2026-11-26

## Rack, reels and icon (new)
- [ ] Insert rack: modules stand side by side in a chassis with wood cheeks; the faceplate finish follows the plugin's category
      (Consoles black/gold, Dynamics/Filters aluminium, Tape/Saturation green, Reverb/Ambience blue, VST3 graphite).
- [ ] IN key bypasses the module (LED goes dark) and still works from a MIDI mapping; the ▾ / ▸ key folds a module to a slim plate and back.
- [ ] Drag a module by its name plate onto another to reorder the chain.
- [ ] Preset readout menu: Default, your saved presets, "Save current settings as a preset…", Delete preset ▸. The readout shows the last preset applied.
- [ ] Rack knobs: drag and mouse wheel; MIDI learn on a rack knob.
- [ ] A hosted VST3 module: "Open plugin window" still opens it.
- [ ] Reels ("Amber Night"): the aluminium reels turn while playing (the left, full one slower) while their highlights stay put;
      the lamp flickers now and then; the small REC lamp under the plate pulses red only while recording; no extra CPU in the DSP readout.
- [ ] App icon (Scope A) on the taskbar, title bar, Alt+Tab and AirDAW.exe in Explorer. A pinned taskbar shortcut may keep the
      old icon until it is unpinned and pinned again (Windows caches icons).

## Layout and menus
- [ ] Insert slot > "Choose Airwindows plugin...": the list shows categories (with counts); each opens to its plugins; favourites stay on top.
- [ ] Dragging the divider between mixer and meters, and between meter / tool columns: follows the mouse smoothly; can be grabbed anywhere along its line (it lights up).
- [ ] The Hit Record Meter's display catches up to its final size when you let go.
- [ ] Deck: readouts centred, the eight menu keys the same size; keys press down, Listen / Meters / Display / MIDI Learn stay lit when on.
- [ ] Takes bar: the ✕ at either end closes it (loop and takes stay); it comes back for a new loop region or a T button.
- [ ] Insert panel: the right ✕ Close sits in the middle of the panel's right edge.

## Comping
- [ ] Record a full take on a track, then loop a region and record passes: each pass plays only in that region, the rest of the song keeps the full take.
- [ ] In the takes bar, click a pass to hear it in the region; click the track's own take (listed first, by time) to hand the region back.
- [ ] Comp two different regions with different passes; both picks play.
- [ ] Right-click a take: "Play on the whole track" / "Play in this loop region only".
- [ ] Crossfade slider (0-50 ms): edges sound smooth, no clicks; 0 ms is a hard cut.
- [ ] "Outside: take" / "Outside: silence".
- [ ] "Clear comp" brings the full take back everywhere (no take is deleted).
- [ ] "Delete unkept" leaves takes used in the comp alone.
- [ ] Punch-in recording over a longer take also plays only in its range.
- [ ] Save, reopen: the comp, outside setting and crossfade come back. Export plays the comp.

## Reference A/B (from step 2 notes)
- [ ] On B, the Hit Record Meter and the listening tools measure the reference; the master strip stays on the mix.
- [ ] Side-by-side MIX / REF levels while both play.
- [ ] Sync: the reference plays, stops and jumps with the song.
- [ ] Each project remembers its own reference (Full Test1 picks up the one used before; a new project starts without one).

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

## Not built yet (roadmap)
- Cable round-trip latency test and the lowest crackle-free buffer size (needs a patch cable from an output to an input).
