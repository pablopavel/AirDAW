#include "InsertPanel.h"
#include "../midi/MidiControl.h"
#include "PluginsPanel.h"
#include "airdaw/fx_host.h"

namespace ui {

// ---------------- PresetStore ----------------
juce::var PresetStore::load(const juce::String& plugin) const {
  auto f = file(plugin);
  if (!f.existsAsFile()) return {};
  return juce::JSON::parse(f);
}
void PresetStore::store(const juce::String& plugin, const juce::var& v) {
  dir_.createDirectory();
  file(plugin).replaceWithText(juce::JSON::toString(v));
}
juce::StringArray PresetStore::names(const juce::String& plugin) const {
  juce::StringArray out;
  auto v = load(plugin);
  if (auto* a = v["presets"].getArray()) for (auto& p : *a) out.add(p["name"].toString());
  return out;
}
std::vector<float> PresetStore::values(const juce::String& plugin, const juce::String& name) const {
  auto v = load(plugin);
  if (auto* a = v["presets"].getArray())
    for (auto& p : *a)
      if (p["name"].toString() == name)
        if (auto* vals = p["values"].getArray()) { std::vector<float> r; for (auto& x : *vals) r.push_back((float)(double)x); return r; }
  return {};
}
void PresetStore::save(const juce::String& plugin, const juce::String& name, const std::vector<float>& values) {
  auto v = load(plugin);
  juce::Array<juce::var> list;
  if (auto* a = v["presets"].getArray()) for (auto& p : *a) if (p["name"].toString() != name) list.add(p);
  auto* o = new juce::DynamicObject(); o->setProperty("name", name);
  juce::Array<juce::var> vals; for (float x : values) vals.add(x);
  o->setProperty("values", vals); list.add(juce::var(o));
  auto* root = new juce::DynamicObject(); root->setProperty("plugin", plugin); root->setProperty("presets", list);
  store(plugin, juce::var(root));
}
void PresetStore::remove(const juce::String& plugin, const juce::String& name) {
  auto v = load(plugin);
  juce::Array<juce::var> list;
  if (auto* a = v["presets"].getArray()) for (auto& p : *a) if (p["name"].toString() != name) list.add(p);
  auto* root = new juce::DynamicObject(); root->setProperty("plugin", plugin); root->setProperty("presets", list);
  store(plugin, juce::var(root));
}

// ---------------- helpers ----------------
static constexpr int kHeaderH = 34, kKnobW = 70, kKnobH = 92, kKnobGapX = 14, kKnobGapY = 12, kMaxCols = 8, kPad = 12;

static void cardFrame(juce::Graphics& g, juce::Rectangle<float> r, bool active) {
  auto& t = T();
  if (active) t.drawGlow(g, r, t.am(), 26.f, 0.2f);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b1a19), 0, r.getY(), juce::Colour(0xff0f0e0d), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, T().rad(6.f));
  if (active) { g.setColour(t.am().withAlpha(t.glowAlpha(0.06f))); g.fillRoundedRectangle(r.reduced(1.f), T().rad(6.f)); }
  g.setColour(active ? t.c(0xff3d2c12) : juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), T().rad(6.f), 1.f);
}

// Favourite plugins: a ';'-separated list in the app settings, shown at the top of the picker.
static juce::StringArray favourites(AppCtx& ctx) {
  auto a = juce::StringArray::fromTokens(ctx.props.getValue("favoritePlugins"), ";", ""); a.removeEmptyStrings(); return a;
}
static bool isFavourite(AppCtx& ctx, const juce::String& n) { return favourites(ctx).contains(n); }
static void setFavourite(AppCtx& ctx, const juce::String& n, bool on) {
  auto a = favourites(ctx);
  if (on) a.addIfNotAlreadyThere(n); else a.removeString(n);
  a.sort(true);
  ctx.props.setValue("favoritePlugins", a.joinIntoString(";")); ctx.props.saveIfNeeded();
}

