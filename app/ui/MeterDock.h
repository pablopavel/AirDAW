// Meter dock (mockup #awm): the real Airwindows Hit Record Meter VST3 editor, pinned on the left, then the
// reference suite: the tools the user picked (ToolSet), side by side with draggable dividers.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AppCtx.h"
#include "Theme.h"
#include "tools/AnalysisHub.h"
#include "tools/HitMeterInfo.h"
#include "tools/ToolSet.h"

namespace ui {

// Vertical divider between dock columns; drag to resize the column on its left.
struct ColumnSplitter : Divider { ColumnSplitter() : Divider(true) {} };  // a vertical divider (Widgets.h)

// The reference-suite part of the dock: chosen tools in order; tools can be dropped in from Listen mode.
class SuiteArea : public juce::Component, public juce::DragAndDropTarget {
 public:
  explicit SuiteArea(ToolSet& tools) : tools_(tools) {}
  void rebuild() {  // take the suite's cards into this area (and let go of the ones that left the suite)
    for (auto& s : splitters_) removeChildComponent(s.get());
    splitters_.clear();
    for (int i = getNumChildComponents(); --i >= 0;)
      if (auto* c = dynamic_cast<ToolCard*>(getChildComponent(i)); c && !tools_.inSuite(c->id())) removeChildComponent(c);
    for (auto& id : tools_.suite()) {
      auto& c = tools_.card(id);
      c.setPlace(ToolCard::Place::Suite);
      addAndMakeVisible(c);
    }
    for (size_t i = 0; i + 1 < tools_.suite().size(); ++i) {
      auto sp = std::make_unique<ColumnSplitter>();
      const juce::String id = tools_.suite()[i];
      sp->current = [this, id] { return tools_.card(id).getWidth(); };
      sp->onDrag = [this, id](int w) {  // stops where the tools to the right would no longer fit
        const int mx = juce::jmax(kMinW, maxWidthFor(id));
        tools_.setWidth(id, juce::jlimit(juce::jmin(160, mx), mx, w)); resized();
      };
      addAndMakeVisible(*sp); splitters_.push_back(std::move(sp));
    }
    resized(); repaint();
  }
  // Columns get their saved widths, the last one takes the rest. When the meter is dragged wide and space runs
  // out, columns shrink to kMinW; after that the tools nearest the meter fold into slim tabs (kTabW), and when
  // even the tabs do not fit the suite tucks away entirely.
  static constexpr int kMinW = 140, kTabW = 28, kGap = 14;
  void resized() override {
    const auto& ids = tools_.suite();
    const int n = (int)ids.size();
    if (n == 0) return;
    const bool none = getWidth() < n * kTabW + (n - 1) * 4;
    for (auto& id : ids) tools_.card(id).setVisible(!none);
    for (auto& s : splitters_) s->setVisible(!none);
    if (none) return;
    int folded = 0;  // tools folded into tabs, counted from the left (the meter side)
    while (folded < n && folded * kTabW + (n - folded) * kMinW + (n - 1) * kGap > getWidth()) ++folded;
    std::vector<int> w((size_t)n, kTabW);
    const int open = n - folded;
    if (open > 0) {
      const int avail = getWidth() - folded * kTabW - (n - 1) * kGap;
      int fixed = 0;
      for (int i = folded; i < n - 1; ++i) { w[(size_t)i] = juce::jmax(kMinW, tools_.width(ids[(size_t)i])); fixed += w[(size_t)i]; }
      const int budget = avail - kMinW;  // the last tool keeps at least kMinW
      if (fixed > budget) {  // not enough room: each open column gives up part of what it has above kMinW
        const int need = fixed - budget, above = fixed - (open - 1) * kMinW;
        for (int i = folded; i < n - 1 && above > 0; ++i) {
          const int ex = w[(size_t)i] - kMinW;
          w[(size_t)i] -= juce::jmin(ex, (int)std::ceil((double)need * ex / above));
        }
        fixed = 0; for (int i = folded; i < n - 1; ++i) fixed += w[(size_t)i];
        for (int i = n - 2; i >= folded && fixed > budget; --i) {  // rounding leftovers
          const int c = juce::jmin(fixed - budget, w[(size_t)i] - kMinW); w[(size_t)i] -= c; fixed -= c;
        }
      }
      w[(size_t)n - 1] = juce::jmax(kMinW, avail - fixed);  // the last tool takes the rest: the row always ends at the edge
    }
    int x = 0;
    for (int i = 0; i < n; ++i) {
      tools_.card(ids[(size_t)i]).setBounds(x, 0, w[(size_t)i], getHeight()); x += w[(size_t)i];
      if (i < (int)splitters_.size()) {
        splitters_[(size_t)i]->setVisible(i >= folded);  // no resizing a folded tab
        splitters_[(size_t)i]->setBounds(x, 0, kGap, getHeight()); x += kGap;
      }
    }
  }
  // The widest a tool may be dragged: the other columns keep their widths and the last one keeps kMinW, so no tool is
  // pushed off the right end.
  int maxWidthFor(const juce::String& id) const {
    const auto& ids = tools_.suite();
    const int n = (int)ids.size();
    int others = 0;
    for (int i = 0; i < n - 1; ++i) if (ids[(size_t)i] != id) others += tools_.card(ids[(size_t)i]).getWidth();
    return getWidth() - (n - 1) * kGap - others - kMinW;
  }
  void paint(juce::Graphics& g) override {
    if (tools_.suite().empty()) {
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(13.f));
      g.drawFittedText(juce::String(juce::CharPointer_UTF8("No reference tools chosen. Add some with \xef\xbc\x8b Tools, or drag them up from Listen mode.")),
                       getLocalBounds().reduced(20), juce::Justification::centred, 2);
    }
  }
  void paintOverChildren(juce::Graphics& g) override {
    if (dropX_ >= 0) { T().drawGlow(g, {(float)dropX_ - 1.f, 0.f, 3.f, (float)getHeight()}, T().am(), 12.f, 0.9f);
                       g.setColour(T().am()); g.fillRect(dropX_ - 1, 0, 3, getHeight()); }
  }
  // drag and drop from the Listen row (or within the suite)
  bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("tool:"); }
  void itemDragMove(const SourceDetails& d) override { dropX_ = insertX(indexAt(d.localPosition.x)); repaint(); }
  void itemDragExit(const SourceDetails&) override { dropX_ = -1; repaint(); }
  void itemDropped(const SourceDetails& d) override {
    dropX_ = -1; repaint();
    const auto id = d.description.toString().fromFirstOccurrenceOf("tool:", false, false);
    int idx = indexAt(d.localPosition.x);
    const auto& s = tools_.suite();
    auto cur = std::find(s.begin(), s.end(), id);
    if (cur != s.end() && (int)(cur - s.begin()) < idx) --idx;  // moving right within the suite
    juce::MessageManager::callAsync([this, id, idx] { tools_.addToSuite(id, idx); });  // re-layout after the drop returns
  }
 private:
  int indexAt(int x) const {
    int i = 0;
    for (auto& id : tools_.suite()) { auto& c = tools_.card(id); if (x < c.getX() + c.getWidth() / 2) return i; ++i; }
    return i;
  }
  int insertX(int idx) const {
    const auto& s = tools_.suite();
    if (s.empty()) return 6;
    if (idx < (int)s.size()) return juce::jmax(2, tools_.card(s[(size_t)idx]).getX() - 7);
    return getWidth() - 3;
  }
  ToolSet& tools_;
  std::vector<std::unique_ptr<ColumnSplitter>> splitters_;
  int dropX_ = -1;
};

