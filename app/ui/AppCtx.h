// What every UI panel needs from the app: the current engine (it is rebuilt on a sample-rate change, so
// never cache the pointer), the audio device, settings, and a few app-level actions.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include "../AudioBridge.h"

namespace ui {

// Channel ids as used by airdaw::Engine: 0..23 tracks, 100..103 buses, 200 master.
inline bool isTrack(int id) { return id >= 0 && id < airdaw::kMaxTracks; }
inline bool isBus(int id) { return id >= 100 && id < 100 + airdaw::kNumBuses; }
constexpr int kMasterId = 200;
inline airdaw::Channel* channelOf(airdaw::Engine& e, int id) {
  if (isTrack(id)) return &e.track(id);
  if (isBus(id)) return &e.bus(id - 100);
  if (id == kMasterId) return &e.master();
  return nullptr;
}

// Listening notes: time-stamped remarks, kept beside the project (<project>/listening-notes.json).
struct ListeningNotes {
  struct Note { double seconds; juce::String text; };
  std::vector<Note> items;
  juce::File file;
  void load(const juce::File& f) {
    file = f; items.clear();
    const juce::var parsed = juce::JSON::parse(f);  // keep it alive while reading
    if (auto* a = parsed.getArray())
      for (auto& v : *a) items.push_back({(double)v["t"], v["text"].toString()});
  }
  void save() const {
    if (file == juce::File()) return;
    juce::Array<juce::var> a;
    for (auto& n : items) { auto* o = new juce::DynamicObject(); o->setProperty("t", n.seconds); o->setProperty("text", n.text); a.add(juce::var(o)); }
    if (items.empty()) file.deleteFile(); else { file.getParentDirectory().createDirectory(); file.replaceWithText(juce::JSON::toString(a)); }
  }
  void sort() { std::sort(items.begin(), items.end(), [](const Note& a, const Note& b) { return a.seconds < b.seconds; }); }
};

// Tap tempo: the average of the last few taps; a pause of more than 2 s starts over.
struct TapTempo {
  std::vector<double> taps;  // ms
  // Returns the new tempo (BPM, 0.5 steps, 20..400), or 0 when there are not enough taps yet.
  double tap(double nowMs) {
    if (!taps.empty() && nowMs - taps.back() > 2000.0) taps.clear();
    taps.push_back(nowMs);
    if (taps.size() > 6) taps.erase(taps.begin());
    if (taps.size() < 2) return 0.0;
    const double beatMs = (taps.back() - taps.front()) / (double)(taps.size() - 1);
    return juce::jlimit(20.0, 400.0, std::round(60000.0 / beatMs * 2.0) / 2.0);
  }
};

struct AppCtx {
  AudioBridge& bridge;
  juce::AudioDeviceManager& devices;
  juce::PropertiesFile& props;
  airdaw::Engine* engine() const { return bridge.engine(); }
  bool audioRunning = false;                                  // updated by MainComponent
  int viewTrack = -1;                                         // track whose takes the takes bar shows (T button)
  ListeningNotes notes;                                       // for the current project (MainComponent loads/saves)
  std::function<void(const juce::String&)> note;              // short message in the footer
  std::function<void(int channelId, int slot)> showInserts;   // open the insert panel (slot -1: just the chain)
  std::function<void(int track)> removeTrack;                 // asks, then clears the track and hides its strip
  std::function<void(int track)> duplicateTrack;              // a copy (audio, inserts, settings) right after it
  std::function<bool(int channelId, int slot)> isCardOpen;    // insert card expanded in the panel
  std::function<int()> insertPanelChannel;                    // the channel the insert panel shows (-1: closed)
  std::function<void(int channelId, int slot)> openPluginWindow;  // a hosted VST3's own editor window
  std::function<void(int channelId, int from, int to)> moveInsert;  // reorder a chain (drag and drop)
  std::function<void(int moving, int onto, bool after)> moveStrip;  // reorder the mixer's strips (drag and drop)
  std::function<void(const juce::String& effect)> showPluginInfo;   // Airwindows write-up window
  std::function<void()> resetPeaks;                                 // headroom tool: peak hold / overs only
  std::function<void()> loadReference;                              // Reference A/B tool: choose and decode a file
  juce::String referenceStatus;                                     // "decoding..." / error text for the A/B tool
  // Side-by-side levels for the A/B tool: the mix (master tap) and the reference as it plays (raw, no match gain).
  struct Levels { float m = -200.f, s = -200.f, i = -200.f, tp = -200.f; };
  Levels mixLevels, refLevels;                                      // refLevels.i = the whole file's loudness
  bool refLive = false;                                             // the reference is playing (its M/S/TP are live)
  TapTempo tapper;                                                  // shared by the deck's Tap button and Audio settings
  // One tap: sets the metronome tempo once there are two taps or more.
  void tapTempo() {
    auto* e = engine(); if (!e) return;
    const double bpm = tapper.tap(juce::Time::getMillisecondCounterHiRes());
    if (bpm > 0.0) e->metronome().bpm = (float)bpm;
    if (note) note(bpm > 0.0 ? "Tempo " + juce::String(bpm, 1) + " BPM (tap)" : juce::String("Tap tempo: keep tapping"));
  }
  std::function<void()> layoutChanged;                        // a panel changed its preferred size
  std::function<bool(const char*)> requireAudio;              // false + explanation if no audio is running
};

}  // namespace ui
