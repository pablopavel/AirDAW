// Tempo map editing, shared by the song overview's bar ruler and the Audio settings list.
// Bar 1 is the metronome's own tempo and beats (Tap, Audio settings); every other entry is a change at the start of a bar.
#pragma once
#include <algorithm>
#include "AppCtx.h"
#include "Theme.h"

namespace ui {

struct TempoEdit {
  static std::vector<airdaw::TempoChange> changes(airdaw::Engine& e) {
    auto* m = e.tempoMap(); return m ? m->changes : std::vector<airdaw::TempoChange>{};
  }
  // The tempo / beats in force at a 1-based bar.
  static airdaw::TempoChange at(airdaw::Engine& e, int bar) {
    airdaw::TempoChange t{1, e.metronome().bpm.load(), e.metronome().beatsPerBar.load()};
    for (auto& c : changes(e)) if (c.bar <= bar) t = c;
    return t;
  }
  static bool has(airdaw::Engine& e, int bar) { for (auto& c : changes(e)) if (c.bar == bar) return true; return false; }
  // Sets bar `bar` to bpm / beats (bar 1: the metronome); adds the change if there is none.
  static void set(airdaw::Engine& e, int bar, float bpm, int beats) {
    bpm = juce::jlimit(20.f, 400.f, bpm); beats = juce::jlimit(1, 16, beats);
    if (bar <= 1) { e.metronome().bpm = bpm; e.metronome().beatsPerBar = beats; return; }
    auto c = changes(e); bool found = false;
    for (auto& x : c) if (x.bar == bar) { x.bpm = bpm; x.beatsPerBar = beats; found = true; }
    if (!found) c.push_back({bar, bpm, beats});
    e.setTempoMap(std::move(c));
  }
  static void remove(airdaw::Engine& e, int bar) {
    auto c = changes(e);
    c.erase(std::remove_if(c.begin(), c.end(), [bar](const airdaw::TempoChange& x) { return x.bar == bar; }), c.end());
    e.setTempoMap(std::move(c));
  }
  // Moves the change at `from` to `to` (refused onto bar 1 or onto another change).
  static bool move(airdaw::Engine& e, int from, int to) {
    if (to < 2 || from == to || has(e, to)) return false;
    auto c = changes(e);
    for (auto& x : c) if (x.bar == from) x.bar = to;
    e.setTempoMap(std::move(c));
    return true;
  }
  static juce::String label(float bpm, int beats) {
    return juce::String(bpm, std::abs(bpm - std::round(bpm)) < 0.05f ? 0 : 1) + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) +
           juce::String(beats) + "/4";
  }
};

// The small editor that opens from the ruler: tempo, beats per bar, and (not for bar 1) Delete.
class TempoEditor : public juce::Component {
 public:
  TempoEditor(AppCtx& ctx, int bar) : ctx_(ctx), bar_(bar) {
    bpm_.setSliderStyle(juce::Slider::IncDecButtons); bpm_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, 26);
    bpm_.setRange(20.0, 400.0, 1.0);
    for (int b = 1; b <= 16; ++b) beats_.addItem(juce::String(b) + "/4", b);
    if (auto* e = ctx_.engine()) { const auto t = TempoEdit::at(*e, bar_); bpm_.setValue(t.bpm, juce::dontSendNotification); beats_.setSelectedId(t.beatsPerBar, juce::dontSendNotification); }
    bpm_.onValueChange = [this] { apply(); }; beats_.onChange = [this] { apply(); };
    del_.setButtonText("Delete change"); del_.setEnabled(bar_ > 1);
    del_.onClick = [this] {
      if (auto* e = ctx_.engine()) TempoEdit::remove(*e, bar_);
      if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&bpm_, &beats_, &del_}) addAndMakeVisible(c);
    setSize(300, bar_ > 1 ? 150 : 116);
  }
  void paint(juce::Graphics& g) override {
    g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(12.f, true), 0.14f));
    g.drawText(bar_ > 1 ? "FROM BAR " + juce::String(bar_) : juce::String("BAR 1  (THE METRONOME)"), 12, 8, getWidth() - 24, 20,
               juce::Justification::centredLeft);
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(13.f));
    g.drawText("Tempo", 12, bpm_.getY(), 80, 26, juce::Justification::centredLeft);
    g.drawText("Beats / bar", 12, beats_.getY(), 80, 26, juce::Justification::centredLeft);
  }
  void resized() override {
    bpm_.setBounds(100, 34, 180, 26);
    beats_.setBounds(100, 70, 90, 26);
    del_.setBounds(12, 110, 140, 26);
  }
 private:
  void apply() { if (auto* e = ctx_.engine()) TempoEdit::set(*e, bar_, (float)bpm_.getValue(), beats_.getSelectedId()); }
  AppCtx& ctx_;
  const int bar_;
  juce::Slider bpm_;
  juce::ComboBox beats_;
  juce::TextButton del_;
};