// Newest version of each Airwindows family only ("ToTape8" hides "ToTape7"), unless showOld.
static std::vector<airdaw::FxInfo> pickerList(bool showOld) {
  auto all = airdaw::listEffects();
  auto split = [](const std::string& n, std::string& base) {
    size_t i = n.size(); while (i > 0 && std::isdigit((unsigned char)n[i - 1])) --i;
    base = n.substr(0, i); return i < n.size() ? std::stoi(n.substr(i)) : -1;
  };
  std::map<std::string, int> newest;
  for (auto& f : all) { std::string b; int v = split(f.name, b); if (v >= 0) newest[b] = std::max(newest[b], v); }
  std::vector<airdaw::FxInfo> out;
  for (auto& f : all) {
    std::string b; int v = split(f.name, b);
    if (showOld || v < 0 || newest[b] == v) out.push_back(f);
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.category != b.category ? a.category < b.category : a.name < b.name; });
  return out;
}

// ---------------- InsertCard ----------------
class InsertCard : public juce::Component, public juce::DragAndDropTarget {
 public:
  InsertCard(InsertPanel& p, int slot) : p_(p), slot_(slot) {
    auto* e = p_.ctx_.engine(); auto* fx = e ? e->insertAt(p_.ch_, slot) : nullptr;
    if (!fx) return;
    name_ = fx->name();
    hosted_ = dynamic_cast<airdaw::FxInstance*>(fx) == nullptr;
    open_ = p_.isOpen(p_.ch_, slot);
    for (auto* b : {&byp_, &tog_, &rm_}) { addAndMakeVisible(*b); b->getProperties().set("noUpper", true); b->getProperties().set("fontH", 12.0); }
    if (!hosted_) {
      for (auto* b : {&fav_, &info_}) { addAndMakeVisible(*b); b->getProperties().set("noUpper", true); b->getProperties().set("fontH", 13.0); }
      fav_.setTooltip("Favourite: show this plugin at the top of the plugin list");
      fav_.onClick = [this] { const bool on = !isFavourite(p_.ctx_, name_); setFavourite(p_.ctx_, name_, on);
                              p_.ctx_.note(name_ + (on ? " added to favourites" : " removed from favourites")); p_.key_.clear(); };
      info_.setTooltip("About this plugin (Chris's description from Airwindows)");
      info_.onClick = [this] { if (p_.ctx_.showPluginInfo) p_.ctx_.showPluginInfo(name_); };
    }
    byp_.setTooltip("Bypass on/off"); tog_.setTooltip("Show / hide the controls"); rm_.setTooltip("Remove from the chain");
    tagMidi(byp_, "fx." + juce::String(p_.ch_) + "." + juce::String(slot_) + ".bypass");
    byp_.onClick = [this] { if (auto* f = fxNow()) f->setBypass(!f->bypassed()); sync(); };
    tog_.onClick = [this] { p_.setOpen(slot_, !open_); };
    rm_.onClick = [this] {
      const int ch = p_.ch_, s = slot_;
      // Deferred: removing rebuilds the panel, which deletes this card.
      juce::MessageManager::callAsync([ch, s, sp = juce::Component::SafePointer<InsertPanel>(&p_)] {
        if (!sp) return;
        if (auto* en = sp->ctx_.engine()) en->setInsert(ch, s, "");
        sp->setOpen(s, false);
      });
    };
    if (!open_) return;
    if (hosted_) {
      window_.setButtonText("Open plugin window"); addAndMakeVisible(window_);
      window_.onClick = [this] { if (p_.ctx_.openPluginWindow) p_.ctx_.openPluginWindow(p_.ch_, slot_); };
      return;
    }
    for (int i = 0; i < fx->numParams(); ++i) {
      auto k = std::make_unique<PluginKnob>();
      k->label = fx->params()[(size_t)i].name;
      k->setRange(0.f, 1.f, fx->params()[(size_t)i].defaultValue);
      k->onChange = [this, i](float v) { if (auto* f = fxNow()) f->setParam(i, v); };
      tagMidi(*k, "fx." + juce::String(p_.ch_) + "." + juce::String(slot_) + "." + juce::String(i));
      addAndMakeVisible(*k); knobs_.push_back(std::move(k));
    }
    def_.setTooltip("Reset every knob to the plugin's default");
    def_.onClick = [this] { if (auto* f = fxNow()) for (int i = 0; i < f->numParams(); ++i) f->setParam(i, f->params()[(size_t)i].defaultValue); };
    save_.setTooltip("Save the current knob settings as a preset");
    save_.onClick = [this] { savePreset(); };
    addAndMakeVisible(def_); addAndMakeVisible(save_);
    for (auto& n : p_.presets_.names(name_)) {
      auto b = std::make_unique<juce::TextButton>(n);
      b->setTooltip("Apply preset \"" + n + "\" (right-click or Ctrl+click to delete)");
      b->getProperties().set("noUpper", true);
      b->onClick = [this, n] {
        const auto mods = juce::ModifierKeys::currentModifiers;
        if (mods.isPopupMenu() || mods.isCommandDown()) { deletePreset(n); return; }
        auto v = p_.presets_.values(name_, n);
        if (auto* f = fxNow()) for (int i = 0; i < (int)v.size() && i < f->numParams(); ++i) f->setParam(i, v[(size_t)i]);
      };
      addAndMakeVisible(*b); presetBtns_.push_back(std::move(b));
    }
    for (auto* b : std::initializer_list<juce::TextButton*>{&def_, &save_}) b->getProperties().set("fontH", 10.0);
    for (auto& b : presetBtns_) b->getProperties().set("fontH", 10.0);
    sync();
  }

