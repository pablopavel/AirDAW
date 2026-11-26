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
juce::uint32 channelColour(int id);  // name-plate bar colour

struct AppCtx {
  AudioBridge& bridge;
  juce::AudioDeviceManager& devices;
  juce::PropertiesFile& props;
  airdaw::Engine* engine() const { return bridge.engine(); }
  bool audioRunning = false;                                  // updated by MainComponent
  std::function<void(const juce::String&)> note;              // short message in the footer
  std::function<void(int channelId, int slot)> showInserts;   // open the insert panel (slot -1: just the chain)
  std::function<bool(int channelId, int slot)> isCardOpen;    // insert card expanded in the panel
  std::function<void(int channelId, int slot)> openPluginWindow;  // a hosted VST3's own editor window
  std::function<void()> layoutChanged;                        // a panel changed its preferred size
  std::function<bool(const char*)> requireAudio;              // false + explanation if no audio is running
};

}  // namespace ui
