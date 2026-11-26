#include "Tools.h"
#include <deque>

namespace ui {

static juce::String num(float v, int dec, float floor = -100.f) { return v > floor ? juce::String(v, dec) : juce::String("-"); }

// A label/value row like the mockup's .cr / .tg rows.
static void row(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value,
                juce::Colour valueCol = {}, float valueH = 14.f) {
  auto& t = T();
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.f), 0.1f));
  g.drawText(label, r, juce::Justification::centredLeft);
  g.setFont(Theme::monoFont(valueH));
  t.drawGlowText(g, value, r, juce::Justification::centredRight, valueCol.isTransparent() ? t.am() : valueCol, 0.5f);
  g.setColour(t.am().withAlpha(0.16f)); g.fillRect(r.getX(), r.getBottom() - 1.f, r.getWidth(), 1.f);
}

// Segmented horizontal bar (mockup .hb). from/to: 0..1 of the width.
static void hbar(juce::Graphics& g, juce::Rectangle<float> r, float from, float to, juce::Colour col = {}) {
  auto& t = T();
  t.drawWell(g, r, juce::Colour(0xff030303), 2.f);
  from = juce::jlimit(0.f, 1.f, from); to = juce::jlimit(0.f, 1.f, to);
  if (to <= from) return;
  auto f = r.withX(r.getX() + from * r.getWidth()).withWidth((to - from) * r.getWidth());
  t.drawGlow(g, f, col.isTransparent() ? t.am() : col, 7.f, 0.35f);
  for (float x = f.getX(); x < f.getRight(); x += 6.f) {
    const float p = (x - r.getX()) / r.getWidth();
    g.setColour(!col.isTransparent() ? col : p > 0.9f ? juce::Colour(0xffe5484d) : p > 0.7f ? t.amLight() : t.am());
    g.fillRect(x, f.getY(), juce::jmin(5.f, f.getRight() - x), f.getHeight());
  }
}

// ---------------------------------------------------------------- Stereo field (goniometer)
class StereoField : public Tool {
 public:
  void update(const AnalysisHub& h) override {
    h.latestScope(pts_, 1400);
    corr_ = h.loud().rms > -70.f ? h.loud().correlation : 0.f;
    live_ = h.loud().rms > -70.f;
    repaint();
  }
  juce::String headerValue() const override { return live_ ? (corr_ >= 0 ? "+" : "") + juce::String(corr_, 2) : juce::String(); }
  void paint(juce::Graphics& g) override {
    auto& t = T();
    auto b = getLocalBounds().toFloat().reduced(4.f);
    g.setColour(juce::Colour(0xff020202)); g.fillRoundedRectangle(b, 3.f);
    const float s = juce::jmin(b.getWidth(), b.getHeight()), r = s / 2.f - 8.f, cx = b.getCentreX(), cy = b.getCentreY();
    g.setColour(t.am().withAlpha(0.18f));
    g.drawLine(cx - r, cy + r, cx + r, cy - r); g.drawLine(cx - r, cy - r, cx + r, cy + r);
    g.drawLine(cx, cy - r, cx, cy + r); g.drawLine(cx - r, cy, cx + r, cy);
    g.drawEllipse(cx - r, cy - r, 2 * r, 2 * r, 1.f);
    g.setColour(t.amd()); g.setFont(Theme::uiFont(10.f));
    g.drawText("L", juce::Rectangle<float>(cx - r - 2, cy - r - 2, 12, 12), juce::Justification::centred);
    g.drawText("R", juce::Rectangle<float>(cx + r - 10, cy - r - 2, 12, 12), juce::Justification::centred);
    g.drawText("M", juce::Rectangle<float>(cx + 2, cy - r - 2, 12, 12), juce::Justification::centredLeft);
    if (!live_) return;
    // Rotated 45 degrees: mid up, side sideways. Auto-gain so quiet mixes still fill the scope.
    float mx = 1e-4f; for (float v : pts_) mx = juce::jmax(mx, std::abs(v));
    const float k = r / juce::jmax(0.05f, mx) * 0.7071f;
    g.setColour(t.am().withAlpha(0.75f));
    for (size_t i = 0; i + 1 < pts_.size(); i += 2) {
      const float l = pts_[i], rr = pts_[i + 1];
      g.fillRect(cx + (rr - l) * k, cy - (l + rr) * k, 1.6f, 1.6f);
    }
  }
  int defaultWidth() const override { return 300; }
 private:
  std::vector<float> pts_;
  float corr_ = 0.f; bool live_ = false;
};

