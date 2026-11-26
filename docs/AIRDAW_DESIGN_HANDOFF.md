# AirDAW design handoff

`airdaw-v4.html` is the approved visual mockup (single self-contained HTML/CSS/JS file; open it in a browser).
Treat it as the visual and behavioural spec for the JUCE UI. It is a mockup: all audio, meters and takes are simulated.

## How to use it
1. Open the file in a browser and click through everything (Display panel, Listen mode, insert panel, loop/takes, Plugins panel, MIDI Learn).
2. Read the CSS custom properties and class names; they define the palette, tints, glow and trim.
3. Port screen by screen onto the real engine (core/ in the repo). Do not change the engine design without asking.

## Layout
- **Top deck:** logo "AIRDAW / Airwindows Preferred Recording" + reels (left), large position timer MM:SS.mmm and tape-style
  transport keys (|< Play Stop Rec Loop) in the centre, rate/latency/DSP readouts, Listen, MIDI Learn, Plugins, Meters, Display, Full (right).
- **Song overview:** one summed waveform row for navigation (no track clutter), loop region, per-track take markers.
- **Mixer:** 24 tracks (mono/stereo), 4 buses, master. Button order M S T R, round gain knob, pan dot, fader with dB scale and ladder meters.
  Double-click knobs/faders resets; mouse wheel adjusts; track names editable (double-click).
- **Insert panel:** buttons for inserts, expands to show ALL knobs of the Airwindows plugin (dynamic count), multi-open, drag to reorder, close on both ends.
- **Meter dock:** Airwindows Hit Record Meter (real VST3 in the build) + reference suite (stereo field, RTA, loudness/balance), resizable columns, pop-out.
  Hit Record Meter dots must never glow.
- **Listen mode:** monitor-only; draggable/minimisable tools (target loudness, tonal balance, spectrogram, reference A/B, loudness history,
  band phase/width, headroom, listening notes, ear-break timer with on/off).
- **Display panel (collapsible sections):** Look (eras: 50s Broadcast, 60s Reel Deck, 70s Console, 80s Rack, 90s Digital, 2000s Glass;
  classics: Studio, Neon, Ice, Lounge, Vintage), Color & glow (12 accents, glow Off..Max, readout colours), Surfaces (faceplate textures, tints,
  corners, screws), Hardware (knobs, fader caps, VU face, reels), Side trim (wood/rack ears/tolex/chrome, species, grain, width), Type, Screen (OLED/standard, scale 100-200%, pixel shift), Reset.
  Accent colour must recolour everything uniformly (strips, faders, lit buttons, logo, transport glow).

## Real-build requirements the mockup cannot show
- Fader, scale and meter heights are computed from the available strip height so they resize correctly with the window.
- Every control is a named parameter so it can be MIDI-learned.
- Newest Airwindows plugin versions shown by default; manual update/rollback with "new" tags (never auto-install).
- Keep 100% scale as default; scale slider must be low-sensitivity. Window resizable and full-screen capable.
- Settings (look, accent, scale) persist between runs.