class MeterDock : public juce::Component {
 public:
  MeterDock(AppCtx& ctx, ToolSet& tools) : ctx_(ctx), tools_(tools), suite_(tools) {
    load_.setButtonText("Load Hit Record Meter...");
    load_.onClick = [this] { if (onLoadMeter) onLoadMeter(); };
    toolsBtn_.setButtonText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Tools")));
    toolsBtn_.setTooltip("Choose the reference tools shown here (all tools are in Listen mode)");
    toolsBtn_.onClick = [this] { toolsMenu(); };
    meterSplit_.current = [this] { return meterW_; };
    // The meter may be dragged all the way across; the suite tools fold away as it grows.
    meterSplit_.onDragState = [this](bool on) { setLiveResize(on); };
    meterSplit_.onDrag = [this](int w) { meterW_ = juce::jlimit(260, juce::jmax(260, bodyWidth() - 14), w); ctx_.props.setValue("dock.meterWidth", meterW_); resized(); };
    meterW_ = ctx_.props.getIntValue("dock.meterWidth", 1341);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&load_, &toolsBtn_, &meterSplit_, &suite_, &viewBtn_}) addAndMakeVisible(c);
    addChildComponent(dark_);
    addAndMakeVisible(info_);
    info_.setTooltip("What the Hit Record Meter measures, and how to use it");
    info_.onClick = [this] { showInfo(nullptr); };
    viewBtn_.setButtonText("Dark view");
    viewBtn_.setTooltip("The Hit Record Meter in a dark view: the same measurements, layout and scores, drawn dark with the "
                        "scale in the accent colour (click it to freeze, double-click to start over). Click again for the plugin's own display.");
    viewBtn_.onClick = [this] { setDark(!darkOn_); };
    darkOn_ = ctx_.props.getBoolValue("dock.darkMeter", true);
    setDark(darkOn_);
    // Monitor section (mockup .mon): Mono / Dim / Pol are switches; L / R / Mid / Side choose one, click again for stereo.
    const char* labels[7] = {"Mono", "Dim", "Pol \xe2\x8c\x80", "L", "R", "Mid", "Side"};
    const char* tips[7] = {"Hear the mix in mono (speakers only; recordings and exports are unaffected)", "Turn the speakers down 20 dB",
                           "Flip the right channel's polarity (with Mono: hear phase problems)", "Left channel on both speakers",
                           "Right channel on both speakers", "Mid (L+R) on both speakers", "Side (L-R) on both speakers"};
    for (int i = 0; i < 7; ++i) {
      auto& b = mon_[(size_t)i];
      b.setButtonText(juce::String(juce::CharPointer_UTF8(labels[i]))); b.setTooltip(tips[i]);
      b.getProperties().set("noUpper", i >= 3 && i <= 4); b.getProperties().set("fontH", 11.0);
      b.onClick = [this, i] {
        auto* e = ctx_.engine(); if (!e) return;
        auto& m = e->monitor();
        if (i == 0) m.mono = !m.mono.load();
        else if (i == 1) m.dim = !m.dim.load();
        else if (i == 2) m.polarity = !m.polarity.load();
        else { const int s = i - 2; m.solo = m.solo.load() == s ? (int)airdaw::Monitor::Stereo : s; }  // 3..6 -> Left..Side
        syncMonitor();
      };
      addAndMakeVisible(b);
      static const char* mids[7] = {"mon.mono", "mon.dim", "mon.pol", "mon.left", "mon.right", "mon.mid", "mon.side"};
      tagMidi(b, mids[i]);
    }
  }
  void syncMonitor() {  // reflect the engine's monitor state (~10x/s)
    auto* e = ctx_.engine(); if (!e) return;
    auto& m = e->monitor();
    const bool on[7] = {m.mono.load(), m.dim.load(), m.polarity.load(), m.solo.load() == airdaw::Monitor::Left, m.solo.load() == airdaw::Monitor::Right,
                        m.solo.load() == airdaw::Monitor::Mid, m.solo.load() == airdaw::Monitor::Side};
    for (int i = 0; i < 7; ++i) mon_[(size_t)i].setToggleState(on[i], juce::dontSendNotification);
  }
  ~MeterDock() override { setPlugin(nullptr); }

  std::function<void()> onLoadMeter;
  // Dark view on / off (remembered). The view takes its measurements from the master analysis (`source`).
  void setDark(bool on) {
    darkOn_ = on; ctx_.props.setValue("dock.darkMeter", on);
    viewBtn_.setToggleState(on, juce::dontSendNotification);
    dark_.setVisible(on); dark_.setActive(on);
    if (editor_) editor_->setVisible(!on);
    resized(); repaint();
  }
  void setMeterSource(std::function<HitMeterAnalyzer*()> s) { dark_.source = std::move(s); }
  HitMeterView& darkMeter() { return dark_; }  // (test hook)
  // The (i) panel. `parent` null: its own window on the desktop; a component: inside it (test hook).
  void showInfo(juce::Component* parent) {
    const auto at = parent ? parent->getLocalArea(this, info_.getBounds()) : info_.getScreenBounds();
    juce::CallOutBox::launchAsynchronously(std::make_unique<HitMeterInfo>(), at, parent);
  }
  void rebuildSuite() { suite_.rebuild(); }
  void setMeterWidth(int w) { meterW_ = juce::jmax(260, w); resized(); }  // (test hook; users drag the divider)

  // Shows this plugin's editor (or the "load" prompt when null). Keeps the plugin alive while its editor exists.
  // A divider is being dragged (true) or was released (false): see resized().
  void setLiveResize(bool on) { live_ = on; if (!on) { lastEditorResize_ = 0; resized(); } }
  void setPlugin(std::shared_ptr<juce::AudioPluginInstance> p) {
    if (p == plugin_) return;
    editor_.reset();      // the editor must go before the plugin reference is released
    plugin_ = std::move(p);
    if (plugin_) {
      if (auto* ed = plugin_->createEditorIfNeeded()) { editor_.reset(ed); addChildComponent(*editor_); editor_->setVisible(!darkOn_); }
    }
    load_.setButtonText(plugin_ ? "Change meter..." : "Load Hit Record Meter...");
    resized(); repaint();
  }
  bool hasPlugin() const { return plugin_ != nullptr; }

  void paint(juce::Graphics& g) override {
    auto& t = T();
    auto r = getLocalBounds().toFloat();
    g.setColour(T().bg()); g.fillRoundedRectangle(r, T().rad(6.f));
    auto head = r.removeFromTop(kHead);
    t.drawBrush(g, head);
    g.setColour(juce::Colours::black); g.fillRect(head.removeFromBottom(1.f));
    // title: AIR + WINDOWS (accent) + HIT RECORD METER + reference suite
    auto tr = head.reduced(12.f, 0.f);
    g.setFont(Theme::spaced(Theme::uiFont(14.f, true), 0.28f));
    auto draw = [&](const juce::String& s, juce::Colour c, bool glow) {
      if (glow) t.drawGlowText(g, s, tr, juce::Justification::centredLeft, c, 1.f);
      else { g.setColour(c); g.drawText(s, tr, juce::Justification::centredLeft, false); }
      tr.removeFromLeft(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), s) + 4.f);
    };
    draw("AIR", Theme::cream(), false); draw("WINDOWS", t.am(), true); tr.removeFromLeft(8.f); draw("HIT RECORD METER", Theme::cream(), false);
    tr.removeFromLeft((float)kInfoW);  // the (i)
    g.setFont(Theme::spaced(Theme::uiFont(10.f), 0.1f)); tr.removeFromLeft(4.f);
    draw("+ REFERENCE SUITE", Theme::dim(), false);
    if (!plugin_ && !darkOn_) {
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(13.f));
      g.drawFittedText("The Airwindows Hit Record Meter is a free VST3 (github.com/airwindows/Meter/releases). "
                       "Load it once; it sits on the last master insert and its display shows here.",
                       meterArea().reduced(16).withTrimmedBottom(50), juce::Justification::centred, 4);
    }
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), T().rad(6.f), 1.f);
  }

  void resized() override {
    auto r = getLocalBounds();
    auto head = r.removeFromTop(kHead);
    {  // the (i) right after the title (same fonts as paint)
      const auto f = Theme::spaced(Theme::uiFont(14.f, true), 0.28f);
      float x = 12.f;
      for (auto* s : {"AIR", "WINDOWS", "HIT RECORD METER"}) x += juce::GlyphArrangement::getStringWidth(f, s) + 4.f;
      x += 8.f;
      info_.setBounds(juce::Rectangle<int>((int)x, head.getY(), kInfoW, kHead).withSizeKeepingCentre(kInfoW - 2, 22));
    }
    toolsBtn_.setBounds(head.removeFromRight(96).reduced(4, 5));
    viewBtn_.setBounds(head.removeFromRight(110).reduced(4, 5));
    if (plugin_ || darkOn_) load_.setBounds(head.removeFromRight(plugin_ ? 140 : 200).reduced(4, 5));
    else load_.setBounds(meterArea().withSizeKeepingCentre(220, 32).translated(0, 40));
    head.removeFromRight(10);
    for (int i = 6; i >= 0; --i) {  // right to left: Side Mid R L | Pol Dim Mono
      const int w = i >= 3 && i <= 4 ? 30 : i == 2 ? 58 : 48;
      mon_[(size_t)i].setBounds(head.removeFromRight(w).reduced(2, 5));
      if (i == 3) head.removeFromRight(8);
    }
    auto body = r.reduced(10);
    auto m = body.removeFromLeft(juce::jmin(meterW_, body.getWidth() - 14));
    meterSplit_.setBounds(body.removeFromLeft(14));
    suite_.setBounds(body);
    dark_.setBounds(m);
    // While a divider is dragged, the plugin's own window (slow to resize) follows only every ~120 ms; the final size is
    // applied when the drag ends (setLiveResize(false)).
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool skipEditor = live_ && now - lastEditorResize_ < 120.0;
    if (editor_ && !skipEditor) {
      lastEditorResize_ = now;
      if (editor_->isResizable()) editor_->setBounds(m);
      else editor_->setTopLeftPosition(m.getX() + juce::jmax(0, (m.getWidth() - editor_->getWidth()) / 2), m.getY());
    }
  }
  static constexpr int kHead = 36, kInfoW = 26;

 private:
  juce::Rectangle<int> meterArea() const { auto r = getLocalBounds().withTrimmedTop(kHead).reduced(10); return r.removeFromLeft(juce::jmin(meterW_, r.getWidth() - 14)); }
  int bodyWidth() const { return getWidth() - 20; }
  void toolsMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Reference suite");
    int i = 1;
    for (auto& t : ToolSet::registry()) m.addItem(i++, juce::String(juce::CharPointer_UTF8(t.title)), true, tools_.inSuite(t.id));
    m.addSeparator();
    m.addItem(1000, "Arrange in Listen mode (drag tools between the suite and the Listen row)", false, false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&toolsBtn_), [this](int r) {
      if (r <= 0 || r > (int)ToolSet::registry().size()) return;
      const juce::String id = ToolSet::registry()[(size_t)r - 1].id;
      if (tools_.inSuite(id)) tools_.removeFromSuite(id); else tools_.addToSuite(id);
    });
  }
  AppCtx& ctx_;
  ToolSet& tools_;
  std::shared_ptr<juce::AudioPluginInstance> plugin_;
  std::unique_ptr<juce::AudioProcessorEditor> editor_;
  bool live_ = false;
  double lastEditorResize_ = 0;
  juce::TextButton load_, toolsBtn_, viewBtn_;
  HitMeterView dark_;
  InfoDot info_;
  bool darkOn_ = false;
  std::array<juce::TextButton, 7> mon_;
  ColumnSplitter meterSplit_;
  SuiteArea suite_;
  int meterW_ = 1341;
};

}  // namespace ui