// ---------------------------------------------------------------- RTA, 31 bands
class Rta : public Tool {
 public:
  void update(const AnalysisHub& h) override {
    for (int b = 0; b < airdaw::BandAnalyzer::kBands; ++b) {
      const float v = h.band(b);
      lv_[(size_t)b] = v > lv_[(size_t)b] ? v : lv_[(size_t)b] - 1.2f;  // fast rise, steady fall
    }
    repaint();
  }
  void paint(juce::Graphics& g) override {
    auto& t = T();
    auto b = getLocalBounds().toFloat().reduced(4.f);
    g.setColour(juce::Colour(0xff020202)); g.fillRoundedRectangle(b, 3.f);
    auto plot = b.reduced(4.f).withTrimmedBottom(14.f);
    const int n = airdaw::BandAnalyzer::kBands;
    const float bw = plot.getWidth() / n, lo = -72.f, hi = 0.f;
    float sum = 0.f; int cnt = 0;
    for (int i = 0; i < n; ++i) {
      const float db = juce::jlimit(lo, hi, lv_[(size_t)i]);
      if (lv_[(size_t)i] > lo) { sum += lv_[(size_t)i]; ++cnt; }
      const float h = (db - lo) / (hi - lo) * plot.getHeight();
      for (float y = 0.f; y < h; y += 5.f) {
        const float f = y / plot.getHeight();
        g.setColour(f > 0.85f ? juce::Colour(0xffe5484d) : f > 0.65f ? t.amLight() : t.am());
        g.fillRect(plot.getX() + i * bw + 1.f, plot.getBottom() - y - 3.f, bw - 3.f, 3.f);
      }
    }
    // Reference: pink noise reads flat in 1/3-octave bands; drawn at the mix's average band level.
    if (cnt > 0) {
      const float y = plot.getBottom() - (juce::jlimit(lo, hi, sum / cnt) - lo) / (hi - lo) * plot.getHeight();
      const float dash[] = {4.f, 4.f};
      g.setColour(juce::Colour(0x77ffffff)); g.drawDashedLine({plot.getX(), y, plot.getRight(), y}, dash, 2, 1.f);
    }
    g.setColour(t.amd()); g.setFont(Theme::uiFont(10.f));
    const char* lab[] = {"31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
    for (int i = 0; i < 10; ++i) {
      const int band = 2 + i * 3;  // 31.5, 63, 125, ... 16k
      g.drawText(lab[i], juce::Rectangle<float>(plot.getX() + band * bw - 10.f, plot.getBottom() + 2.f, bw + 20.f, 12.f), juce::Justification::centred);
    }
  }
  juce::String headerValue() const override { return juce::String(juce::CharPointer_UTF8("\xe2\x94\x84 pink (flat)")); }
  int defaultWidth() const override { return 520; }
  Rta() { lv_.fill(-200.f); }  // bars start silent and rise with the signal
 private:
  std::array<float, airdaw::BandAnalyzer::kBands> lv_{};
};

// ---------------------------------------------------------------- Loudness panel (mockup's stacked column)
class LoudnessPanel : public Tool {
 public:
  void update(const AnalysisHub& h) override { r_ = h.loud(); repaint(); }
  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat().reduced(8.f, 2.f);
    const float rh = juce::jlimit(15.f, 26.f, (r.getHeight() - 158.f) / 5.f);  // the five value rows share what the bars leave
    row(g, r.removeFromTop(rh), "INTEGRATED", num(r_.integrated, 1, -70.f));
    row(g, r.removeFromTop(rh), "RANGE LRA", r_.integrated > -70.f ? juce::String(r_.lra, 1) + " LU" : "-");
    row(g, r.removeFromTop(rh), "PLR", r_.plr > 0.f ? juce::String(r_.plr, 1) + " dB" : "-");
    row(g, r.removeFromTop(rh), "CREST", r_.crest > 0.f ? juce::String(r_.crest, 1) + " dB" : "-");
    row(g, r.removeFromTop(rh), "DR", r_.dr > 0.f ? "DR" + juce::String(juce::roundToInt(r_.dr)) : "-");
    r.removeFromTop(4.f);
    auto& t = T();
    g.setColour(t.am().withAlpha(0.3f)); g.fillRect(r.removeFromTop(2.f)); r.removeFromTop(4.f);
    const bool live = r_.rms > -70.f;
    auto label = [&](const juce::String& a, const juce::String& b) {
      auto l = r.removeFromTop(15.f);
      g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.f), 0.14f));
      g.drawText(a, l, juce::Justification::centredLeft); g.drawText(b, l, juce::Justification::centredRight);
    };
    label("BALANCE  L", "R");
    { auto bar = r.removeFromTop(9.f); const float c = 0.5f + 0.5f * (live ? r_.balance : 0.f);
      hbar(g, bar, c - 0.02f, c + 0.02f); }
    r.removeFromTop(5.f);
    label("WIDTH", live ? juce::String(juce::roundToInt(r_.width)) + "%" : "-");
    hbar(g, r.removeFromTop(9.f), 0.f, live ? juce::jmin(1.f, r_.width / 100.f) : 0.f); r.removeFromTop(5.f);
    label("MID", num(r_.midDb, 1, -90.f));
    hbar(g, r.removeFromTop(9.f), 0.f, live ? juce::jmax(0.f, (r_.midDb + 60.f) / 60.f) : 0.f); r.removeFromTop(5.f);
    label("SIDE", num(r_.sideDb, 1, -90.f));
    hbar(g, r.removeFromTop(9.f), 0.f, live ? juce::jmax(0.f, (r_.sideDb + 60.f) / 60.f) : 0.f); r.removeFromTop(6.f);
    auto mono = r.removeFromTop(18.f);
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.f), 0.14f));
    g.drawText("MONO COMPAT.", mono, juce::Justification::centredLeft);
    const bool ok = r_.monoLoss > -3.f;
    g.setColour(!live ? Theme::dim() : ok ? juce::Colour(0xff7bd88f) : juce::Colour(0xffe5484d)); g.setFont(Theme::monoFont(12.f));
    g.drawText(!live ? juce::String("-") : ok ? juce::String("OK") : "CHECK " + juce::String(r_.monoLoss, 1) + " dB", mono, juce::Justification::centredRight);
  }
  int defaultWidth() const override { return 280; }
 private:
  airdaw::LoudnessReadout r_;
};

