#include "Mixer.h"
#include <algorithm>

namespace ui {

void Mixer::restyle() {
  for (auto& s : tracks_) s->resized();
  for (auto& s : buses_) s->resized();
  master_.resized();
}

Mixer::Mixer(AppCtx& ctx) : ctx_(ctx), master_(ctx) {
  for (int i = 0; i < airdaw::kMaxTracks; ++i) { tracks_.push_back(std::make_unique<Strip>(ctx, i)); content_.addChildComponent(*tracks_.back()); }
  for (int b = 0; b < airdaw::kNumBuses; ++b) { buses_.push_back(std::make_unique<Strip>(ctx, 100 + b)); content_.addAndMakeVisible(*buses_.back()); }
  resetOrder();
  ctx_.moveStrip = [this](int moving, int onto, bool after) { moveStrip(moving, onto, after); };
  add_.getProperties().set("kind", "flat");
  add_.onClick = [this] {  // a removed slot comes back first (at the end of the strips), else the next new one
    for (int t = 0; t < shown_; ++t)
      if (removed_[(size_t)t]) {
        removed_[(size_t)t] = false;
        int last = -1;
        for (int i = 0; i < (int)order_.size(); ++i) { const int u = order_[(size_t)i]; if (u != t && u < shown_ && !removed_[(size_t)u]) last = u; }
        if (last >= 0) moveStrip(t, last, true);
        relayout(); if (onRemovedChanged) onRemovedChanged(); return;
      }
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
  for (int t : order_) {
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
  for (int b : busOrder_) { buses_[(size_t)(b - 100)]->setBounds(x, 0, Strip::kWidth, h); x += Strip::kWidth; }
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

int Mixer::claimTrack() {
  for (int t = 0; t < shown_; ++t)
    if (removed_[(size_t)t]) { removed_[(size_t)t] = false; relayout(); if (onRemovedChanged) onRemovedChanged(); return t; }
  if (shown_ < airdaw::kMaxTracks) { ++shown_; relayout(); return shown_ - 1; }
  return -1;
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

void Mixer::resetOrder() {
  order_.clear(); busOrder_.clear();
  for (int t = 0; t < airdaw::kMaxTracks; ++t) order_.push_back(t);
  for (int b = 0; b < airdaw::kNumBuses; ++b) busOrder_.push_back(100 + b);
}

void Mixer::moveStrip(int moving, int onto, bool after) {
  auto& v = isBus(moving) ? busOrder_ : order_;
  auto from = std::find(v.begin(), v.end(), moving);
  if (from == v.end() || moving == onto || isBus(moving) != isBus(onto)) return;
  v.erase(from);
  auto at = std::find(v.begin(), v.end(), onto);
  if (at == v.end()) v.push_back(moving); else v.insert(after ? at + 1 : at, moving);
  relayout();
  if (onOrderChanged) onOrderChanged();
}

// A saved order: a list of track ids then bus ids. Anything missing or unknown is put right (a project from before
// has no list: the plain order).
void Mixer::setStripOrder(const juce::var& list) {
  resetOrder();
  if (auto* a = list.getArray()) {
    std::vector<int> t, b;
    for (auto& v : *a) {
      const int id = (int)v;
      if (isTrack(id) && std::find(t.begin(), t.end(), id) == t.end()) t.push_back(id);
      if (isBus(id) && std::find(b.begin(), b.end(), id) == b.end()) b.push_back(id);
    }
    for (int id : order_) if (std::find(t.begin(), t.end(), id) == t.end()) t.push_back(id);
    for (int id : busOrder_) if (std::find(b.begin(), b.end(), id) == b.end()) b.push_back(id);
    order_ = t; busOrder_ = b;
  }
  relayout();
}

juce::var Mixer::stripOrder() const {
  juce::Array<juce::var> a;
  for (int id : order_) a.add(id);
  for (int id : busOrder_) a.add(id);
  return a;
}

void Mixer::paint(juce::Graphics& g) { g.fillAll(T().pb()); }
void Mixer::paintOverChildren(juce::Graphics& g) {
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), T().rad(6.f), 1.f);
}

}  // namespace ui
