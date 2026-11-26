// Insert panel (mockup #dr drawer): the selected channel's insert chain as cards. Several cards can be open;
// an open Airwindows card shows every parameter as a knob. Presets: Default + the user's own saved ones.
// (Drag-to-reorder needs an engine change and comes in a later phase.)
#pragma once
#include <map>
#include <memory>
#include <vector>
#include "AppCtx.h"
#include "Widgets.h"

namespace ui {

// User presets, one JSON file per plugin in <settings folder>/presets.
class PresetStore {
 public:
  explicit PresetStore(juce::File dir) : dir_(std::move(dir)) {}
  juce::StringArray names(const juce::String& plugin) const;
  std::vector<float> values(const juce::String& plugin, const juce::String& name) const;
  void save(const juce::String& plugin, const juce::String& name, const std::vector<float>& values);
  void remove(const juce::String& plugin, const juce::String& name);
 private:
  juce::var load(const juce::String& plugin) const;
  void store(const juce::String& plugin, const juce::var& v);
  juce::File file(const juce::String& plugin) const { return dir_.getChildFile(juce::File::createLegalFileName(plugin) + ".json"); }
  juce::File dir_;
};

class InsertCard;

class InsertPanel : public juce::Component {
 public:
  InsertPanel(AppCtx& ctx, PresetStore& presets);
  ~InsertPanel() override;
  // Opens the panel on a channel. slot >= 0: toggles that card open (filled slot) or targets it for loading (empty slot).
  void show(int channelId, int slot);
  void close();
  int channel() const { return ch_; }
  bool isOpen(int channelId, int slot) const;
  void sync();  // ~15x/s: knob values, rebuild if the chain changed
  int preferredHeight() const;
  void paint(juce::Graphics&) override;
  void resized() override;
  std::function<void()> onClosed;
 private:
  friend class InsertCard;
  void rebuild();
  void setOpen(int slot, bool open);
  juce::String chainKey() const;
  AppCtx& ctx_;
  PresetStore& presets_;
  int ch_ = -1, target_ = -1;
  std::map<int, std::map<int, bool>> open_;  // channel -> slot -> open
  juce::String key_;
  juce::Viewport view_;
  juce::Component row_;
  std::vector<std::unique_ptr<InsertCard>> cards_;
  std::unique_ptr<juce::Component> addCard_;
  juce::TextButton close1_{juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95 Close"))}, close2_{juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95 Close"))};
  bool showOld_ = false;
};

}  // namespace ui