// ---------------------------------------------------------------- Target loudness
class TargetLoudness : public Tool {
 public:
  void update(const AnalysisHub& h) override { i_ = h.loud().integrated; repaint(); }
  juce::String headerValue() const override { return i_ > -70.f ? "I " + juce::String(i_, 1) : juce::String(); }
  void paint(juce::Graphics& g) override {
    struct T2 { const char* name; float lufs; };
    static const T2 targets[] = {{"Spotify", -14}, {"Apple Music", -16}, {"YouTube", -14}, {"Tidal", -14}, {"Amazon Music", -14}, {"Loud / club", -9}};
    auto r = getLocalBounds().toFloat().reduced(8.f, 2.f);
    const float rh = juce::jmin(28.f, r.getHeight() / 6.f);
    for (auto& t : targets) {
      auto row = r.removeFromTop(rh);
      g.setColour(Theme::tx()); g.setFont(Theme::uiFont(13.f));
      g.drawText(t.name, row, juce::Justification::centredLeft);
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(11.f));
      g.drawText(juce::String((int)t.lufs) + " LUFS", row.withTrimmedRight(78.f), juce::Justification::centredRight);
      // How much the platform's normalisation will change your track (+ turns it up, - turns it down).
      const float d = t.lufs - i_;
      g.setFont(Theme::monoFont(13.f));
      T().drawGlowText(g, i_ > -70.f ? (d >= 0 ? "+" : "") + juce::String(d, 1) + " dB" : juce::String("-"), row, juce::Justification::centredRight,
                       d < -3.f ? juce::Colour(0xffe5484d) : T().am(), 0.5f);
      g.setColour(T().am().withAlpha(0.12f)); g.fillRect(row.getX(), row.getBottom() - 1.f, row.getWidth(), 1.f);
    }
  }
 private:
  float i_ = -200.f;
};