  airdaw::InsertProcessor* fxNow() const { auto* e = p_.ctx_.engine(); return e ? e->insertAt(p_.ch_, slot_) : nullptr; }

  void sync() {
    auto* f = fxNow(); if (!f) return;
    byp_.setToggleState(!f->bypassed(), juce::dontSendNotification);
    active_ = !f->bypassed();
    for (int i = 0; i < (int)knobs_.size() && i < f->numParams(); ++i) {
      auto& k = *knobs_[(size_t)i];
      if (!k.isMouseButtonDown()) k.setValue(f->getParam(i));
      auto txt = juce::String(f->paramDisplay(i)).trim();
      const auto lab = juce::String(f->params()[(size_t)i].label).trim();
      // Many Airwindows parameters only report their raw 0..1 value: show it as 0-100 like the mockup.
      if (lab.isEmpty() && txt.containsOnly("0123456789.-") && std::abs(txt.getFloatValue() - f->getParam(i)) < 0.0006f)
        txt = juce::String(juce::roundToInt(f->getParam(i) * 100.f));
      else if (lab.isNotEmpty()) txt << " " << lab;
      if (txt != k.valueText) { k.valueText = txt; k.repaint(); }
    }
    repaint();
  }

  int cols() const { return juce::jlimit(1, kMaxCols, (int)knobs_.size()); }
  int prefWidth() const {
    const int head = (hosted_ ? 172 : 240) + juce::roundToInt(juce::GlyphArrangement::getStringWidth(Theme::spaced(Theme::uiFont(14.f, true), 0.05f), name_));
    if (!open_) return juce::jmax(170, head);
    if (hosted_) return juce::jmax(head, 260);
    return juce::jmax(head, cols() * kKnobW + (cols() - 1) * kKnobGapX + 2 * kPad);
  }
  int prefHeight() const {
    if (!open_) return kHeaderH;
    if (hosted_) return kHeaderH + 70;
    const int rows = ((int)knobs_.size() + kMaxCols - 1) / kMaxCols;
    return kHeaderH + kPad + 24 + 10 + rows * kKnobH + juce::jmax(0, rows - 1) * kKnobGapY + kPad;
  }

  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat();
    cardFrame(g, r, active_);
    auto head = r.removeFromTop((float)kHeaderH);
    g.saveState(); g.reduceClipRegion(head.toNearestInt());
    juce::Path clip; clip.addRoundedRectangle(head.getX(), head.getY(), head.getWidth(), head.getHeight(), T().rad(6.f), T().rad(6.f), true, true, false, false);
    g.reduceClipRegion(clip);
    T().drawBrush(g, head); g.restoreState();
    g.setColour(juce::Colours::black); g.fillRect(head.getX(), head.getBottom() - 1.f, head.getWidth(), 1.f);
    auto hr = head.reduced(8.f, 0.f);
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(16.f));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\xa0\xbf")), hr.removeFromLeft(14.f), juce::Justification::centredLeft);  // drag grip
    g.setColour(T().amd()); g.setFont(Theme::monoFont(12.f, false));
    g.drawText(juce::String(slot_ + 1), hr.removeFromLeft(16.f), juce::Justification::centredLeft);
    g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(14.f, true), 0.05f));
    g.drawText(name_, hr, juce::Justification::centredLeft, true);
    if (open_ && hosted_) {
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(12.f));
      g.drawFittedText(name_.containsIgnoreCase("meter") ? "Hosted VST3. Its display is in the meter dock below." : "Hosted VST3 plugin.",
                       getLocalBounds().withTrimmedTop(kHeaderH + 44).reduced(12, 0), juce::Justification::topLeft, 2);
    }
  }

  void resized() override {
    auto r = getLocalBounds();
    auto head = r.removeFromTop(kHeaderH).reduced(6, 5);
    rm_.setBounds(head.removeFromRight(30)); head.removeFromRight(4);
    tog_.setBounds(head.removeFromRight(30)); head.removeFromRight(4);
    byp_.setBounds(head.removeFromRight(30));
    tog_.setButtonText(juce::String(juce::CharPointer_UTF8(open_ ? "\xe2\x96\xbe" : "\xe2\x96\xb8")));
    byp_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x8f\xbb")));
    if (!hosted_) {
      info_.setBounds(head.removeFromRight(30)); head.removeFromRight(4);
      fav_.setBounds(head.removeFromRight(30)); head.removeFromRight(4);
      const bool fav = isFavourite(p_.ctx_, name_);
      fav_.setButtonText(juce::String(juce::CharPointer_UTF8(fav ? "\xe2\x98\x85" : "\xe2\x98\x86")));
      fav_.setToggleState(fav, juce::dontSendNotification);
      info_.setButtonText("i");
    }
    rm_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95")));
    if (!open_) return;
    auto body = r.reduced(kPad);
    if (hosted_) { window_.setBounds(body.removeFromTop(30).removeFromLeft(180)); return; }
    auto pre = body.removeFromTop(24); body.removeFromTop(10);
    auto place = [&](juce::TextButton& b) { const int w = b.getBestWidthForHeight(24) + 14; b.setBounds(pre.removeFromLeft(w)); pre.removeFromLeft(4); };
    place(def_); for (auto& b : presetBtns_) place(*b); place(save_);
    for (int i = 0; i < (int)knobs_.size(); ++i) {
      const int c = i % kMaxCols, row = i / kMaxCols;
      knobs_[(size_t)i]->setBounds(body.getX() + c * (kKnobW + kKnobGapX), body.getY() + row * (kKnobH + kKnobGapY), kKnobW, kKnobH);
    }
  }

  void mouseUp(const juce::MouseEvent& e) override {  // clicking the header toggles the card
    if (e.mouseWasClicked() && e.y < kHeaderH) p_.setOpen(slot_, !open_);
  }
  void mouseDrag(const juce::MouseEvent& e) override {  // dragging the header moves the plugin in the chain
    if (e.mouseDownPosition.y >= kHeaderH || e.getDistanceFromDragStart() < 6) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
      if (!dc->isDragAndDropActive()) dc->startDragging(insertDragId(p_.ch_, slot_), this);
  }
  void paintOverChildren(juce::Graphics& g) override { if (dropHover_) drawDropOutline(g, getLocalBounds().toFloat(), 6.f); }
  bool isInterestedInDragSource(const SourceDetails& d) override {
    int ch, s; return parseInsertDrag(d.description, ch, s) && ch == p_.ch_ && s != slot_;
  }
  void itemDragEnter(const SourceDetails&) override { dropHover_ = true; repaint(); }
  void itemDragExit(const SourceDetails&) override { dropHover_ = false; repaint(); }
  void itemDropped(const SourceDetails& d) override {
    dropHover_ = false; repaint();
    int ch, s; if (parseInsertDrag(d.description, ch, s) && p_.ctx_.moveInsert) p_.ctx_.moveInsert(ch, s, slot_);
  }

 private:
  void savePreset() {
    auto* w = new juce::AlertWindow("Save preset", "Name for this " + name_ + " preset:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "My " + name_, {});
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, sp = juce::Component::SafePointer<InsertPanel>(&p_)](int r) {
      const auto n = w->getTextEditorContents("name").trim();
      if (!sp || r != 1 || n.isEmpty()) return;
      auto* f = fxNow(); if (!f) return;
      std::vector<float> v; for (int i = 0; i < f->numParams(); ++i) v.push_back(f->getParam(i));
      sp->presets_.save(name_, n, v);
      sp->key_.clear(); sp->rebuild();
      sp->ctx_.note("Saved preset \"" + n + "\" for " + name_);
    }), true);
  }
  void deletePreset(const juce::String& n) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Delete preset").withMessage("Delete the " + name_ + " preset \"" + n + "\"?")
                                     .withButton("Delete", 1).withButton("Cancel", 0),
                                 [this, n, sp = juce::Component::SafePointer<InsertPanel>(&p_)](int r) {
                                   if (!sp || r != 1) return;
                                   sp->presets_.remove(name_, n); sp->key_.clear(); sp->rebuild();
                                 });
  }

  InsertPanel& p_;
  const int slot_;
  juce::String name_;
  bool hosted_ = false, open_ = false, active_ = true, dropHover_ = false;
  juce::TextButton byp_, tog_, rm_, fav_, info_, window_, def_{"Default"}, save_{juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Save"))};
  std::vector<std::unique_ptr<PluginKnob>> knobs_;
  std::vector<std::unique_ptr<juce::TextButton>> presetBtns_;
};

