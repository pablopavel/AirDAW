// MIDI Learn overlay (mockup body.learn): every mappable control gets a pulsing dashed outline, mapped ones a
// solid outline and a dot; click a control to arm it (solid glow), then move a knob or press a pad.
// Right-click a control to remove its mapping. The banner has "Mappings..." and "Done".
// Plus the mappings window: every mapping with its source, mode (pick-up / relative / button behaviour), remove.
#pragma once
#include "MidiControl.h"
#include "../ui/Theme.h"

namespace ui {

class MidiLearnOverlay : public juce::Component, private juce::Timer {
 public:
  MidiLearnOverlay(MidiControl& mc, juce::Component& root) : mc_(mc), root_(root) {
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    for (auto* b : {&list_, &done_}) { addAndMakeVisible(*b); b->getProperties().set("fontH", 11.0); }
    list_.setButtonText(juce::String(juce::CharPointer_UTF8("Mappings\xe2\x80\xa6")));
    done_.setButtonText("Done");
    list_.onClick = [this] { if (onShowList) onShowList(); };
    done_.onClick = [this] { mc_.setLearning(false); };
  }
  std::function<void()> onShowList;
  std::function<juce::Rectangle<int>()> passThrough;  // area that keeps working (the deck's MIDI Learn button)

  void start() { refresh(); startTimerHz(20); grabKeyboardFocus(); }
  void stop() { stopTimer(); }

  bool hitTest(int x, int y) override { return !(passThrough && passThrough().contains(x, y)); }
  bool keyPressed(const juce::KeyPress& k) override {
    if (k == juce::KeyPress::escapeKey) { if (mc_.armed().isNotEmpty()) mc_.arm({}); else mc_.setLearning(false); return true; }
    return false;
  }

  void paint(juce::Graphics& g) override {
    auto& t = T();
    const float pulse = 0.35f + 0.45f * (0.5f + 0.5f * std::sin((float)juce::Time::getMillisecondCounter() * 0.0052f));
    for (auto& [id, r] : regions_) {
      const auto rf = r.toFloat().expanded(2.f);
      if (id == mc_.armed()) {
        t.drawGlow(g, rf, t.am(), 14.f, 0.8f);
        g.setColour(t.am()); g.drawRoundedRectangle(rf, 3.f, 2.f);
      } else if (mc_.mappingFor(id)) {
        g.setColour(t.am().withAlpha(0.85f)); g.drawRoundedRectangle(rf, 3.f, 1.2f);
        g.fillEllipse(rf.getRight() - 7.f, rf.getY() - 1.f, 7.f, 7.f);
      } else {
        juce::Path p; p.addRoundedRectangle(rf, 3.f);
        juce::Path d; const float dash[] = {4.f, 3.f};
        juce::PathStrokeType(1.f).createDashedStroke(d, p, dash, 2);
        g.setColour(t.am().withAlpha(pulse)); g.fillPath(d);
      }
    }
    // banner
    auto b = banner().toFloat();
    g.setColour(juce::Colour(0xf0100a03)); g.fillRoundedRectangle(b, 6.f);
    g.setColour(t.am()); g.drawRoundedRectangle(b.reduced(0.5f), 6.f, 1.f);
    auto txt = b.reduced(14.f, 0.f).withTrimmedRight(190.f);
    g.setFont(Theme::spaced(Theme::uiFont(11.5f, true), 0.14f)); g.setColour(t.am());
    g.drawText(mc_.armed().isNotEmpty() ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f  MOVE A CONTROLLER\xe2\x80\xa6  ")) + mc_.describe(mc_.armed()).toUpperCase()
                                        : juce::String("MIDI LEARN"), txt.removeFromTop(b.getHeight() / 2.f + 2.f), juce::Justification::bottomLeft, true);
    g.setFont(Theme::uiFont(11.5f)); g.setColour(Theme::tx());
    g.drawText(mc_.armed().isNotEmpty() ? "Turn a knob, move a fader or press a pad (Esc: cancel)"
                                        : "Click a control, then move a knob or press a pad.  Right-click: remove a mapping.  Esc: done" +
                                              (mc_.lastMessage().isNotEmpty() ? "      Last MIDI: " + mc_.lastMessage() : juce::String()),
               txt, juce::Justification::topLeft, true);
  }

