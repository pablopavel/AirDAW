// Meter dock (mockup #awm): the real Airwindows Hit Record Meter VST3 editor, embedded.
// The reference suite (stereo field, RTA, loudness) arrives with the master analysis feed in a later phase.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AppCtx.h"
#include "Theme.h"

namespace ui {

class MeterDock : public juce::Component {
 public:
  explicit MeterDock(AppCtx& ctx) : ctx_(ctx) {
    load_.setButtonText("Load Hit Record Meter...");
    load_.onClick = [this] { if (onLoadMeter) onLoadMeter(); };
    addAndMakeVisible(load_);
  }
  ~MeterDock() override { setPlugin(nullptr); }

  std::function<void()> onLoadMeter;

  // Shows this plugin's editor (or the "load" prompt when null). Keeps the plugin alive while its editor exists.
  void setPlugin(std::shared_ptr<juce::AudioPluginInstance> p) {
    if (p == plugin_) return;
    editor_.reset();      // the editor must go before the plugin reference is released
    plugin_ = std::move(p);
    if (plugin_) {
      if (auto* ed = plugin_->createEditorIfNeeded()) { editor_.reset(ed); addAndMakeVisible(*editor_); }
    }
    load_.setButtonText(plugin_ ? "Change meter..." : "Load Hit Record Meter...");
    resized(); repaint();
  }
  bool hasPlugin() const { return plugin_ != nullptr; }

  void paint(juce::Graphics& g) override {
    auto& t = T();
    auto r = getLocalBounds().toFloat();
    t.drawGlow(g, r, t.am(), 26.f, 0.09f);
    g.setColour(juce::Colour(Theme::kBg)); g.fillRoundedRectangle(r, 6.f);
    auto head = r.removeFromTop(kHead);
    t.drawBrush(g, head);
    g.setColour(juce::Colours::black); g.fillRect(head.removeFromBottom(1.f));
    // title: AIR + WINDOWS (accent) + HIT RECORD METER
    auto tr = head.reduced(12.f, 0.f);
    g.setFont(Theme::spaced(Theme::uiFont(14.f, true), 0.28f));
    juce::GlyphArrangement ga;
    auto draw = [&](const juce::String& s, juce::Colour c, bool glow) {
      if (glow) t.drawGlowText(g, s, tr, juce::Justification::centredLeft, c, 1.f);
      else { g.setColour(c); g.drawText(s, tr, juce::Justification::centredLeft, false); }
      ga.clear(); ga.addLineOfText(g.getCurrentFont(), s, 0, 0);
      tr.removeFromLeft(ga.getBoundingBox(0, -1, true).getWidth() + 4.f);
    };
    draw("AIR", Theme::cream(), false); draw("WINDOWS", t.am(), true); tr.removeFromLeft(8.f); draw("HIT RECORD METER", Theme::cream(), false);
    if (!plugin_) {
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(14.f));
      g.drawFittedText("The Airwindows Hit Record Meter is a free VST3 (github.com/airwindows/Meter/releases).\n"
                       "Load it once; it sits on the last master insert and its own display shows here.",
                       getLocalBounds().withTrimmedTop(kHead).reduced(20).withTrimmedBottom(60), juce::Justification::centred, 3);
    }
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.f, 1.f);
  }

  void resized() override {
    auto r = getLocalBounds();
    auto head = r.removeFromTop(kHead);
    if (plugin_) load_.setBounds(head.removeFromRight(150).reduced(6, 5));
    else load_.setBounds(r.withSizeKeepingCentre(240, 34).translated(0, 40));
    if (editor_) {
      auto area = r.reduced(8);
      if (editor_->isResizable()) editor_->setBounds(area);
      else editor_->setTopLeftPosition(area.getX() + juce::jmax(0, (area.getWidth() - editor_->getWidth()) / 2), area.getY());
    }
  }
  static constexpr int kHead = 36;

 private:
  AppCtx& ctx_;
  std::shared_ptr<juce::AudioPluginInstance> plugin_;
  std::unique_ptr<juce::AudioProcessorEditor> editor_;
  juce::TextButton load_;
};

}  // namespace ui
