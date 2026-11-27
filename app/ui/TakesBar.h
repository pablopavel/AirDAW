// Takes bar (mockup #tk): the loop region, and the takes of one track as chips with a mini waveform.
// Shown when a loop region exists or a track's T button is on. Click a chip to hear that take; star the
// keepers; "Delete unkept" moves the rest to the Recycle Bin.
// Comping: with a loop region, clicking a take plays it in that region only (the rest of the track keeps its take);
// right-click a take for "whole track" / "this region". Outside the comped regions: the track's take or silence.
// Crossfade at every comp edge: 0..50 ms (saved with the project).
#pragma once
#include <memory>
#include <vector>
#include "AppCtx.h"
#include "Widgets.h"

namespace ui {

class TakesBar : public juce::Component {
 public:
  explicit TakesBar(AppCtx& ctx);
  bool wanted() const;  // should the bar be visible? (not after a close, until the region or the viewed track changes)
  void sync();          // ~10x/s
  void paint(juce::Graphics&) override;
  void resized() override;
  static constexpr int kHeight = 48;

 private:
  // Click: pick the take (after the double-click time, so a double-click does not pick it first).
  // Double-click: bring back the take's loop region (and with it the comp settings). Right-click: menu.
  struct Chip : juce::Component, juce::SettableTooltipClient, private juce::Timer {
    int index = -1;            // take index on the track
    juce::String label;
    bool active = false, kept = false, inComp = false;
    std::array<float, 24> peaks{};
    std::function<void(int)> onPick, onMenu, onRecall;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent& e) override {
      if (!e.mouseWasClicked()) return;
      if (e.mods.isPopupMenu()) { if (onMenu) onMenu(index); return; }
      if (e.getNumberOfClicks() == 1) startTimer(juce::MouseEvent::getDoubleClickTimeout());
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
      if (e.mods.isPopupMenu()) return;
      stopTimer(); if (onRecall) onRecall(index);
    }
   private:
    void timerCallback() override { stopTimer(); if (onPick) onPick(index); }
  };
  void recall(int t, int idx);  // the take's loop region back (its comp region, else its own span), loop on
  int track() const;                                              // viewed track, or the first armed one, or -1
  const airdaw::Take* selected(airdaw::Engine& e, int t) const;  // what Keep stars
  std::vector<int> scope(airdaw::Engine&, int t) const;  // takes in the loop region (or all, without a region)
  void rebuild();
  void deleteUnkept();
  void pick(int t, int idx, int how);  // how: 0 = automatic, 1 = whole track, 2 = this region only
  AppCtx& ctx_;
  Lcd region_{"Region", 14.f};
  juce::TextButton closeL_, closeR_, loop_, keep_, del_{"Delete unkept"}, clear_{"Clear loop"}, outside_, clearComp_{"Clear comp"};
  juce::Slider xfade_{juce::Slider::LinearBar, juce::Slider::TextBoxLeft};
  juce::Viewport view_;
  juce::Component row_;
  std::vector<std::unique_ptr<Chip>> chips_;
  juce::String key_, title_, hint_;
  juce::String closedFor_;  // region / track the bar was closed for
  juce::String placeKey() const;
  juce::Rectangle<int> titleArea_;
  int labelX_ = 16;
};

}  // namespace ui