// ---------------------------------------------------------------- Tonal balance (7 bands from the 31-band analyser)
class TonalBalance : public Tool {
 public:
  void update(const AnalysisHub& h) override {
    // band groups by analyser index: sub 20-50, bass 63-200, low-mid 250-400, mid 500-1.6k, hi-mid 2k-3.15k, presence 4k-6.3k, air 8k-20k
    static const int edges[8] = {0, 5, 11, 14, 20, 23, 26, 31};
    double total = 0; std::array<double, 7> e{};
    for (int g2 = 0; g2 < 7; ++g2) for (int b = edges[g2]; b < edges[g2 + 1]; ++b) { const float db = h.band(b); if (db > -150.f) e[(size_t)g2] += std::pow(10.0, db / 10.0); }
    for (double x : e) total += x;
    for (int g2 = 0; g2 < 7; ++g2) {
      const float rel = (e[(size_t)g2] > 0 && total > 0) ? (float)(10.0 * std::log10(e[(size_t)g2] / total)) : -60.f;
      v_[(size_t)g2] += 0.3f * (rel - v_[(size_t)g2]);
    }
    live_ = h.loud().rms > -70.f; repaint();
  }
  void paint(juce::Graphics& g) override {
    static const char* names[7] = {"Sub", "Bass", "Low-mid", "Mid", "Hi-mid", "Presence", "Air"};
    auto r = getLocalBounds().toFloat().reduced(8.f, 2.f);
    const float rh = juce::jmin(26.f, r.getHeight() / 7.f);
    for (int i = 0; i < 7; ++i) {
      auto row = r.removeFromTop(rh);
      g.setColour(Theme::tx()); g.setFont(Theme::uiFont(12.f));
      g.drawText(names[i], row.removeFromLeft(66.f), juce::Justification::centredLeft);
      g.setColour(Theme::dim()); g.setFont(Theme::monoFont(11.f, false));
      g.drawText(live_ ? juce::String(v_[(size_t)i], 1) : juce::String("-"), row.removeFromRight(44.f), juce::Justification::centredRight);
      hbar(g, row.withSizeKeepingCentre(row.getWidth() - 6.f, 9.f), 0.f, live_ ? juce::jlimit(0.f, 1.f, (v_[(size_t)i] + 30.f) / 30.f) : 0.f);
    }
  }
  juce::String headerValue() const override { return "dB of mix"; }
 private:
  std::array<float, 7> v_{-60, -60, -60, -60, -60, -60, -60};
  bool live_ = false;
};