// "+ Load into slot N" card with the plugin picker.
class AddCard : public juce::Component {
 public:
  AddCard(InsertPanel& p, AppCtx& ctx, int ch, int slot, bool& showOld, std::function<void(int)> refresh)
      : ctx_(ctx), ch_(ch), slot_(slot), showOld_(showOld), refresh_(std::move(refresh)) {
    juce::ignoreUnused(p);
    showOld = ctx.props.getBoolValue("plugins.showOld", showOld);  // shared with the Plugins panel
    auto list = pickerList(showOld);
    box_.setTextWhenNothingSelected(juce::String(juce::CharPointer_UTF8("Choose Airwindows plugin\xe2\x80\xa6")));
    const auto favs = favourites(ctx);
    if (!favs.isEmpty()) {  // favourites first (ids offset so they never clash with the main list)
      box_.addSectionHeading(juce::String(juce::CharPointer_UTF8("\xe2\x98\x85 Favourites")));
      for (auto& f : favs) if (airdaw::effectExists(f.toStdString())) { box_.addItem(f, 10000 + names_.size()); names_.add(f); }
    }
    const int favCount = names_.size();
    std::string cat;
    for (int i = 0; i < (int)list.size(); ++i) {
      if (list[(size_t)i].category != cat) { cat = list[(size_t)i].category; box_.addSectionHeading(cat); }
      const juce::String nm(list[(size_t)i].name);
      box_.addItem(pluginlib::newEffects().contains(nm) ? nm + juce::String(juce::CharPointer_UTF8("  \xe2\x80\x94 new")) : nm, favCount + i + 1);
      names_.add(list[(size_t)i].name);
    }
    box_.onChange = [this] {
      const int id = box_.getSelectedId(); if (id <= 0) return;
      const int i = id >= 10000 ? id - 10000 : id - 1;  // index into names_
      const auto n = names_[i];
      juce::Component::SafePointer<AddCard> sp(this);
      juce::MessageManager::callAsync([this, n, sp] {
        if (!sp) return;
        auto* e = ctx_.engine(); if (!e) return;
        if (!e->setInsert(ch_, slot_, n.toStdString())) { ctx_.note("Could not load " + n); return; }
        ctx_.note("Loaded " + n + " into slot " + juce::String(slot_ + 1));
        auto refresh = refresh_;  // refreshing deletes this card, including refresh_
        refresh(slot_);
      });
    };
    old_.setButtonText(showOld ? "Hide older versions" : "Show older versions");
    old_.getProperties().set("fontH", 10.0);
    old_.onClick = [this] { showOld_ = !showOld_; ctx_.props.setValue("plugins.showOld", showOld_);
                            juce::MessageManager::callAsync([r = refresh_] { r(-1); }); };
    count_ = (int)list.size();
    addAndMakeVisible(box_); addAndMakeVisible(old_);
  }
  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat();
    cardFrame(g, r, false);
    T().drawBrush(g, r.reduced(1.f));
    g.setColour(Theme::cream()); g.setFont(Theme::uiFont(14.f, true));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Load into slot ")) + juce::String(slot_ + 1), getLocalBounds().reduced(12, 8).removeFromTop(22),
               juce::Justification::centredLeft);
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(12.f));
    g.drawText(juce::String(count_) + " plugins" + (showOld_ ? "" : juce::String(juce::CharPointer_UTF8(" \xc2\xb7 latest versions"))),
               getLocalBounds().reduced(12, 8).withTrimmedTop(64).removeFromTop(18), juce::Justification::centredLeft);
  }
  void resized() override {
    auto r = getLocalBounds().reduced(12, 8);
    r.removeFromTop(26); box_.setBounds(r.removeFromTop(30)); r.removeFromTop(28);
    old_.setBounds(r.removeFromTop(24).removeFromLeft(170));
  }
 private:
  AppCtx& ctx_; int ch_, slot_; bool& showOld_; std::function<void(int)> refresh_;
  juce::ComboBox box_; juce::TextButton old_; juce::StringArray names_; int count_ = 0;
};

