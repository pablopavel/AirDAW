// The meter dock's (i): what the Hit Record Meter measures and how to use it, in plain words. Drawn from Chris
// Johnson's notes in the Airwindows Meter source (github.com/airwindows/Meter) and its display.
#pragma once
#include "../Theme.h"

namespace ui {

class HitMeterInfo : public juce::Component {
 public:
  HitMeterInfo() {
    body_.setMultiLine(true, true);
    body_.setReadOnly(true);
    body_.setScrollbarsShown(true);
    body_.setCaretVisible(false);
    body_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0b0b0c));
    body_.setColour(juce::TextEditor::outlineColourId, juce::Colours::black);
    body_.setColour(juce::TextEditor::textColourId, Theme::cream());
    body_.setIndents(16, 12);
    addAndMakeVisible(body_);
    write();
    setSize(560, 600);
  }
  void resized() override { body_.setBounds(getLocalBounds()); }

 private:
  void head(const juce::String& s) {
    body_.setFont(Theme::spaced(Theme::uiFont(13.5f, true), 0.12f));  // (the editor has one text colour: headings are bold capitals)
    body_.insertTextAtCaret(s.toUpperCase() + "\n");
  }
  void para(const juce::String& s) {
    body_.setFont(Theme::uiFont(14.5f));
    body_.insertTextAtCaret(s + "\n\n");
  }
  void write() {
    head("Hit Record Meter");
    para("Chris Johnson's (Airwindows) meter. It doesn't just show how loud the mix is: it shows how three things move "
         "over time, peaks, brightness and bass, as clouds of dots that sweep from left to right (about 23 columns a "
         "second). It rewards variety and balance, not maximum levels: a good mix fills each band with an even, "
         "lively cloud, and keeps the three in balance with each other.");
    head("Top band: zero crosses (authority)");
    para("How long the waveform goes before it crosses the middle line again. Long stretches mean low frequencies, so "
         "the scale reads 35 Hz at the bottom up to 1 kHz. Louder bass pushes dots down; treble breaks the stretches up.\n"
         "Aim for a cloud spread over the whole range, deep bass included, not one flat line. A single resonant bass "
         "note gives one height; a kick that drops in pitch, a bass line that moves, and well-filled low mids spread it "
         "out. Gaps with a lone line at the bottom usually mean the low mids are missing. Too much RMS loudness, clipped "
         "peaks or boosted top end take this cloud away.");
    head("Middle band: peaks (power)");
    para("The highest level in each moment, from -36 dB up to 0 dB. Red bars at the top are clipping (longer bars: "
         "longer clipping).\n"
         "Power is peaks that jump out beyond the body of the sound, and keep varying. Aim for a tall cloud with "
         "activity at many levels at once. Anything that sustains (drones, heavy saturation, long reverbs, held bass) "
         "props up the bottom of the cloud; turning those down or shortening them lets it open up. Constant loudness "
         "and heavy limiting squeeze it into a narrow band: the mix gets louder but less exciting.");
    head("Bottom band: slews (detail)");
    para("How fast the waveform changes from one sample to the next: treble and transient detail, marked absent, "
         "present and exciting.\n"
         "Aim for balance: always at the top sounds hard and glaring, always low sounds dull. Match detail to the power "
         "of the peaks, not only to the overall loudness.");
    head("Dots and lines");
    para("Blue dots: peak and brightness are in balance (the bigger, the better balanced). Red: brightness is ahead "
         "of the peak. Green: the peak is ahead of the brightness (in the dark view, the louder the paler).\n"
         "The light lines are one-second averages (zero cross, level, slew); how thick they get shows how much the "
         "sound departs from that average, which is the variety the meter likes.");
    head("Letters and score");
    para("The line at the top reads: a number (only in the best zone), the overall letter, then authority, power and "
         "detail. Each band's letter climbs as its cloud fills evenly and stays that way: from lowercase (q, p, o... a) "
         "up through capitals (A, B, C...); higher is better, and it only goes up when the music keeps earning it.\n"
         "The overall letter (Z up to A) is a running tally of how well the three bands balance each other through the "
         "whole song. Loud, busy passages count more than quiet intros and fades. Chris compares it to ketchup: sweet, "
         "salt, sour and bitter all at full strength, with none of them taking over.");
    head("Tone bar and hints");
    para("The strip along the top is the balance as a colour: red leans bright (slews), green leans bass (zero "
         "crosses), blue leans peaks. Neutral means balanced (white in the plugin, near black in the dark view). Until "
         "the mix reaches the top zone, a short hint appears, such as \"less bright\", \"allow fullness\", "
         "\"allow detail\", \"allow activity\" or \"more calm\": what you would do to even things out.");
    head("Using it");
    para("Play the whole song: the score builds up over time. Compare with a reference you trust (Reference A/B: the "
         "meter measures what you hear, B included). Change one thing at a time and watch the band it belongs to. "
         "Different styles can all score well at very different loudness: it's the balance that counts.\n"
         "Click the meter to freeze it, double-click to start over. The dark view draws the same measurements as the "
         "plugin; make the dock taller (about 370 px) for the full layout with labels.");
    body_.moveCaretToTop(false);
  }
  juce::TextEditor body_;
};

// The small round (i) in the meter dock's header.
class InfoDot : public juce::Button {
 public:
  InfoDot() : juce::Button("info") { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
  void paintButton(juce::Graphics& g, bool over, bool down) override {
    auto& t = T();
    auto r = getLocalBounds().toFloat().reduced(2.f);
    r = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()), juce::jmin(r.getWidth(), r.getHeight()));
    if (down) r = r.translated(0.f, 1.f);
    g.setColour(over ? t.am() : t.amd()); g.drawEllipse(r.reduced(0.75f), 1.5f);
    g.setFont(Theme::uiFont(r.getHeight() * 0.72f, true));
    g.drawText("i", r.translated(0.f, -0.5f), juce::Justification::centred, false);
  }
};

}  // namespace ui
