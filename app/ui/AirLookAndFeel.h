// JUCE LookAndFeel for the AirDAW mockup style. Buttons pick their style from the component property "kind":
//   (none)  standard button; toggled on = lit in the accent
//   "m" "s" "r" "t" "mon"  strip Mute / Solo / Record-arm / Takes / Monitor (amber, blue, red, violet, green)
//   "key"   deck menu keys (they light while held); "cap" (int64 ARGB, 0 = the accent) gives a button its own cap colour
//   "flat"  quiet button (dashed "add track" etc.); "lcd" a small readout that opens a menu
// "noUpper" = true keeps the text case (default is UPPERCASE like the mockup); "sw" = true makes the button a bat-handle
// toggle switch when Display > Hardware > Buttons is Toggles. The Buttons option picks the look: Console (coloured caps
// in a bezel, lit from behind when on), Toggles (the same, strip switches as toggles) or Classic (the original keys).
#pragma once
#include "Theme.h"

namespace ui {

class AirLookAndFeel : public juce::LookAndFeel_V4 {
 public:
  AirLookAndFeel();
  void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
  void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
  juce::Font getTextButtonFont(juce::TextButton&, int h) override;
  void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
  void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
  juce::Font getComboBoxFont(juce::ComboBox&) override;
  void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
  void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
  void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked,
                         bool hasSub, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon,
                         const juce::Colour* textColour) override;
  void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
  juce::Font getPopupMenuFont() override { return Theme::uiFont(14.f); }
  void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                     int thumbSize, bool over, bool down) override;
  int getDefaultScrollbarWidth() override { return 9; }
  void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
  void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
  void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                        juce::Slider::SliderStyle, juce::Slider&) override;
  void drawTooltip(juce::Graphics&, const juce::String& text, int w, int h) override;
  void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
  void refreshColours();  // after an accent change
  // Where a button's legend goes and its colour (for buttons that draw their own icon, like the monitor button).
  static juce::Rectangle<float> legendArea(juce::Button&, bool down);
  static juce::Colour legendColour(juce::Button&, bool down);
  static bool isSwitch(const juce::Button&);  // drawn as a toggle switch right now
};

}  // namespace ui
