// "About this plugin" window: Chris's write-up for an Airwindows effect (compiled into AirDAW, the same text
// as airwindows.com), with its category and one-line summary.
#pragma once
#include <map>
#include "Theme.h"
#include "airdaw/fx_host.h"

namespace ui {

class PluginInfoContent : public juce::Component {
 public:
  PluginInfoContent() {
    body_.setMultiLine(true, true);
    body_.setReadOnly(true);
    body_.setScrollbarsShown(true);
    body_.setCaretVisible(false);
    body_.setFont(Theme::uiFont(15.f));
    body_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0b0b0c));
    body_.setColour(juce::TextEditor::textColourId, Theme::tx());
    body_.setColour(juce::TextEditor::outlineColourId, juce::Colours::black);
    body_.setIndents(14, 12);
    addAndMakeVisible(body_);
    setSize(680, 620);
  }

  void setPlugin(const juce::String& name) {
    if (infos_.empty()) for (auto& f : airdaw::listEffects()) infos_[f.name] = f;
    name_ = name;
    auto it = infos_.find(name.toStdString());
    category_ = it != infos_.end() ? juce::String(it->second.category) : juce::String();
    what_ = it != infos_.end() ? juce::String(it->second.description) : juce::String();
    auto doc = juce::String::fromUTF8(airdaw::effectDocumentation(name.toStdString()).c_str()).trim();
    if (doc.startsWith("#")) doc = doc.fromFirstOccurrenceOf("\n", false, false).trim();  // the headline repeats the summary
    body_.setText(doc.isNotEmpty() ? doc : juce::String("No description is available for this plugin."), false);
    body_.moveCaretToTop(false);
    repaint();
  }

  void paint(juce::Graphics& g) override {
    auto& t = T();
    g.fillAll(juce::Colour(0xff0b0b0c));
    auto head = getLocalBounds().removeFromTop(kHead);
    t.drawBrush(g, head.toFloat());
    g.setColour(juce::Colours::black); g.fillRect(head.removeFromBottom(1));
    auto r = head.reduced(18, 12);
    g.setFont(Theme::spaced(Theme::uiFont(22.f, true), 0.06f));
    t.drawGlowText(g, name_, r.removeFromTop(30).toFloat(), juce::Justification::centredLeft, Theme::cream(), 0.4f);
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.f), 0.16f));
    g.drawText(category_.toUpperCase() + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  AIRWINDOWS")), r.removeFromTop(18), juce::Justification::centredLeft);
    g.setColour(t.am()); g.setFont(Theme::uiFont(14.f));
    g.drawFittedText(what_, r, juce::Justification::topLeft, 2);
  }
  void resized() override { body_.setBounds(getLocalBounds().withTrimmedTop(kHead).reduced(10)); }
  static constexpr int kHead = 112;

 private:
  juce::TextEditor body_;
  juce::String name_, category_, what_;
  std::map<std::string, airdaw::FxInfo> infos_;
};

class PluginInfoWindow : public juce::DocumentWindow {
 public:
  PluginInfoWindow() : DocumentWindow("About plugin", juce::Colour(0xff0b0b0c), DocumentWindow::closeButton) {
    content_ = new PluginInfoContent();
    setUsingNativeTitleBar(true);
    setContentOwned(content_, true);
    setResizable(true, false);
    setResizeLimits(420, 320, 2000, 2000);
    centreWithSize(getWidth(), getHeight());
  }
  void show(const juce::String& name) {
    content_->setPlugin(name);
    setName("About " + name);
    setVisible(true); toFront(true);
  }
  void closeButtonPressed() override { setVisible(false); }
 private:
  PluginInfoContent* content_ = nullptr;
};

}  // namespace ui
