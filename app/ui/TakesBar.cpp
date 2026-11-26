#include "TakesBar.h"

namespace ui {

static juce::String mmss(double sec) { const int s = (int)sec; return juce::String::formatted("%02d:%02d", s / 60, s % 60); }

TakesBar::TakesBar(AppCtx& ctx) : ctx_(ctx) {
  for (juce::Component* c : std::initializer_list<juce::Component*>{&region_, &loop_, &keep_, &del_, &clear_, &view_}) addAndMakeVisible(c);
  view_.setViewedComponent(&row_, false);
  view_.setScrollBarsShown(false, true, false, true);
  view_.setScrollBarThickness(6);
  loop_.setTooltip("Loop on/off (Shift+drag on the overview to set a new region)");
  loop_.onClick = [this] { if (auto* e = ctx_.engine()) e->transport().loopOn = !e->transport().loopOn.load(); };
  keep_.setTooltip("Star the selected take so Delete unkept leaves it alone");
  keep_.onClick = [this] {
    auto* e = ctx_.engine(); const int t = track(); if (!e || t < 0) return;
    for (int i = 0; i < e->numTakes(t); ++i)
      if (e->takeAt(t, i) == e->track(t).take.load()) { e->setTakeKeep(t, i, !e->takeAt(t, i)->keep); key_.clear(); return; }
    ctx_.note("Click a take first, then keep it");
  };
  del_.setTooltip("Remove the takes without a star (in this loop region) and move their files to the Recycle Bin");
  del_.onClick = [this] { deleteUnkept(); };
  clear_.setTooltip("Remove the loop region");
  clear_.onClick = [this] { if (auto* e = ctx_.engine()) { auto& tr = e->transport(); tr.loopOn = false; tr.loopIn = 0; tr.loopOut = 0; } };
}

int TakesBar::track() const {
  auto* e = ctx_.engine(); if (!e) return -1;
  if (ctx_.viewTrack >= 0) return ctx_.viewTrack;
  for (int t = 0; t < airdaw::kMaxTracks; ++t) if (e->track(t).armed.load()) return t;
  return -1;
}

bool TakesBar::wanted() const {
  auto* e = ctx_.engine(); if (!e) return false;
  return ctx_.viewTrack >= 0 || e->transport().loopOut.load() > e->transport().loopIn.load();
}

// With a loop region: the takes recorded in that region (each loop pass starts at the loop start).
// Without one: every take on the track.
std::vector<int> TakesBar::scope(airdaw::Engine& e, int t) const {
  std::vector<int> v;
  const auto li = e.transport().loopIn.load(), lo = e.transport().loopOut.load();
  for (int i = 0; i < e.numTakes(t); ++i) {
    const auto* tk = e.takeAt(t, i);
    if (lo <= li || (tk->start >= li - 1 && tk->start <= li + 1)) v.push_back(i);
  }
  return v;
}

void TakesBar::sync() {
  auto* e = ctx_.engine(); if (!e || !isVisible()) return;
  const int t = track();
  auto& tr = e->transport();
  juce::String k; k << t << "|" << tr.loopIn.load() << "|" << tr.loopOut.load() << "|" << (int)tr.loopOn.load();
  if (t >= 0) {
    k << "|" << juce::String::toHexString((juce::pointer_sized_int)e->track(t).take.load()) << "|" << (int)e->track(t).armed.load();
    for (int i = 0; i < e->numTakes(t); ++i) k << ";" << juce::String::toHexString((juce::pointer_sized_int)e->takeAt(t, i)) << (e->takeAt(t, i)->keep ? "*" : "");
  }
  if (k != key_) { key_ = k; rebuild(); }
}

void TakesBar::rebuild() {
  auto* e = ctx_.engine(); if (!e) return;
  chips_.clear();
  const int t = track();
  auto& tr = e->transport();
  const bool region = tr.loopOut.load() > tr.loopIn.load();
  const double sr = e->sampleRate();
  region_.setVisible(region); loop_.setVisible(region); clear_.setVisible(region);
  if (region) {
    region_.setText(mmss((double)tr.loopIn.load() / sr) + juce::String(juce::CharPointer_UTF8("\xe2\x80\x93")) + mmss((double)tr.loopOut.load() / sr));
    loop_.setButtonText(tr.loopOn.load() ? "Loop on" : "Loop off");
    loop_.setToggleState(tr.loopOn.load(), juce::dontSendNotification);
  }
  title_.clear(); hint_.clear();
  if (t < 0) { hint_ = "Arm (R) or view (T) a track: takes are stored per track"; keep_.setVisible(false); del_.setVisible(false); resized(); repaint(); return; }
  auto& ch = e->track(t);
  title_ = "TAKES " + juce::String(juce::CharPointer_UTF8("\xc2\xb7 ")) + juce::String(ch.name).toUpperCase() + (ch.armed.load() ? juce::String(juce::CharPointer_UTF8("  \xe2\x97\x8f armed")) : juce::String());
  const auto inScope = scope(*e, t);
  std::vector<int> shown = inScope;
  if (region)  // kept takes from other regions of this track are listed after the loop's takes
    for (int i = 0; i < e->numTakes(t); ++i)
      if (e->takeAt(t, i)->keep && std::find(inScope.begin(), inScope.end(), i) == inScope.end()) shown.push_back(i);
  int n = 0;
  const auto* active = ch.take.load();
  for (int i : shown) {
    const auto* tk = e->takeAt(t, i);
    auto c = std::make_unique<Chip>();
    c->index = i; c->active = tk == active; c->kept = tk->keep;
    const bool own = std::find(inScope.begin(), inScope.end(), i) != inScope.end();
    const double a = (double)tk->start / sr, b = a + (double)tk->l.size() / sr;
    c->label = own ? (region ? "Take " + juce::String(++n) : "Take " + juce::String(++n) + "  " + mmss(a))
                   : mmss(a) + juce::String(juce::CharPointer_UTF8("\xe2\x80\x93")) + mmss(b);
    for (size_t s = 0; s < tk->l.size(); ++s) {  // mini waveform: 24 peak buckets
      const size_t bkt = s * 24 / juce::jmax<size_t>(1, tk->l.size());
      c->peaks[bkt] = juce::jmax(c->peaks[bkt], std::abs(tk->l[s]));
    }
    float mx = 0.f; for (float p : c->peaks) mx = juce::jmax(mx, p);
    if (mx > 0.f) for (float& p : c->peaks) p = std::pow(p / mx, 0.7f);
    c->onPick = [this, t](int idx) { if (auto* en = ctx_.engine()) { en->setActiveTake(t, idx); key_.clear(); } };
    c->setTooltip(juce::File(juce::String(tk->path)).getFileName());
    row_.addAndMakeVisible(*c); chips_.push_back(std::move(c));
  }
  if (inScope.empty())
    hint_ = ch.armed.load() ? (region ? "Press " + juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f")) + " Rec with Loop on: every pass adds a take to " + juce::String(ch.name)
                                      : "Press Rec to record a take on " + juce::String(ch.name))
                            : "No takes here yet: arm " + juce::String(ch.name) + " (R) to record some";
  bool activeKept = false;
  for (int i = 0; i < e->numTakes(t); ++i) if (e->takeAt(t, i) == active) activeKept = e->takeAt(t, i)->keep;
  keep_.setVisible(!shown.empty()); del_.setVisible(!inScope.empty());
  keep_.setButtonText(juce::String(juce::CharPointer_UTF8(activeKept ? "\xe2\x98\x85 Kept" : "\xe2\x98\x85 Keep")));
  keep_.setToggleState(activeKept, juce::dontSendNotification);
  resized(); repaint();
}

void TakesBar::deleteUnkept() {
  auto* e = ctx_.engine(); const int t = track(); if (!e || t < 0) return;
  std::vector<int> doomed;
  for (int i : scope(*e, t)) if (!e->takeAt(t, i)->keep) doomed.push_back(i);
  if (doomed.empty()) { ctx_.note("Every take here is starred: nothing to delete"); return; }
  if (e->transport().recording.load()) { ctx_.note("Stop recording first"); return; }
  const juce::String name(e->track(t).name);
  juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
      .withTitle("Delete unkept takes")
      .withMessage("Remove " + juce::String((int)doomed.size()) + " take(s) without a star from " + name +
                   "?\nTheir files go to the Recycle Bin, so you can still get them back.")
      .withButton("Delete", 1).withButton("Cancel", 0),
      [this, t, sp = juce::Component::SafePointer<TakesBar>(this)](int r) {
        if (!sp || r != 1) return;
        auto* en = ctx_.engine(); if (!en) return;
        int removed = 0;
        auto list = scope(*en, t);
        for (auto it = list.rbegin(); it != list.rend(); ++it) {  // highest index first: removal shifts later ones
          const auto* tk = en->takeAt(t, *it); if (tk->keep) continue;
          juce::File(juce::String::fromUTF8(tk->path.c_str())).moveToTrash();
          en->removeTake(t, *it); ++removed;
        }
        if (!en->track(t).take.load())  // nothing playing any more: fall back to a kept take in this region
          for (int i : scope(*en, t)) if (en->takeAt(t, i)->keep) { en->setActiveTake(t, i); break; }
        ctx_.note("Deleted " + juce::String(removed) + " take(s); the files are in the Recycle Bin. Save to keep the change.");
        key_.clear();
      });
}

