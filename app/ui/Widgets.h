// Mockup controls drawn in JUCE: LCD readout, tape reels, name plate, gain knob, pan, fader block
// (dB scale + fader + ladder meters + dB readout), plugin knob, VU meter, insert slot.
#pragma once
#include <functional>
#include "Theme.h"

namespace ui {

// Amber LCD readout with a small caption (mockup .lcd).
class Lcd : public juce::Component, public juce::SettableTooltipClient {
 public:
  Lcd(juce::String caption, float textH) : caption_(std::move(caption)), h_(textH) {}
  void setText(const juce::String& s, juce::Colour override = {}) {
    if (s == text_ && override == col_) return; text_ = s; col_ = override; repaint();
  }
  void paint(juce::Graphics&) override;
  std::function<void()> onClick;
  void mouseUp(const juce::MouseEvent& e) override { if (onClick && e.mouseWasClicked()) onClick(); }
 private:
  juce::String caption_, text_; float h_; juce::Colour col_;
};

// Pair of tape reels that turn while the transport plays.
class Reels : public juce::Component, private juce::Timer {
 public:
  Reels() { startTimerHz(30); }
  std::function<bool()> isPlaying;
  void paint(juce::Graphics&) override;
 private:
  void timerCallback() override;
  float a1_ = 0.f, a2_ = 0.f;
};

// Cream name plate with a coloured bar (click = select, double-click = rename).
class NamePlate : public juce::Component {
 public:
  void setName2(const juce::String& n) { if (n != name_) { name_ = n; repaint(); } }
  void setColourBar(juce::uint32 c) { bar_ = c; repaint(); }
  void setRenamable(bool r) { renamable_ = r; }
  std::function<void()> onClick;
  std::function<void(const juce::String&)> onRename;
  void paint(juce::Graphics&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  void resized() override { if (ed_) ed_->setBounds(getLocalBounds()); }
 private:
  void finishEdit(bool ok);
  juce::String name_; juce::uint32 bar_ = 0xff999999; bool renamable_ = true;
  std::unique_ptr<juce::TextEditor> ed_;
};

// Base for drag/wheel/double-click value controls. Value range [min,max], `def` on double-click.
class DragControl : public juce::Component {
 public:
  void setRange(float mn, float mx, float def) { min_ = mn; max_ = mx; def_ = def; }
  void setValue(float v, bool notify = false);
  float value() const { return v_; }
  std::function<void(float)> onChange;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override { setValue(def_, true); }
  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
 protected:
  float pixelsForRange = 150.f;  // vertical drag distance for the full range
  bool horizontal = false;
  float wheelStep = 0.f;         // 0 = range / 40
  float min_ = 0.f, max_ = 1.f, def_ = 0.f, v_ = 0.f, dragStart_ = 0.f;
};

// Channel gain (trim) knob with GAIN caption and value (mockup .gn).
class GainKnob : public DragControl {
 public:
  GainKnob() { setRange(-24.f, 24.f, 0.f); pixelsForRange = 144.f; wheelStep = 0.5f; }
  void paint(juce::Graphics&) override;
};

// Pan: thin track with a centre tick and a glowing round thumb. -1..1.
class PanSlider : public DragControl {
 public:
  PanSlider() { setRange(-1.f, 1.f, 0.f); horizontal = true; pixelsForRange = 110.f; wheelStep = 0.06f; }
  void paint(juce::Graphics&) override;
};

// Fader + dB scale + two-column ladder meter + dB readout. Heights follow the component height.
class FaderBlock : public DragControl {
 public:
  FaderBlock() { setRange(0.f, 100.f, 75.f); pixelsForRange = 260.f; wheelStep = 1.5f; }
  void setDb(float db) { setValue(vOf(db)); }
  float db() const { return dbOf(v_); }
  void setMeter(float peakL, float peakR);  // linear peaks, called ~30x/s
  bool bigReadout = false;
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
 private:
  juce::Rectangle<float> trackArea() const;
  float mL_ = 0.f, mR_ = 0.f;
};

// Large plugin knob with name and value text (mockup .kn / .knob).
class PluginKnob : public DragControl {
 public:
  PluginKnob() { setRange(0.f, 1.f, 0.5f); pixelsForRange = 150.f; wheelStep = 0.02f; }
  juce::String label, valueText;
  void paint(juce::Graphics&) override;
};

// Cream VU meter face with a needle (mockup .vu). level 0..1 maps -20..+3 VU.
class VuMeter : public juce::Component {
 public:
  explicit VuMeter(juce::String w) : word_(std::move(w)) {}
  void setLevel(float l) { if (std::abs(l - lv_) > 0.002f) { lv_ = l; repaint(); } }
  void paint(juce::Graphics&) override;
 private:
  juce::String word_; float lv_ = 0.f;
};

// Insert slot button: LED + plugin name, or "+ Insert" when empty (mockup .ins).
class InsertSlot : public juce::Component {
 public:
  juce::String name;       // empty = free slot
  bool lit = true, open = false, hosted = false;
  std::function<void()> onClick;
  void paint(juce::Graphics&) override;
  void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
  void mouseEnter(const juce::MouseEvent&) override { repaint(); }
  void mouseExit(const juce::MouseEvent&) override { repaint(); }
};

}  // namespace ui