// The list in the Audio settings: one row per change (from bar, tempo, beats, remove) and "+ Add change".
class TempoList : public juce::Component {
 public:
  explicit TempoList(AppCtx& ctx) : ctx_(ctx) {
    add_.setButtonText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Add change")));
    add_.setTooltip("Adds a tempo / time-signature change four bars after the last one (or at bar 5)");
    add_.onClick = [this] {
      auto* e = ctx_.engine(); if (!e) return;
      auto c = TempoEdit::changes(*e);
      const int bar = c.empty() ? 5 : c.back().bar + 4;
      const auto t = TempoEdit::at(*e, bar);
      TempoEdit::set(*e, bar, t.bpm, t.beatsPerBar);
      rebuild();
    };
    addAndMakeVisible(add_);
    rebuild();
  }
  static constexpr int kRowH = 30;
  int preferredHeight() const { return (int)rows_.size() * kRowH + 34; }
  std::function<void()> onHeightChanged;
  // Picks up changes made elsewhere (the ruler), unless a row is being edited.
  void sync() {
    auto* e = ctx_.engine(); if (!e) return;
    if (e->tempoMap() != shown_ && !isEditing()) rebuild();
  }
  void resized() override {
    int y = 0;
    for (auto& r : rows_) { r->setBounds(0, y, getWidth(), kRowH); y += kRowH; }
    add_.setBounds(0, y + 4, 140, 26);
  }

 private:
  struct Row : public juce::Component {
    Row(TempoList& o, const airdaw::TempoChange& c) : owner(o), bar(c.bar) {
      barS.setSliderStyle(juce::Slider::IncDecButtons); barS.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
      barS.setRange(2, 999, 1); barS.setValue(c.bar, juce::dontSendNotification);
      bpm.setSliderStyle(juce::Slider::IncDecButtons); bpm.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 24);
      bpm.setRange(20.0, 400.0, 1.0); bpm.setValue(c.bpm, juce::dontSendNotification);
      for (int b = 1; b <= 16; ++b) beats.addItem(juce::String(b) + "/4", b);
      beats.setSelectedId(c.beatsPerBar, juce::dontSendNotification);
      del.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95"))); del.getProperties().set("noUpper", true);
      del.setTooltip("Remove this change");
      barS.onDragEnd = [this] { moveTo(); };
      barS.onValueChange = [this] { if (!barS.isMouseButtonDown()) moveTo(); };
      bpm.onValueChange = [this] { write(); }; beats.onChange = [this] { write(); };
      del.onClick = [this] { if (auto* e = owner.ctx_.engine()) TempoEdit::remove(*e, bar); juce::MessageManager::callAsync([sp = juce::Component::SafePointer<TempoList>(&owner)] { if (sp) sp->rebuild(); }); };
      for (juce::Component* k : std::initializer_list<juce::Component*>{&barS, &bpm, &beats, &del}) addAndMakeVisible(k);
    }
    void paint(juce::Graphics& g) override {
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(13.f));
      g.drawText("From bar", 0, 0, 70, getHeight(), juce::Justification::centredLeft);
    }
    void resized() override {
      auto r = getLocalBounds().reduced(0, 2); r.removeFromLeft(70);
      barS.setBounds(r.removeFromLeft(110)); r.removeFromLeft(10);
      bpm.setBounds(r.removeFromLeft(150)); r.removeFromLeft(10);
      beats.setBounds(r.removeFromLeft(76)); r.removeFromLeft(10);
      del.setBounds(r.removeFromLeft(30));
    }
    void write() { if (auto* e = owner.ctx_.engine()) { TempoEdit::set(*e, bar, (float)bpm.getValue(), beats.getSelectedId()); owner.shown_ = e->tempoMap(); } }
    void moveTo() {
      auto* e = owner.ctx_.engine(); if (!e) return;
      const int to = (int)barS.getValue();
      if (to == bar) return;
      if (TempoEdit::move(*e, bar, to)) { bar = to; owner.shown_ = e->tempoMap(); }
      else barS.setValue(bar, juce::dontSendNotification);  // that bar already has a change
    }
    TempoList& owner;
    int bar;
    juce::Slider barS, bpm;
    juce::ComboBox beats;
    juce::TextButton del;
  };
  bool isEditing() const {
    for (auto& r : rows_) if (r->barS.isMouseButtonDown() || r->bpm.isMouseButtonDown() || r->beats.isPopupActive() || r->hasKeyboardFocus(true)) return true;
    return false;
  }
  void rebuild() {
    auto* e = ctx_.engine();
    rows_.clear();
    if (e) { shown_ = e->tempoMap(); for (auto& c : TempoEdit::changes(*e)) { rows_.push_back(std::make_unique<Row>(*this, c)); addAndMakeVisible(*rows_.back()); } }
    resized();
    if (onHeightChanged) onHeightChanged();
  }
  AppCtx& ctx_;
  std::vector<std::unique_ptr<Row>> rows_;
  juce::TextButton add_;
  const airdaw::TempoMap* shown_ = nullptr;
};

}  // namespace ui
