// JUCE LookAndFeel for the AirDAW mockup style. Buttons pick their style from the component property "kind":
//   (none)  standard bevelled button; toggled on = lit amber with glow
//   "m" "s" "r" "t"  strip Mute / Solo / Record-arm / Takes (amber, blue, red, violet when on)
//   "flat"  quiet button (dashed "add track" etc.)
// and "noUpper" = true keeps the text case (default is UPPERCASE like the mockup).
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
};

}  // namespace ui