void TakesBar::Chip::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat().reduced(0.5f);
  if (active) {
    t.drawGlow(g, r, t.am(), 12.f, 0.35f);
    g.setGradientFill(juce::ColourGradient(t.c(0xffffc46b), 0, r.getY(), t.c(0xffd98e24), 0, r.getBottom(), false));
  } else g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c3c3f), 0, r.getY(), juce::Colour(0xff262628), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, T().rad(4.f));
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, T().rad(4.f), 1.f);
  const juce::Colour ink = active ? juce::Colour(0xff2a1800) : kept ? t.am() : Theme::tx();
  auto tr = r.reduced(9.f, 0.f);
  auto wave = tr.removeFromRight(72.f).reduced(0.f, 7.f);
  g.setColour(ink); g.setFont(Theme::spaced(Theme::uiFont(12.f), 0.04f));
  g.drawText((kept ? juce::String(juce::CharPointer_UTF8("\xe2\x98\x85 ")) : juce::String()) + label, tr, juce::Justification::centredLeft, true);
  const float bw = wave.getWidth() / 24.f;
  for (int i = 0; i < 24; ++i) {
    const float h = juce::jmax(1.f, peaks[(size_t)i] * wave.getHeight());
    g.fillRect(wave.getX() + i * bw, wave.getCentreY() - h / 2.f, bw * 0.7f, h);
  }
}

