# Features waiting for a hands-on test

Built and covered by the engine tests and snapshot checks, but not yet tried with real hardware and real recordings.
When something works, it is removed from this list; when it doesn't, it gets a note and a fix.

Last updated: 2026-11-27

## Controls, inserts and layout (new)
- [ ] Menu keys in the deck are grey console buttons that light in the accent while pressed; Listen / Meters / Display /
      MIDI Learn stay lit when on. No square glow round a lit key.
- [ ] Bigger AIRDAW logo; no tape reels in the deck any more (the position readout takes their place).
- [ ] Display > Hardware > Buttons: Console (coloured caps, M amber, S blue, monitor green, T violet, R red, dim when
      off), Toggles (strip M / S / monitor / T / R as bat-handle switches with a label and LED; Settings tick boxes as switches),
      Classic (the old keys, now the default). Each switch still toggles and still answers MIDI Learn.
- [ ] Knobs: Chrome, Knurled, Skirted, Bakelite, Soft touch, Ivory, Gold (gain knobs and insert-card knobs). Faders: Chrome,
      Black, Console grey, Cream, Red, Wood (grooved caps with a soft shadow).
- [ ] Typefaces: Modern, Industrial, Condensed, Grotesque, Humanist, Engraved, Typewriter, Tape label all look different;
      nothing important is cut off with the wider ones.
- [ ] Each strip (and the master) has one INSERTS button: the number of plugins on the right, LED lit while one runs,
      outlined while the insert panel shows that channel, the plugin list in its tooltip; click opens the panel.
- [ ] More than 8 inserts on one channel (up to 32): load 10 or more, they all process, save and reopen keeps them all.
      Reordering a long chain by drag still works.
- [ ] Insert panel: a plain X at the left and one at the right (centred in its column, halfway down); no "AIRDAW 500 / SLOTS"
      text on the rack rail.
- [ ] With the insert panel open, the meter dock stays (when the window has room) and its divider resizes it; that height is
      remembered separately from the height without the insert panel.
- [ ] Dragging the dividers between reference tools: no tool slides off the right end; the last tool stops at its minimum width.

## Hit Record Meter (new)
- [ ] The meter is always the last master insert: add effects to the master and they go before it (the add card sits
      in front of the meter); drag the meter or another module around and the meter goes back to the end.
- [ ] A project saved with the meter in an earlier master slot: on opening, the meter moves to the end, the other
      effects keep their order, and there is only one meter.
- [ ] "Dark view" in the meter dock header: the Hit Record Meter drawn dark (same layout, scales, dots, beam lines,
      letter grades, score and advice as the plugin; the scale, rules and labels in the accent). Compare it with the
      plugin's own display on the same music: the dots and grades should match. Click the view to freeze it,
      double-click to start over. Below about 370 px tall it shows the compact layout (tone colour and big grade),
      like the plugin. Remembered after a restart; works without the plugin loaded.
- [ ] The (i) after "HIT RECORD METER" opens a plain-words guide (what each band measures, how to move each readout,
      the letters, tone bar and hints, tips); it scrolls and closes when you click elsewhere.

## New default view (new)
- [ ] On a fresh install (or after Display > Reset) the look is today's: amber, strong glow, matte faceplate, sharp
      corners, ivory knobs, cream faders, Industrial type, screws, rack inserts, classic buttons. The eras and the
      other classic looks are unchanged.
- [ ] Fresh settings also start with the meter dock at 504 px (469 px with the insert panel open), the dark meter view
      on and 1341 px wide, and the reference suite Stereo field, Listening notes, Loudness, Band phase, Reference A/B.

## Removing tracks (new)
- [ ] Right-click a track's name: Rename…, Show inserts, Remove track…. Remove asks first (it says how many takes leave the project).
- [ ] After removing: the strip is gone, the count on "+ Add track" drops, the track's takes no longer play, its inserts are unloaded.
- [ ] "+ Add track" brings back a removed slot first (as a fresh track), then new ones.
- [ ] Save, reopen: removed tracks stay removed; another project has its own set. The recorded files are still in the project folder.
- [ ] Removing an armed track while recording is refused with a note.

## Rack and icon (new)
- [ ] Display > Hardware > Inserts: Rack (now the default) or Cards (the original insert cards); switching changes an open insert panel straight away
      and is remembered after a restart.
- [ ] Insert rack (Inserts: Rack): modules stand side by side in a chassis with wood cheeks; the faceplate finish follows the plugin's category
      (Consoles black/gold, Dynamics/Filters aluminium, Tape/Saturation green, Reverb/Ambience blue, VST3 graphite).
- [ ] IN key bypasses the module (LED goes dark) and still works from a MIDI mapping; the ▾ / ▸ key folds a module to a slim plate and back.
- [ ] Drag a module by its name plate onto another to reorder the chain.
- [ ] Preset readout menu: Default, your saved presets, "Save current settings as a preset…", Delete preset ▸. The readout shows the last preset applied.
- [ ] Rack knobs: drag and mouse wheel; MIDI learn on a rack knob.
- [ ] A hosted VST3 module: "Open plugin window" still opens it.
- [ ] App icon (Scope A) on the taskbar, title bar, Alt+Tab and AirDAW.exe in Explorer. A pinned taskbar shortcut may keep the
      old icon until it is unpinned and pinned again (Windows caches icons).
- [ ] Song overview: the playhead's glow sits evenly on both sides of the line while it moves.
- [ ] Backlit transport keys: the symbols are frosted glass lit from below (bright amber when stopped, hotter and slowly breathing
      while playing, red and pulsing while recording, fading between them); only a faint light shows in the gaps and under the keys;
      no glow round the transport. Glow 0 in Display gives plain keys with cream symbols.
- [ ] Loop and Click keys show symbols (loop arrows, metronome) and still toggle loop / click; lit keys show dark symbols.

## Layout and menus
- [ ] Insert slot > "Choose Airwindows plugin...": the list shows categories (with counts); each opens to its plugins; favourites stay on top.
- [ ] Dragging the divider between mixer and meters, and between meter / tool columns: follows the mouse smoothly; can be grabbed anywhere along its line (it lights up).
- [ ] The Hit Record Meter's display catches up to its final size when you let go.
- [ ] Deck: readouts centred, the eight menu keys the same size; keys press down, Listen / Meters / Display / MIDI Learn stay lit when on.
- [ ] Takes bar: the ✕ at either end closes it (loop and takes stay); it comes back for a new loop region or a T button.

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