// ---------------------------------------------------------------- Spectrogram (31 bands, scrolling)
class Spectrogram : public Tool {
 public:
  void update(const AnalysisHub& h) override {
    const int w = juce::jmax(10, getWidth()), hh = juce::jmax(10, getHeight());
    if (img_.getWidth() != w || img_.getHeight() != hh) { img_ = juce::Image(juce::Image::RGB, w, hh, true); }
    if (!h.playing() && h.loud().rms < -70.f) return;  // hold the picture while nothing plays
    img_.moveImageSection(0, 0, 2, 0, w - 2, hh);      // scroll left
    juce::Graphics g(img_);
    const int n = airdaw::BandAnalyzer::kBands;
    for (int b = 0; b < n; ++b) {
      const float v = juce::jlimit(0.f, 1.f, (h.band(b) + 72.f) / 66.f);
      const float y0 = hh - (float)(b + 1) * hh / n, y1 = hh - (float)b * hh / n;
      g.setColour(ramp(v)); g.fillRect((float)w - 2.f, y0, 2.f, y1 - y0);
    }
    repaint();
  }
  void paint(juce::Graphics& g) override {
    g.setColour(juce::Colour(0xff020202)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
    if (img_.isValid()) g.drawImageAt(img_, 0, 0);
    g.setColour(T().amd()); g.setFont(Theme::uiFont(9.f));
    g.drawText("20k", 3, 2, 30, 10, juce::Justification::left); g.drawText("20", 3, getHeight() - 12, 30, 10, juce::Justification::left);
  }
 private:
  static juce::Colour ramp(float v) {  // dark -> accent -> cream
    auto& t = T();
    return v < 0.6f ? juce::Colour(0xff0a0603).interpolatedWith(t.am(), v / 0.6f) : t.am().interpolatedWith(juce::Colour(0xfffff0c0), (v - 0.6f) / 0.4f);
  }
  juce::Image img_;
};

// ---------------------------------------------------------------- Loudness history
class LoudnessHistory : public Tool {
 public:
  void update(const AnalysisHub& h) override { pts_ = h.history(); repaint(); }
  juce::String headerValue() const override { return pts_.empty() ? juce::String() : juce::String(pts_.back(), 1) + " LUFS S"; }
  void paint(juce::Graphics& g) override {
    auto& t = T();
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff020202)); g.fillRoundedRectangle(b, 3.f);
    auto yOf = [&](float v) { return b.getBottom() - (juce::jlimit(-30.f, -6.f, v) + 30.f) / 24.f * b.getHeight(); };
    g.setFont(Theme::uiFont(9.f));
    for (int v : {-10, -14, -18, -22, -26}) {
      const float y = yOf((float)v);
      g.setColour(t.am().withAlpha(v == -14 ? 0.45f : 0.18f));
      if (v == -14) { const float d[] = {5.f, 4.f}; g.drawDashedLine({b.getX(), y, b.getRight(), y}, d, 2, 1.f); }
      else g.drawHorizontalLine((int)y, b.getX(), b.getRight());
      g.setColour(t.amd()); g.drawText(juce::String(v), (int)b.getX() + 3, (int)y - 11, 30, 10, juce::Justification::left);
    }
    if (pts_.size() < 2) { g.setColour(Theme::dim()); g.setFont(Theme::uiFont(12.f)); g.drawText("Plays from here once you press Play", b, juce::Justification::centred); return; }
    const size_t n = std::min<size_t>(240, pts_.size()), start = pts_.size() - n;  // last 4 minutes
    juce::Path p;
    for (size_t i = 0; i < n; ++i) {
      const float x = b.getX() + (float)i / 239.f * b.getWidth(), y = yOf(pts_[start + i]);
      if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    if (t.glow > 0.f) { g.setColour(t.am().withAlpha(t.glowAlpha(0.25f))); g.strokePath(p, juce::PathStrokeType(5.f)); }
    g.setColour(t.am()); g.strokePath(p, juce::PathStrokeType(2.f));
  }
 private:
  std::vector<float> pts_;
};

