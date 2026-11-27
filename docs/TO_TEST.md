# Features waiting for a hands-on test

Built and covered by the engine tests and snapshot checks, but not yet tried with real hardware and real recordings.
When something works, it is removed from this list; when it doesn't, it gets a note and a fix.

Last updated: 2026-11-27

## Full screen (new)
- [ ] Esc leaves full screen straight away, even with a panel open (windowed, Esc closes an open panel).
- [ ] Menus close each other and on a click elsewhere: open Display, Plugins, Audio settings, MIDI mappings or About
      plugin, then open another (the first closes) or click anywhere else in AirDAW (it closes). Clicks inside the open
      panel / window, and in its drop-down lists, keep it open. A plugin's own editor window is not affected.
- [ ] Deck keys read just Listen, MIDI Learn, Plugins, Project, Meters (no small symbols).
- [ ] The Full key is an icon only: corners pointing out when windowed, pointing in when full screen. The Display key is
      a gear icon only (still lit while the Display panel is open). Both keys are narrow; the other keys share the row.

## Tempo, click and tempo map (new)
- [ ] Tap key: tapping still sets the tempo; Shift + drag gives 0.1 BPM steps; the mouse wheel changes it by 1 BPM (Shift: 0.1). After a drag or scroll the next taps start fresh.
- [ ] Click sounds: Beep (as before), Woodblock, Clave, Cowbell, Stick, Hi-hat, Soft; beat 1 of each bar accented. Choose
      them in Audio settings > Click sound, or right-click the Click key. Saved with the project.
- [ ] Bar ruler above the song overview: bar numbers, a marker for bar 1 (the metronome's tempo) and one per change.
      Double-click a bar to add a change there (it opens an editor: tempo, beats per bar, Delete); click a marker to edit
      it; drag a marker to move it by whole bars; right-click for Edit / Delete / Add.
- [ ] The click follows the changes (tempo and accents, e.g. 3/4 after 4/4); count-in uses bar 1's tempo.
- [ ] Audio settings > Tempo changes: the same changes as rows (from bar, tempo, beats, X) and "+ Add change"; edits
      show on the ruler and the other way round. The changes are saved with the project.

## Moving strips (new)
- [ ] Drag a track by its name plate onto another track: a line shows where it lands (left or right half), it moves
      there; buses move among buses the same way. A single click on a name does nothing (only the INSERTS button opens the
      chain); a right-click gives the menu, a double-click renames.
- [ ] The order is saved with the project and comes back on reopening; another project has its own order; the sound
      and routing don't change. "+ Add track" puts a re-added track at the end.
- [ ] No coloured bar under the channel names (the option is gone too).

## New default view (new)
- [ ] On a fresh install (or after Display > Reset) the look is today's: amber, normal glow, matte faceplate, sharp
      corners, ivory knobs, cream faders, Industrial type, screws, rack inserts, classic buttons. The eras and the
      other classic looks are unchanged.
- [ ] Fresh settings also start with the meter dock at 377 px (469 px with the insert panel open), the dark meter view
      on and 1339 px wide, and the reference suite Stereo field, Listening notes, Loudness, Band phase, Reference A/B.

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
- [ ] Deck: readouts centred, the word keys the same size (Display and Full narrow icon keys); keys press down, Listen / Meters / Display / MIDI Learn stay lit when on.
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

## To review later
- Hit Record Meter (i) guide: review the wording (app/ui/tools/HitMeterInfo.h).

## Not built yet (roadmap)
- Cable round-trip latency test and the lowest crackle-free buffer size (needs a patch cable from an output to an input).
