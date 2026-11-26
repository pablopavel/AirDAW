// MIDI Learn: every mixer control is a named target ("ch.3.fader", "transport.play", "fx.0.1.2" ...).
//
//  * Controls mark themselves with tagMidi(component, id); the learn overlay finds them by walking the window.
//  * Buttons are driven through their own click handler (no duplicated logic); faders, pan, gain and plugin
//    knobs are set on the engine directly, so a plugin mapping works even when its insert card is closed.
//  * DAW mappings are saved once for the whole app (settings), plugin mappings with the project (session "app").
//  * Fixed knobs/faders pick up: the control only follows once the knob passes its current value.
//    Endless knobs (relative) are detected while learning and never jump.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>
#include <array>
#include <deque>
#include <mutex>
#include "airdaw/engine.h"

namespace ui {

inline void tagMidi(juce::Component& c, const juce::String& id) { c.getProperties().set("midi", id); }

// A component with several mappable areas (e.g. the transport keys).
struct MidiRegionProvider {
  virtual ~MidiRegionProvider() = default;
  virtual std::vector<std::pair<juce::String, juce::Rectangle<int>>> midiRegions() const = 0;  // local coordinates
};

struct MidiMapping {
  enum Src { CC = 0, Note, PitchBend };
  enum Mode { Absolute = 0, Rel64, Rel2C, RelSign, PressToggles, FollowsValue, Rel16 };  // saved as numbers: append only
  Src src = CC; int ch = 1; int num = 0; juce::String device;  // device "" = any input
  juce::String target, fx;  // fx: plugin name for "fx.*" targets (ignored when another plugin sits in that slot)
  Mode mode = Absolute;
  // runtime (not saved)
  bool picked = false; float lastSet = -1.f, lastIn = -1.f; int lastRaw = -1, detect = 0;
  std::array<int, 6> recent{}; int nRecent = 0;  // last values, for telling endless knobs from fixed ones
  juce::String sourceText() const;
  juce::var toVar() const;
  static bool fromVar(const juce::var& v, MidiMapping& m);
};

class MidiControl : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
 public:
  struct Host {
    std::function<airdaw::Engine*()> engine;
    std::function<juce::Component*()> root;                     // the window content holding the tagged controls
    std::function<bool(const juce::String& id)> press;          // app-level targets (transport keys, tap); true = handled
    std::function<juce::var()> projectData;                     // per-project app data
    std::function<void(const juce::String&, const juce::var&)> setProjectValue;
    std::function<void(const juce::String&)> note;
    std::function<void()> changed;                              // learn state / mappings changed (repaint overlay, deck)
  };
  // openDevices = false: no MIDI inputs are opened (test snapshots; Windows MIDI inputs are often exclusive).
  MidiControl(juce::AudioDeviceManager& dm, juce::PropertiesFile& props, Host host, bool openDevices = true);
  ~MidiControl() override;

  // --- learn mode ---
  bool learning() const { return learning_; }
  void setLearning(bool on);
  const juce::String& armed() const { return armed_; }
  const juce::String& lastMessage() const { return lastMessage_; }  // e.g. "CC 21 = 65 . ch 1 . MiniLab 3"
  void arm(const juce::String& id) { armed_ = id; changed(); }

  // --- mappings ---
  const std::vector<MidiMapping>& mappings() const { return maps_; }
  const MidiMapping* mappingFor(const juce::String& target) const;
  void removeMapping(const juce::String& target);
  void setMode(const juce::String& target, MidiMapping::Mode m);
  void loadProjectMappings();                          // after a project is opened / created / recovered
  void insertMoved(int channelId, int from, int to);   // keep plugin mappings on the same plugin after a reorder

  // --- targets ---
  static bool isContinuous(const juce::String& id);
  static bool isProjectTarget(const juce::String& id) { return id.startsWith("fx."); }
  juce::String describe(const juce::String& id) const;
  float valueOf(const juce::String& id) const;         // continuous 0..1 (-1 = target not available)
  void setValue(const juce::String& id, float v);
  bool stateOf(const juce::String& id) const;          // buttons: lit or not
  void press(const juce::String& id);
  juce::Component* findTagged(const juce::String& id) const;

  // Test hook: feed a message as if it came from a device (message thread).
  void inject(const juce::String& device, const juce::MidiMessage& m) { handle(device, m); }

 private:
  void handleIncomingMidiMessage(juce::MidiInput* src, const juce::MidiMessage& m) override;
  void handleAsyncUpdate() override;
  void timerCallback() override;  // enable newly plugged-in controllers
  void enableNewDevices();
  void handle(const juce::String& device, const juce::MidiMessage& m);
  void learn(const juce::String& device, MidiMapping::Src src, int ch, int num, int raw);
  void apply(MidiMapping& mp, int raw, bool noteOn);
  void saveApp();
  void saveProject();
  void changed() { if (host_.changed) host_.changed(); }

  juce::AudioDeviceManager& dm_;
  juce::PropertiesFile& props_;
  Host host_;
  std::vector<MidiMapping> maps_;
  bool learning_ = false;
  juce::String armed_, lastMessage_;
  bool open_ = true;
  double lastHint_ = 0;
  std::mutex mx_;
  std::deque<std::pair<juce::String, juce::MidiMessage>> queue_;  // MIDI thread -> message thread
};

}  // namespace ui