void TakesBar::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  T().drawBrush(g, r);
  g.setColour(juce::Colours::black); g.fillRect(r.removeFromBottom(2.f));
  auto in = getLocalBounds().reduced(16, 0);
  g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(13.f, true), 0.16f));
  g.drawText("LOOP", in.removeFromLeft(56), juce::Justification::centredLeft);
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.5f), 0.14f));
  g.drawText(title_, titleArea_, juce::Justification::centredLeft, true);
  if (hint_.isNotEmpty() && chips_.empty()) {
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(13.f));
    g.drawText(hint_, view_.getBounds(), juce::Justification::centredLeft, true);
  }
}

void TakesBar::resized() {
  auto r = getLocalBounds().reduced(16, 8);
  r.removeFromLeft(60);
  if (region_.isVisible()) { region_.setBounds(r.removeFromLeft(120)); r.removeFromLeft(8); loop_.setBounds(r.removeFromLeft(90).reduced(0, 2)); r.removeFromLeft(12); }
  for (auto* b : {&clear_, &del_, &keep_}) if (b->isVisible()) { b->setBounds(r.removeFromRight(b->getBestWidthForHeight(28) + 20).reduced(0, 2)); r.removeFromRight(8); }
  titleArea_ = r.removeFromLeft(juce::jmin(240, r.getWidth() / 3));
  view_.setBounds(r);
  int x = 0;
  for (auto& c : chips_) { const int w = 86 + juce::roundToInt(juce::GlyphArrangement::getStringWidth(Theme::uiFont(12.f), c->label)) + 24;
                           c->setBounds(x, 2, w, r.getHeight() - 10); x += w + 6; }
  row_.setSize(juce::jmax(x, 10), r.getHeight());
}

}  // namespace ui