// ---------------------------------------------------------------- Band phase / width
class BandPhase : public Tool {
 public:
  void update(const AnalysisHub& h) override {
    live_ = h.loud().rms > -70.f;
    for (int i = 0; i < 4; ++i) c_[(size_t)i] = h.bandCorrelation(i);
    loss_ = h.loud().monoLoss; repaint();
  }
  void paint(juce::Graphics& g) override {
    static const char* names[4] = {"Sub <120", "Low-mid", "Mid", "High"};
    auto r = getLocalBounds().toFloat().reduced(8.f, 2.f);
    const float rh = juce::jmin(30.f, r.getHeight() / 5.f);
    for (int i = 0; i < 4; ++i) {
      auto row = r.removeFromTop(rh);
      g.setColour(Theme::tx()); g.setFont(Theme::uiFont(12.f));
      g.drawText(names[i], row.removeFromLeft(70.f), juce::Justification::centredLeft);
      const float c = c_[(size_t)i];
      g.setFont(Theme::monoFont(12.f));
      T().drawGlowText(g, live_ ? (c >= 0 ? "+" : "") + juce::String(c, 2) : juce::String("-"), row.removeFromRight(52.f), juce::Justification::centredRight,
                       c < 0.f ? juce::Colour(0xffe5484d) : T().am(), 0.5f);
      auto bar = row.withSizeKeepingCentre(row.getWidth() - 6.f, 9.f);  // from the centre, like the master's correlation bar
      if (live_) hbar(g, bar, c >= 0 ? 0.5f : 0.5f + c * 0.5f, c >= 0 ? 0.5f + c * 0.5f : 0.5f, c < 0 ? juce::Colour(0xffe5484d) : juce::Colour());
      else hbar(g, bar, 0, 0);
    }
    row(g, r.removeFromTop(rh), "Mono-compat. loss", live_ ? juce::String(loss_, 1) + " dB" : juce::String("-"), loss_ < -3.f ? juce::Colour(0xffe5484d) : juce::Colour());
  }
 private:
  std::array<float, 4> c_{};
  float loss_ = 0.f; bool live_ = false;
};

// ---------------------------------------------------------------- Headroom
class Headroom : public Tool {
 public:
  Headroom() {
    reset_.setButtonText("Reset hold");
    reset_.onClick = [this] { if (ctx && ctx->resetPeaks) ctx->resetPeaks(); };
    addAndMakeVisible(reset_);
  }
  void update(const AnalysisHub& h) override { r_ = h.loud(); repaint(); }
  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat().reduced(8.f, 2.f).withTrimmedBottom(40.f);
    auto big = r.removeFromTop(r.getHeight() * 0.45f);
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.f), 0.1f));
    g.drawText("PEAK HOLD", big, juce::Justification::centredLeft);
    g.setFont(Theme::monoFont(30.f));
    T().drawGlowText(g, r_.truePeak > -90.f ? juce::String(r_.truePeak, 1) : juce::String("-"), big, juce::Justification::centredRight,
                     r_.truePeak > -1.f ? juce::Colour(0xffe5484d) : T().am(), 1.f);
    const float rh = r.getHeight() / 2.f;
    row(g, r.removeFromTop(rh), "TO 0 dBTP", r_.truePeak > -90.f ? juce::String(-r_.truePeak, 1) + " dB" : juce::String("-"));
    row(g, r.removeFromTop(rh), "OVERS", juce::String(r_.overs), r_.overs > 0 ? juce::Colour(0xffe5484d) : juce::Colour());
  }
  void resized() override { reset_.setBounds(getLocalBounds().removeFromBottom(32).reduced(8, 2)); }
 private:
  juce::TextButton reset_;
  airdaw::LoudnessReadout r_;
};