// ---------------- InsertPanel ----------------
InsertPanel::InsertPanel(AppCtx& ctx, PresetStore& presets) : ctx_(ctx), presets_(presets) {
  view_.setViewedComponent(&row_, false);
  view_.setScrollBarsShown(true, true);  // short windows: the cards scroll instead of being cut off
  addAndMakeVisible(view_);
  for (auto* b : {&close1_, &close2_}) { addAndMakeVisible(*b); b->onClick = [this] { close(); }; }
}
InsertPanel::~InsertPanel() = default;

bool InsertPanel::isOpen(int channelId, int slot) const {
  if (!isVisible() || channelId != ch_) return false;
  auto it = open_.find(channelId); if (it == open_.end()) return false;
  auto s = it->second.find(slot); return s != it->second.end() && s->second;
}

void InsertPanel::show(int channelId, int slot) {
  auto* e = ctx_.engine(); if (!e) return;
  ch_ = channelId; target_ = -1;
  if (slot >= 0) {
    if (e->insertAt(channelId, slot)) open_[channelId][slot] = !open_[channelId][slot];
    else target_ = slot;
  }
  key_.clear();
  setVisible(true);
  rebuild();
  if (ctx_.layoutChanged) ctx_.layoutChanged();
}

void InsertPanel::close() {
  setVisible(false); ch_ = -1;
  if (onClosed) onClosed();
  if (ctx_.layoutChanged) ctx_.layoutChanged();
}

