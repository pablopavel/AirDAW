// "Audio" window: device selection, test tone, metronome settings and auto-save.
// (These lived on the old single test screen; the mockup has no place for them in the main view.)
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AppCtx.h"
#include "Theme.h"

namespace ui {

class SettingsContent : public juce::Component, private juce::Timer {
 public:
  SettingsContent(AppCtx& ctx) : ctx_(ctx), selector_(ctx.devices, 0, 2, 0, 2, false, false, true, false) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&selector_, &tone_, &bpm_, &beats_, &countIn_, &level_,
                                                                      &autoSave_, &autoEvery_, &autoInfo_})
      addAndMakeVisible(c);
    tone_.setButtonText("Test tone on track 1 (440 Hz, -15 dBFS)");
    tone_.onClick = [this] { ctx_.bridge.testTone = tone_.getToggleState(); if (tone_.getToggleState()) ctx_.requireAudio("Test tone"); };
    bpm_.setSliderStyle(juce::Slider::IncDecButtons); bpm_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 26);
    bpm_.setRange(20.0, 400.0, 0.5);
    level_.setSliderStyle(juce::Slider::LinearHorizontal); level_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    level_.setRange(-40.0, 0.0, 0.5); level_.setTextValueSuffix(" dB");
    for (int b = 1; b <= 12; ++b) beats_.addItem(juce::String(b) + "/4", b);
    countIn_.addItem("No count-in", 1); countIn_.addItem("Count-in 1 bar", 2); countIn_.addItem("Count-in 2 bars", 3);
    bpm_.onValueChange = [this] { apply(); }; level_.onValueChange = [this] { apply(); };
    beats_.onChange = [this] { apply(); }; countIn_.onChange = [this] { apply(); };
    autoSave_.setButtonText("Auto-save every");
    const char* labels[] = {"1 min", "2 min", "5 min", "10 min", "15 min", "30 min"};
    for (int i = 0; i < 6; ++i) autoEvery_.addItem(labels[i], i + 1);
    autoSave_.onClick = [this] { if (onAutoSave) onAutoSave(autoSave_.getToggleState(), autoEvery_.getSelectedId()); };
    autoEvery_.onChange = [this] { if (onAutoSave) onAutoSave(autoSave_.getToggleState(), autoEvery_.getSelectedId()); };
    autoInfo_.setColour(juce::Label::textColourId, Theme::dim());
    sync();
    startTimerHz(4);
    setSize(640, 760);
  }

  std::function<void(bool on, int intervalId)> onAutoSave;
  std::function<juce::String()> autoSaveInfo;
  std::function<std::pair<bool, int>()> autoSaveState;

  void sync() {
    auto* e = ctx_.engine(); if (!e) return;
    auto& m = e->metronome();
    if (!bpm_.isMouseButtonDown()) bpm_.setValue(m.bpm.load(), juce::dontSendNotification);
    if (!level_.isMouseButtonDown()) level_.setValue(m.levelDb.load(), juce::dontSendNotification);
    beats_.setSelectedId(juce::jlimit(1, 12, m.beatsPerBar.load()), juce::dontSendNotification);
    countIn_.setSelectedId(juce::jlimit(0, 2, m.countInBars.load()) + 1, juce::dontSendNotification);
    tone_.setToggleState(ctx_.bridge.testTone.load(), juce::dontSendNotification);
    if (autoSaveState) { auto [on, id] = autoSaveState(); autoSave_.setToggleState(on, juce::dontSendNotification);
                         autoEvery_.setSelectedId(id, juce::dontSendNotification); }
    if (autoSaveInfo) autoInfo_.setText(autoSaveInfo(), juce::dontSendNotification);
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colour(0xff0b0b0c));
    auto head = [&](int y, const char* s) {
      g.setColour(juce::Colour(0x14ffffff)); g.fillRect(16, y - 6, getWidth() - 32, 1);
      g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.5f, true), 0.18f));
      g.drawText(s, 16, y, getWidth() - 32, 20, juce::Justification::centredLeft);
    };
    head(12, "AUDIO DEVICE");
    head(metroY_, "METRONOME");
    head(autoY_, "AUTO-SAVE");
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(13.f));
    g.drawText("Tempo (BPM)", 24, bpm_.getY(), 120, 26, juce::Justification::centredLeft);
    g.drawText("Beats / count-in", 24, beats_.getY(), 120, 26, juce::Justification::centredLeft);
    g.drawText("Click level", 24, level_.getY(), 120, 26, juce::Justification::centredLeft);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(16);
    r.removeFromTop(24);
    selector_.setBounds(r.removeFromTop(390));
    tone_.setBounds(r.removeFromTop(28).withTrimmedLeft(8));
    r.removeFromTop(22); metroY_ = r.getY(); r.removeFromTop(28);
    bpm_.setBounds(r.removeFromTop(28).withTrimmedLeft(140).withWidth(170)); r.removeFromTop(8);
    auto row = r.removeFromTop(28).withTrimmedLeft(140);
    beats_.setBounds(row.removeFromLeft(80)); row.removeFromLeft(8); countIn_.setBounds(row.removeFromLeft(150));
    r.removeFromTop(8);
    level_.setBounds(r.removeFromTop(28).withTrimmedLeft(140).withWidth(300));
    r.removeFromTop(22); autoY_ = r.getY(); r.removeFromTop(28);
    row = r.removeFromTop(28).withTrimmedLeft(8);
    autoSave_.setBounds(row.removeFromLeft(150)); autoEvery_.setBounds(row.removeFromLeft(100));
    autoInfo_.setBounds(r.removeFromTop(26).withTrimmedLeft(8));
  }

 private:
  void apply() {
    auto* e = ctx_.engine(); if (!e) return;
    auto& m = e->metronome();
    m.bpm = (float)bpm_.getValue(); m.levelDb = (float)level_.getValue();
    m.beatsPerBar = juce::jlimit(1, 16, beats_.getSelectedId()); m.countInBars = juce::jlimit(0, 4, countIn_.getSelectedId() - 1);
  }
  void timerCallback() override { sync(); }
  AppCtx& ctx_;
  juce::AudioDeviceSelectorComponent selector_;
  juce::ToggleButton tone_, autoSave_;
  juce::Slider bpm_, level_;
  juce::ComboBox beats_, countIn_, autoEvery_;
  juce::Label autoInfo_;
  int metroY_ = 0, autoY_ = 0;
};

class SettingsWindow : public juce::DocumentWindow {
 public:
  SettingsWindow(AppCtx& ctx, std::function<void(SettingsContent&)> wire)
      : DocumentWindow("AirDAW - Audio", juce::Colour(0xff0b0b0c), DocumentWindow::closeButton) {
    auto* c = new SettingsContent(ctx);
    if (wire) wire(*c);
    c->sync();
    setUsingNativeTitleBar(true);
    setContentOwned(c, true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }
  void closeButtonPressed() override { setVisible(false); }
};

}  // namespace ui