// ---------------------------------------------------------------- Listening notes
class Notes : public Tool {
 public:
  Notes() {
    add_.setButtonText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Note at playhead")));
    add_.onClick = [this] {
      if (!ctx) return;
      ctx->notes.items.push_back({playhead_, {}}); ctx->notes.sort(); ctx->notes.save();
      focusTime_ = playhead_; rebuild();
    };
    addAndMakeVisible(add_);
    view_.setViewedComponent(&list_, false); view_.setScrollBarsShown(true, false);
    addAndMakeVisible(view_);
  }
  void update(const AnalysisHub& h) override {
    playhead_ = h.playheadSeconds;
    if (ctx && key() != key_) rebuild();
  }
  void paint(juce::Graphics& g) override {
    if (ctx && ctx->notes.items.empty()) {
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(12.f));
      g.drawFittedText("No notes yet. They also show as markers on the overview.", view_.getBounds().reduced(4), juce::Justification::topLeft, 3);
    }
  }
  void resized() override {
    auto r = getLocalBounds();
    add_.setBounds(r.removeFromTop(30).reduced(4, 2));
    view_.setBounds(r.reduced(0, 4));
    layoutRows();
  }
 private:
  juce::String key() const {
    juce::String k; for (auto& n : ctx->notes.items) k << n.seconds << ";"; k << ctx->notes.file.getFullPathName(); return k;
  }
  static juce::String mmss(double s) { return juce::String::formatted("%02d:%02d", (int)s / 60, (int)s % 60); }
  void rebuild() {
    rows_.clear(); key_ = key();
    for (size_t i = 0; i < ctx->notes.items.size(); ++i) {
      auto rw = std::make_unique<Row>();
      const double t = ctx->notes.items[i].seconds;
      rw->time.setButtonText(mmss(t)); rw->time.getProperties().set("fontH", 11.0);
      rw->time.setTooltip("Jump the playhead here");
      rw->time.onClick = [this, t] { if (auto* e = ctx->engine()) e->transport().seek((juce::int64)(t * e->sampleRate())); };
      rw->text.setText(ctx->notes.items[i].text, false);
      rw->text.setTextToShowWhenEmpty(juce::String(juce::CharPointer_UTF8("note\xe2\x80\xa6")), Theme::dim());
      rw->text.onTextChange = [this, t, ed = &rw->text] { for (auto& n : ctx->notes.items) if (n.seconds == t) n.text = ed->getText(); ctx->notes.save(); };
      rw->del.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95"))); rw->del.getProperties().set("noUpper", true);
      rw->del.onClick = [this, t] {
        auto& v = ctx->notes.items;
        v.erase(std::remove_if(v.begin(), v.end(), [t](auto& n) { return n.seconds == t; }), v.end());
        ctx->notes.save();
        juce::Component::SafePointer<Notes> sp(this);
        juce::MessageManager::callAsync([sp] { if (sp) sp->rebuild(); });  // this row is being deleted
      };
      for (juce::Component* c : std::initializer_list<juce::Component*>{&rw->time, &rw->text, &rw->del}) list_.addAndMakeVisible(c);
      if (t == focusTime_) { focusTime_ = -1; juce::MessageManager::callAsync([ed = juce::Component::SafePointer<juce::TextEditor>(&rw->text)] { if (ed) ed->grabKeyboardFocus(); }); }
      rows_.push_back(std::move(rw));
    }
    layoutRows(); repaint();
  }
  void layoutRows() {
    const int w = juce::jmax(100, view_.getWidth() - 10);
    for (size_t i = 0; i < rows_.size(); ++i) {
      juce::Rectangle<int> r(0, (int)i * 30, w, 26);
      rows_[i]->time.setBounds(r.removeFromLeft(58)); r.removeFromLeft(4);
      rows_[i]->del.setBounds(r.removeFromRight(26)); r.removeFromRight(4);
      rows_[i]->text.setBounds(r);
    }
    list_.setSize(w, (int)rows_.size() * 30);
  }
  struct Row { juce::TextButton time, del; juce::TextEditor text; };
  juce::TextButton add_;
  juce::Viewport view_;
  juce::Component list_;
  std::vector<std::unique_ptr<Row>> rows_;
  juce::String key_;
  double playhead_ = 0.0, focusTime_ = -1.0;
};