void InsertPanel::moveInsert(int channelId, int from, int to) {
  auto* e = ctx_.engine(); if (!e) return;
  if (!e->moveInsert(channelId, from, to)) { ctx_.note("Could not move the insert"); return; }
  // Same reorder for the open/closed card state, so each card stays as it was.
  auto& m = open_[channelId];
  std::vector<int> order; for (int s = 0; s < airdaw::kMaxInserts; ++s) if (s != from) order.push_back(s);
  order.insert(order.begin() + to, from);
  std::map<int, bool> next; for (int s = 0; s < airdaw::kMaxInserts; ++s) next[s] = m[order[(size_t)s]];
  m = next;
  if (auto* c = channelOf(*e, channelId)) ctx_.note("Moved " + juce::String(e->insertAt(channelId, to)->name()) + " to slot " + juce::String(to + 1) +
                                                    " on " + juce::String(c->name));
  key_.clear(); rebuild();
  if (ctx_.layoutChanged) ctx_.layoutChanged();
}

void InsertPanel::setOpen(int slot, bool open) {
  open_[ch_][slot] = open; key_.clear(); rebuild();
  if (ctx_.layoutChanged) ctx_.layoutChanged();
}

juce::String InsertPanel::chainKey() const {
  auto* e = ctx_.engine(); if (!e || ch_ < 0) return {};
  juce::String k; k << ch_ << "|" << target_ << "|" << (int)showOld_ << "|" << ctx_.props.getValue("favoritePlugins") << "|";
  for (int s = 0; s < airdaw::kMaxInserts; ++s) {
    auto* fx = e->insertAt(ch_, s);
    k << (fx ? juce::String(fx->name()) : juce::String("-")) << (isOpen(ch_, s) ? "+" : "") << ";";
  }
  return k;
}

