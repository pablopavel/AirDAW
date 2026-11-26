// Takes bar (mockup #tk): the loop region, and the takes of one track as chips with a mini waveform.
// Shown when a loop region exists or a track's T button is on. Click a chip to hear that take; star the
// keepers; "Delete unkept" moves the rest to the Recycle Bin.
#pragma once
#include <memory>
#include <vector>
#include "AppCtx.h"
#include "Widgets.h"

namespace ui {

class TakesBar : public juce::Component {
 public:
  explicit TakesBar(AppCtx& ctx);
  bool wanted() const;  // should the bar be visible?
  void sync();          // ~10x/s
  void paint(juce::Graphics&) override;
  void resized() override;
  static constexpr int kHeight = 48;

 private:
  struct Chip : juce::Component, juce::SettableTooltipClient {
    int index = -1;            // take index on the track
    juce::String label;
    bool active = false, kept = false;
    std::array<float, 24> peaks{};
    std::function<void(int)> onPick;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onPick) onPick(index); }
  };
  int track() const;                       // viewed track, or the first armed one, or -1
  std::vector<int> scope(airdaw::Engine&, int t) const;  // takes in the loop region (or all, without a region)
  void rebuild();
  void deleteUnkept();
  AppCtx& ctx_;
  Lcd region_{"Region", 14.f};
  juce::TextButton loop_, keep_, del_{"Delete unkept"}, clear_{"Clear loop"};
  juce::Viewport view_;
  juce::Component row_;
  std::vector<std::unique_ptr<Chip>> chips_;
  juce::String key_, title_, hint_;
  juce::Rectangle<int> titleArea_;
};

}  // namespace ui