// ---------------------------------------------------------------- Ear break
class EarBreak : public Tool {
 public:
  EarBreak() {
    for (int i = 0; i < 3; ++i) {
      auto& b = mins_[(size_t)i]; const int m = 30 + 15 * i;
      b.setButtonText(juce::String(m) + " min"); b.getProperties().set("fontH", 11.0);
      b.onClick = [this, m] { if (ctx) { ctx->props.setValue("earbreak.minutes", m); } sync(); };
      addAndMakeVisible(b);
    }
    took_.setButtonText("I took a break"); took_.onClick = [this] { listened_ = 0.0; sync(); };
    on_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x8f\xbb"))); on_.getProperties().set("noUpper", true);
    on_.setTooltip("Ear-break timer on/off");
    on_.onClick = [this] { if (ctx) ctx->props.setValue("earbreak.on", !enabled()); sync(); };
    addAndMakeVisible(took_); addAndMakeVisible(on_);
  }
  void update(const AnalysisHub& h) override {
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (last_ > 0 && h.playing() && enabled()) listened_ += now - last_;  // counts while playing
    last_ = now;
    sync(); repaint();
  }
  juce::String headerValue() const override { return enabled() ? juce::String() : juce::String("off"); }
  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat().reduced(8.f, 2.f);
    auto top = r.removeFromTop(44.f).withTrimmedRight(34.f);
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(11.f), 0.1f));
    g.drawText("LISTENING", top, juce::Justification::centredLeft);
    const int s = (int)listened_;
    g.setFont(Theme::monoFont(30.f));
    T().drawGlowText(g, juce::String::formatted("%02d:%02d", s / 60, s % 60), top, juce::Justification::centredRight,
                     enabled() ? T().am() : Theme::dim(), enabled() ? 1.f : 0.f);
    hbar(g, r.removeFromTop(9.f), 0.f, enabled() ? (float)juce::jlimit(0.0, 1.0, listened_ / (minutes() * 60.0)) : 0.f);
    r.removeFromTop(40.f);
    g.setColour(due() ? T().am() : Theme::dim()); g.setFont(Theme::uiFont(12.f));
    g.drawFittedText(!enabled() ? juce::String("Timer is off")
                     : due() ? juce::String("Time for a break: rest your ears for 10 minutes")
                             : "Break reminder at " + juce::String(minutes()) + " min of playback",
                     r.removeFromTop(32.f).toNearestInt(), juce::Justification::centredLeft, 2);
  }
  void resized() override {
    auto r = getLocalBounds().reduced(8, 2);
    on_.setBounds(r.getRight() - 28, r.getY() + 8, 28, 26);
    r.removeFromTop(44 + 9 + 8);
    auto row = r.removeFromTop(26); const int w = (row.getWidth() - 8) / 3;
    for (auto& b : mins_) { b.setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); }
    took_.setBounds(getLocalBounds().removeFromBottom(32).reduced(8, 2));
  }
 private:
  bool enabled() const { return !ctx || ctx->props.getBoolValue("earbreak.on", true); }
  int minutes() const { return ctx ? ctx->props.getIntValue("earbreak.minutes", 45) : 45; }
  bool due() const { return enabled() && listened_ >= minutes() * 60.0; }
  void sync() {
    for (int i = 0; i < 3; ++i) mins_[(size_t)i].setToggleState(minutes() == 30 + 15 * i, juce::dontSendNotification);
    on_.setToggleState(enabled(), juce::dontSendNotification);
  }
  std::array<juce::TextButton, 3> mins_;
  juce::TextButton took_, on_;
  double listened_ = 0.0, last_ = 0.0;
};

std::unique_ptr<Tool> createTool(const juce::String& id) {
  if (id == "stereo") return std::make_unique<StereoField>();
  if (id == "rta") return std::make_unique<Rta>();
  if (id == "loudness") return std::make_unique<LoudnessPanel>();
  if (id == "target") return std::make_unique<TargetLoudness>();
  if (id == "tonal") return std::make_unique<TonalBalance>();
  if (id == "spectrogram") return std::make_unique<Spectrogram>();
  if (id == "history") return std::make_unique<LoudnessHistory>();
  if (id == "bandphase") return std::make_unique<BandPhase>();
  if (id == "headroom") return std::make_unique<Headroom>();
  if (id == "notes") return std::make_unique<Notes>();
  if (id == "earbreak") return std::make_unique<EarBreak>();
  return nullptr;
}

}  // namespace ui