  void resized() override {
    auto b = banner();
    done_.setBounds(b.removeFromRight(80).reduced(8, 9));
    list_.setBounds(b.removeFromRight(104).reduced(4, 9));
  }

  void mouseDown(const juce::MouseEvent& e) override {
    const juce::String id = regionAt(e.getPosition());
    if (e.mods.isPopupMenu()) {
      if (id.isEmpty()) return;
      juce::PopupMenu m;
      m.addSectionHeader(mc_.describe(id));
      if (auto* mp = mc_.mappingFor(id)) m.addItem(1, "Remove mapping (" + mp->sourceText() + ")");
      else m.addItem(1, "Not mapped", false);
      m.addItem(2, "Learn a controller for this");
      m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({e.getScreenX(), e.getScreenY(), 1, 1}), [this, id](int r) {
        if (r == 1) mc_.removeMapping(id); else if (r == 2) mc_.arm(id);
      });
      return;
    }
    mc_.arm(id == mc_.armed() ? juce::String() : id);
  }

  // Mappable areas in this overlay's coordinates (the overlay covers `root`).
  void refresh() {
    regions_.clear();
    // `clip`: the part of the window this component can show (scrolled viewports, strips under the master).
    std::function<void(juce::Component*, juce::Rectangle<int>)> walk = [&](juce::Component* c, juce::Rectangle<int> clip) {
      if (!c->isVisible() || c == this) return;
      clip = clip.getIntersection(root_.getLocalArea(c, c->getLocalBounds()));
      if (clip.isEmpty()) return;
      const auto id = c->getProperties()["midi"].toString();
      if (id.isNotEmpty()) regions_.push_back({id, clip});
      if (auto* p = dynamic_cast<MidiRegionProvider*>(c))
        for (auto& [rid, rr] : p->midiRegions()) {
          const auto r = root_.getLocalArea(c, rr).getIntersection(clip);
          if (!r.isEmpty()) regions_.push_back({rid, r});
        }
      for (auto* k : c->getChildren()) walk(k, clip);
    };
    for (auto* k : root_.getChildren()) walk(k, root_.getLocalBounds());
  }

 private:
  void timerCallback() override { refresh(); repaint(); }
  juce::Rectangle<int> banner() const { return getLocalBounds().withSizeKeepingCentre(juce::jmin(820, getWidth() - 40), 48).withY(getHeight() - 96); }
  juce::String regionAt(juce::Point<int> p) const {
    juce::String best; int bestArea = INT_MAX;  // the smallest area wins (a button inside a card)
    for (auto& [id, r] : regions_) if (r.expanded(2).contains(p) && r.getWidth() * r.getHeight() < bestArea) { best = id; bestArea = r.getWidth() * r.getHeight(); }
    return best;
  }
  MidiControl& mc_;
  juce::Component& root_;
  std::vector<std::pair<juce::String, juce::Rectangle<int>>> regions_;
  juce::TextButton list_, done_;
};

