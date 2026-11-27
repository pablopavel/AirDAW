// Insert panel: the selected channel's insert chain, side by side. Two looks (Display > Hardware > Inserts):
// - Cards (default, the original mockup #dr drawer): cards that open to every parameter as a knob, presets as buttons.
// - Rack: a 500-series "lunchbox" with modules in a chassis (wood cheeks from the trim setting, a top rail, blank
//   plates in unused slots), faceplate finish by plugin category, folded modules as slim plates, a preset menu.
// Either way, drag a card / module by its header or name plate to reorder the chain.
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
class ClassicCard;
class RackCard;

// The row behind the cards. Rack look: the chassis well, with blank vented plates in the slots past `used` pixels.
class RackWell : public juce::Component {
 public:
  int used = 0;
  bool rack = false;
  void paint(juce::Graphics&) override;
};

class InsertPanel : public juce::Component {
 public:
  InsertPanel(AppCtx& ctx, PresetStore& presets);
  ~InsertPanel() override;
  // Opens the panel on a channel. slot >= 0: toggles that card open (filled slot) or targets it for loading (empty slot).
  void show(int channelId, int slot);
  void close();
  // Reorders a chain (drag and drop); open/closed card state follows each plugin.
  void moveInsert(int channelId, int from, int to);
  int channel() const { return ch_; }
  bool isOpen(int channelId, int slot) const;
  void sync();  // ~15x/s: knob values, rebuild if the chain changed
  int preferredHeight() const;
  void paint(juce::Graphics&) override;
  void resized() override;
  std::function<void()> onClosed;
 private:
  friend class InsertCard;
  friend class ClassicCard;
  friend class RackCard;
  void rebuild();
  void setOpen(int slot, bool open);
  juce::String chainKey() const;
  AppCtx& ctx_;
  PresetStore& presets_;
  int ch_ = -1, target_ = -1;
  std::map<int, std::map<int, bool>> open_;  // channel -> slot -> open
  std::map<int, std::map<int, juce::String>> preset_;  // channel -> slot -> last preset applied (shown on the module)
  juce::String key_;
  juce::Viewport view_;
  RackWell row_;
  juce::Rectangle<int> chassis_;  // wood cheeks + rail around the viewport
  std::vector<std::unique_ptr<InsertCard>> cards_;
  std::unique_ptr<juce::Component> addCard_;
  juce::TextButton close1_{juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95 Close"))}, close2_{juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95 Close"))};
  bool showOld_ = false;
  bool rack_ = false;  // Display > Hardware > Inserts: Cards (default) or Rack
};

}  // namespace ui
