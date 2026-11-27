#include "Mixer.h"

namespace ui {

void Mixer::restyle() {
  for (auto& s : tracks_) s->resized();
  for (auto& s : buses_) s->resized();
  master_.resized();
}

Mixer::Mixer(AppCtx& ctx) : ctx_(ctx), master_(ctx) {
  for (int i = 0; i < airdaw::kMaxTracks; ++i) { tracks_.push_back(std::make_unique<Strip>(ctx, i)); content_.addChildComponent(*tracks_.back()); }
  for (int b = 0; b < airdaw::kNumBuses; ++b) { buses_.push_back(std::make_unique<Strip>(ctx, 100 + b)); content_.addAndMakeVisible(*buses_.back()); }
  add_.getProperties().set("kind", "flat");
  add_.onClick = [this] {  // a removed slot comes back first, else the next new one
    for (int t = 0; t < shown_; ++t)
      if (removed_[(size_t)t]) { removed_[(size_t)t] = false; relayout(); if (onRemovedChanged) onRemovedChanged(); return; }
    if (shown_ < airdaw::kMaxTracks) { ++shown_; relayout(); }
  };
  content_.addAndMakeVisible(add_);
  view_.setViewedComponent(&content_, false);
  view_.setScrollBarsShown(false, true);
  view_.setScrollBarThickness(9);
  addAndMakeVisible(view_);
  addAndMakeVisible(master_);
}

// A track is "in use" if anything about it differs from a fresh track.
static bool inUse(airdaw::Engine& e, int t) {
  auto& c = e.track(t);
  if (c.armed.load() || e.numTakes(t) > 0 || c.inputL.load() >= 0 || c.name != "TRACK " + std::to_string(t + 1)) return true;
  for (int s = 0; s < airdaw::kMaxInserts; ++s) if (e.insertAt(t, s)) return true;
  return false;
}

void Mixer::sync() {
  auto* e = ctx_.engine(); if (!e) return;
  int need = 8;
  bool unhid = false;
  for (int t = 0; t < airdaw::kMaxTracks; ++t)
    if (inUse(*e, t)) {
      need = juce::jmax(need, t + 1);
      if (removed_[(size_t)t]) { removed_[(size_t)t] = false; unhid = true; }  // something was put on a removed track: show it
    }
  if (need > shown_ || unhid) { shown_ = juce::jmax(shown_, need); relayout(); }
  if (unhid && onRemovedChanged) onRemovedChanged();
  for (int t = 0; t < shown_; ++t) if (!removed_[(size_t)t]) tracks_[(size_t)t]->sync();
  for (auto& b : buses_) b->sync();
  master_.sync();
}

void Mixer::relayout() {
  const int h = juce::jmax(200, view_.getHeight() - (view_.isHorizontalScrollBarShown() ? view_.getScrollBarThickness() : 0));
  int x = 0, visible = 0;
  for (int t = 0; t < airdaw::kMaxTracks; ++t) {
    auto& s = *tracks_[(size_t)t];
    const bool show = t < shown_ && !removed_[(size_t)t];
    s.setVisible(show);
    if (show) { s.setBounds(x, 0, Strip::kWidth, h); x += Strip::kWidth; ++visible; }
  }
  add_.setVisible(visible < airdaw::kMaxTracks);
  if (add_.isVisible()) {
    add_.setButtonText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Add track\n")) + juce::String(visible) + "/24");
    add_.setBounds(x + 8, 8, 74, h - 16); x += 90;
  }
  for (auto& b : buses_) { b->setBounds(x, 0, Strip::kWidth, h); x += Strip::kWidth; }
  content_.setSize(x, h);
}

void Mixer::resized() {
  auto r = getLocalBounds();
  if (listen_) { master_.setBounds(r); return; }
  master_.setBounds(r.removeFromRight(MasterStrip::kWidth));
  view_.setBounds(r);
  relayout();
  // Re-run once the scrollbar visibility is known (it changes the usable height).
  relayout();
}

void Mixer::removeTrack(int t) {
  if (t < 0 || t >= airdaw::kMaxTracks || removed_[(size_t)t]) return;
  removed_[(size_t)t] = true;
  relayout();
  if (onRemovedChanged) onRemovedChanged();
}

void Mixer::setRemovedTracks(const juce::var& list) {
  removed_.fill(false);
  if (auto* a = list.getArray())
    for (auto& v : *a) { const int t = (int)v; if (t >= 0 && t < airdaw::kMaxTracks) removed_[(size_t)t] = true; }
  relayout();
}

juce::var Mixer::removedTracks() const {
  juce::Array<juce::var> a;
  for (int t = 0; t < airdaw::kMaxTracks; ++t) if (removed_[(size_t)t]) a.add(t);
  return a;
}

void Mixer::paint(juce::Graphics& g) { g.fillAll(T().pb()); }
void Mixer::paintOverChildren(juce::Graphics& g) {
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), T().rad(6.f), 1.f);
}

}  // namespace ui
