// Mockup controls drawn in JUCE: LCD readout, name plate, gain knob, pan, fader block
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

// Cream name plate with a coloured bar (click = select, double-click = rename).
class NamePlate : public juce::Component {
 public:
  void setName2(const juce::String& n) { if (n != name_) { name_ = n; repaint(); } }
  void setColourBar(juce::uint32 c) { bar_ = c; repaint(); }
  void setRenamable(bool r) { renamable_ = r; }
  std::function<void()> onClick;
  std::function<void()> onMenu;  // right-click
  std::function<void(const juce::String&)> onRename;
  void startRename();  // the inline name editor (also on double-click)
  void paint(juce::Graphics&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  void resized() override { if (ed_) ed_->setBounds(getLocalBounds()); }
 private:
  void finishEdit(bool ok);
  juce::String name_; juce::uint32 bar_ = 0xff999999; bool renamable_ = true;
  std::unique_ptr<juce::TextEditor> ed_;
};

// Draggable divider between two areas (mixer / meter dock, dock columns). The groove runs the whole length and can be
// grabbed anywhere; it lights up while hovered or dragged. Drag updates are applied at most ~60 times a second
// (always the newest position, and once more on release), so heavy layouts follow the mouse instead of lagging.
class Divider : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
 public:
  explicit Divider(bool vertical) : vertical_(vertical) {
    setMouseCursor(vertical ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity(true);
  }
  std::function<int()> current;          // the size being dragged, read when the drag starts
  std::function<void(int)> onDrag;       // new size: start + horizontal distance, or start - vertical distance
  std::function<void(bool)> onDragState; // true when a drag starts, false when it ends
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  bool dragging() const { return dragging_; }
 private:
  void timerCallback() override;
  void flush() { if (dirty_) { dirty_ = false; if (onDrag) onDrag(pending_); } }
  bool vertical_, dragging_ = false, dirty_ = false;
  int start_ = 0, pending_ = 0;
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
// The component is kGlowPad taller above and below than its row, so the thumb's glow is not cut off; only the row
// itself takes clicks.
class PanSlider : public DragControl {
 public:
  PanSlider() { setRange(-1.f, 1.f, 0.f); horizontal = true; pixelsForRange = 110.f; wheelStep = 0.06f; }
  static constexpr int kGlowPad = 9;  // fits the glow up to the Max setting
  bool hitTest(int, int y) override { return y >= kGlowPad && y < getHeight() - kGlowPad; }
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
  // Free corner at the bottom left, under the dB scale (where a strip puts its polarity button).
  juce::Rectangle<int> cornerArea() const;
 private:
  juce::Rectangle<float> trackArea() const;
  float mL_ = 0.f, mR_ = 0.f;
};

// Large plugin knob with name and value text (mockup .kn / .knob).
class PluginKnob : public DragControl {
 public:
  PluginKnob() { setRange(0.f, 1.f, 0.5f); pixelsForRange = 150.f; wheelStep = 0.02f; }
  juce::String label, valueText;
  // Rack faceplate style (insert rack modules): -1 = the glowing amber ring; 0 = black cap, 1 = cream, 2 = gold,
  // with a printed tick scale and labels in `ink` and the value in a small LCD.
  int rackCap = -1;
  juce::Colour ink, inkDim;
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

// Insert drag-and-drop: description "insert:<channelId>:<slot>"; drops only within the same channel.
juce::String insertDragId(int channelId, int slot);
bool parseInsertDrag(const juce::var& desc, int& channelId, int& slot);
void drawDropOutline(juce::Graphics&, juce::Rectangle<float>, float corner);

// Insert slot button: LED + plugin name, or "+ Insert" when empty (mockup .ins).
// A filled slot can be dragged onto another slot of the same strip to reorder the chain.
// `summary`: a strip's one Inserts button instead (LED lit while a plugin runs, the number of plugins on the right).
class InsertSlot : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
 public:
  juce::String name;       // empty = free slot
  bool lit = true, open = false, hosted = false, summary = false;
  int channelId = -1, slot = -1, count = 0;
  std::function<void()> onClick;
  std::function<void(int fromSlot)> onDropFrom;
  void paint(juce::Graphics&) override;
  void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseEnter(const juce::MouseEvent&) override { repaint(); }
  void mouseExit(const juce::MouseEvent&) override { repaint(); }
  bool isInterestedInDragSource(const SourceDetails&) override;
  void itemDragEnter(const SourceDetails&) override { dropHover_ = true; repaint(); }
  void itemDragExit(const SourceDetails&) override { dropHover_ = false; repaint(); }
  void itemDropped(const SourceDetails&) override;
 private:
  bool dropHover_ = false;
};

}  // namespace ui