void InsertPanel::rebuild() {
  auto* e = ctx_.engine(); if (!e || ch_ < 0) return;
  const auto k = chainKey(); if (k == key_) return;
  key_ = k;
  cards_.clear(); addCard_.reset();
  for (int s = 0; s < airdaw::kMaxInserts; ++s)
    if (e->insertAt(ch_, s)) { cards_.push_back(std::make_unique<InsertCard>(*this, s)); row_.addAndMakeVisible(*cards_.back()); }
  int free = target_;
  if (free < 0 || e->insertAt(ch_, free)) { free = -1; for (int s = 0; s < airdaw::kMaxInserts; ++s) if (!e->insertAt(ch_, s)) { free = s; break; } }
  if (free >= 0) {
    addCard_ = std::make_unique<AddCard>(*this, ctx_, ch_, free, showOld_, [this](int loaded) {
      target_ = -1; if (loaded >= 0) open_[ch_][loaded] = true;
      key_.clear(); rebuild(); if (ctx_.layoutChanged) ctx_.layoutChanged(); });
    row_.addAndMakeVisible(*addCard_);
  }
  resized(); repaint();
}

void InsertPanel::sync() {
  if (!isVisible()) return;
  auto* e = ctx_.engine();
  if (!e) return;
  if (chainKey() != key_) { rebuild(); if (ctx_.layoutChanged) ctx_.layoutChanged(); }
  for (auto& c : cards_) c->sync();
}

int InsertPanel::preferredHeight() const {
  int h = 96;
  for (auto& c : cards_) h = juce::jmax(h, c->prefHeight());
  return h + 20 + (view_.isHorizontalScrollBarShown() ? 10 : 0);
}

void InsertPanel::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  T().drawBrush(g, r);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), T().rad(6.f), 1.f);
  auto* e = ctx_.engine();
  auto hd = getLocalBounds().reduced(14, 10).removeFromLeft(118);
  if (e && ch_ >= 0) {
    auto* c = channelOf(*e, ch_);
    g.setFont(Theme::spaced(Theme::uiFont(15.f, true), 0.16f));
    T().drawGlowText(g, juce::String(c ? c->name : "").toUpperCase(), hd.removeFromTop(22).toFloat(), juce::Justification::centredLeft, Theme::cream(), 0.5f);
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(11.5f));
    g.drawFittedText(juce::String(juce::CharPointer_UTF8("Insert chain\ndrag \xe2\xa0\xbf to reorder")), hd.removeFromTop(34), juce::Justification::topLeft, 2);
  }
}

void InsertPanel::resized() {
  auto r = getLocalBounds().reduced(10);
  auto hd = r.removeFromLeft(122);
  close1_.setBounds(hd.withTrimmedTop(62).removeFromTop(30).removeFromLeft(100));
  close2_.setBounds(r.removeFromRight(96).removeFromTop(30));
  r.removeFromRight(8);
  view_.setBounds(r);
  int x = 0, h = 120;
  for (auto& c : cards_) { const int w = c->prefWidth(); c->setBounds(x, 0, w, c->prefHeight()); x += w + 10; h = juce::jmax(h, c->prefHeight()); }
  if (addCard_) { addCard_->setBounds(x, 0, 230, 120); x += 240; }
  row_.setSize(juce::jmax(x, 10), h);
}

}  // namespace ui