// Window listing every mapping: what it controls, where it comes from, how it behaves, remove.
class MidiMappingsContent : public juce::Component {
 public:
  explicit MidiMappingsContent(MidiControl& mc) : mc_(mc) {
    addAndMakeVisible(view_); view_.setViewedComponent(&rows_, false); view_.setScrollBarsShown(true, false);
    rebuild();
    setSize(760, 520);
  }
  void rebuild() {
    rowsList_.clear(); rows_.removeAllChildren();
    auto section = [&](const juce::String& title, bool project) {
      std::vector<const MidiMapping*> v;
      for (auto& m : mc_.mappings()) if (MidiControl::isProjectTarget(m.target) == project) v.push_back(&m);
      auto h = std::make_unique<Row>(); h->header = title + (v.empty() ? "  (none yet)" : ""); rows_.addAndMakeVisible(*h); rowsList_.push_back(std::move(h));
      std::sort(v.begin(), v.end(), [&](auto* a, auto* b) { return mc_.describe(a->target) < mc_.describe(b->target); });
      for (auto* m : v) {
        auto r = std::make_unique<Row>(); r->target = m->target; r->what = mc_.describe(m->target); r->src = m->sourceText();
        if (!MidiControl::isProjectTarget(m->target) || m->fx.isEmpty() || mc_.describe(m->target).contains(m->fx)) {} else r->what << "  (plugin " << m->fx << " not in this slot)";
        const bool cont = MidiControl::isContinuous(m->target);
        if (cont) { r->mode.addItem("Fixed knob (pick up)", 1); r->mode.addItem("Endless (64 = stop)", 2); r->mode.addItem(juce::String(juce::CharPointer_UTF8("Endless (1 / 127)")), 3); r->mode.addItem("Endless (sign bit)", 4); r->mode.addItem("Endless (16 = stop)", 7); }
        else { r->mode.addItem("Press toggles", 5); r->mode.addItem("Follows on / off", 6); }
        r->mode.setSelectedId((int)m->mode + 1, juce::dontSendNotification);
        const juce::String t = m->target;
        r->mode.onChange = [this, t, rp = r.get()] { mc_.setMode(t, (MidiMapping::Mode)(rp->mode.getSelectedId() - 1)); };
        juce::Component::SafePointer<MidiMappingsContent> self(this);  // (MSVC: no `this` in a nested init-capture)
        r->remove.onClick = [self, t] { juce::MessageManager::callAsync([self, t] { if (self) { self->mc_.removeMapping(t); self->rebuild(); } }); };
        r->addAndMakeVisible(r->mode); r->addAndMakeVisible(r->remove);
        rows_.addAndMakeVisible(*r); rowsList_.push_back(std::move(r));
      }
    };
    section(juce::String(juce::CharPointer_UTF8("DAW CONTROLS  \xc2\xb7  all projects")), false);
    section(juce::String(juce::CharPointer_UTF8("PLUGIN CONTROLS  \xc2\xb7  this project")), true);
    resized();
  }
  void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0b0b0c)); }
  void resized() override {
    view_.setBounds(getLocalBounds().reduced(8));
    int y = 0; const int w = view_.getWidth() - view_.getScrollBarThickness();
    for (auto& r : rowsList_) { const int h = r->header.isNotEmpty() ? 34 : 30; r->setBounds(0, y, w, h); y += h; }
    rows_.setSize(w, y);
  }
 private:
  struct Row : juce::Component {
    juce::String header, target, what, src;
    juce::ComboBox mode; juce::TextButton remove{"Remove"};
    void paint(juce::Graphics& g) override {
      auto& t = T();
      if (header.isNotEmpty()) {
        g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(11.5f, true), 0.16f));
        g.drawText(header, getLocalBounds().withTrimmedLeft(4).withTrimmedTop(10), juce::Justification::centredLeft);
        return;
      }
      auto r = getLocalBounds().withTrimmedRight(300).reduced(4, 0);
      g.setColour(Theme::tx()); g.setFont(Theme::uiFont(12.5f)); g.drawText(what, r.removeFromLeft(r.getWidth() * 55 / 100), juce::Justification::centredLeft, true);
      g.setColour(t.am()); g.setFont(Theme::monoFont(11.5f, false)); g.drawText(src, r, juce::Justification::centredLeft, true);
      g.setColour(juce::Colour(0x14ffffff)); g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
    void resized() override {
      auto r = getLocalBounds().reduced(2, 3);
      remove.setBounds(r.removeFromRight(80)); r.removeFromRight(6); mode.setBounds(r.removeFromRight(206));
    }
  };
  MidiControl& mc_;
  juce::Viewport view_;
  juce::Component rows_;
  std::vector<std::unique_ptr<Row>> rowsList_;
};

class MidiMappingsWindow : public juce::DocumentWindow {
 public:
  explicit MidiMappingsWindow(MidiControl& mc) : DocumentWindow("AirDAW - MIDI mappings", juce::Colour(0xff0b0b0c), DocumentWindow::closeButton) {
    content_ = new MidiMappingsContent(mc);
    setUsingNativeTitleBar(true); setContentOwned(content_, true); setResizable(true, false);
    centreWithSize(getWidth(), getHeight()); setVisible(true);
  }
  void rebuild() { content_->rebuild(); }
  void closeButtonPressed() override { setVisible(false); }
 private:
  MidiMappingsContent* content_;
};

}  // namespace ui
