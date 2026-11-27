// Mixer area (mockup #mixw): scrolling tracks + "add track" + buses, master fixed on the right.
#pragma once
#include <array>
#include <memory>
#include <vector>
#include "Strip.h"

namespace ui {

class Mixer : public juce::Component {
 public:
  explicit Mixer(AppCtx& ctx);
  void sync();  // ~30x/s
  void resized() override;
  void paint(juce::Graphics&) override;
  void paintOverChildren(juce::Graphics&) override;
  int tracksShown() const { return shown_; }
  // Listen mode: only the master strip, larger readouts (mockup body.listen).
  void setListenMode(bool on) { listen_ = on; view_.setVisible(!on); master_.setListenLayout(on); resized(); }
  MasterStrip& master() { return master_; }
  void restyle();  // the Buttons option changed: strips lay their rows out again
  // Removed tracks: their strips are hidden (the track itself is cleared by the caller). "Add track" brings back the
  // lowest removed slot first. The list is saved per project (onRemovedChanged / setRemovedTracks).
  void removeTrack(int t);
  void setRemovedTracks(const juce::var& list);
  juce::var removedTracks() const;
  std::function<void()> onRemovedChanged;
  // The order strips are shown in (tracks among tracks, buses among buses); the engine's channels don't move.
  // Saved per project (onOrderChanged / setStripOrder).
  void moveStrip(int moving, int onto, bool after);
  void setStripOrder(const juce::var& list);
  juce::var stripOrder() const;
  std::function<void()> onOrderChanged;
 private:
  void relayout();
  std::array<bool, airdaw::kMaxTracks> removed_{};
  std::vector<int> order_, busOrder_;  // track ids / bus ids, left to right
  void resetOrder();
  AppCtx& ctx_;
  juce::Viewport view_;
  juce::Component content_;
  std::vector<std::unique_ptr<Strip>> tracks_, buses_;
  juce::TextButton add_;
  MasterStrip master_;
  int shown_ = 8;
  bool listen_ = false;
};

}  // namespace ui
