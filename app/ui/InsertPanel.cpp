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


// ---------------- rack look ----------------
static constexpr int kTop = 44, kFoot = 32, kKnobW = 70, kKnobH = 92, kKnobGapX = 6, kKnobGapY = 8, kMaxCols = 6, kSide = 14,
                     kClosedW = 58, kMinRowH = 190, kBlankW = 58, kRail = 18, kCheek = 18, kGap = 4;

// Faceplate finish by plugin category, like real gear: black + gold, brushed aluminium, hammertone green,
// blue enamel; graphite for hosted VST3s and anything uncategorised. cap: knob cap (0 black, 1 cream, 2 gold).
struct Finish { juce::Colour top, bot, ink, dim; int cap = 0; int tex = 0; };  // tex: 0 smooth, 1 brushed, 2 hammertone
static Finish finishFor(const juce::String& cat, bool hosted) {
  auto is = [&](std::initializer_list<const char*> l) { for (auto* s : l) if (cat == s) return true; return false; };
  using C = juce::Colour;
  if (!hosted && is({"Consoles", "Utility", "Stereo", "Subtlety", "Dithers"})) return {C(0xff202022), C(0xff111113), C(0xffd9b46a), C(0xff8a7550), 2, 1};
  if (!hosted && is({"Dynamics", "Clipping", "Filter", "Biquads", "XYZ Filters", "Brightness"}))
    return {C(0xffc9c9c6), C(0xff8e8e8a), C(0xff161616), C(0xff3c3c3a), 0, 1};
  if (!hosted && is({"Tape", "Saturation", "Tone Color", "Lo-Fi", "Distortion", "Amp Sims", "Bass"}))
    return {C(0xff5f7268), C(0xff3f4f47), C(0xffefe6cc), C(0xffb8c4b4), 1, 2};
  if (!hosted && is({"Reverb", "Ambience", "Effects", "Noise"})) return {C(0xff4a5a70), C(0xff2c3848), C(0xffe9e2cc), C(0xffa8b4c4), 1, 0};
  return {C(0xff2e2e31), C(0xff1b1b1d), Theme::cream(), Theme::dim(), 0, 0};
}
static Finish blankFinish() { return {juce::Colour(0xff1d1d1f), juce::Colour(0xff121214), Theme::cream(), Theme::dim(), 0, 0}; }

// Category of an Airwindows plugin (the library is fixed for a run of the app, so the table is built once).
static juce::String categoryOf(const juce::String& name) {
  static std::map<juce::String, juce::String> table;
  if (table.empty()) for (auto& f : airdaw::listEffects()) table[juce::String(f.name)] = juce::String(f.category);
  auto it = table.find(name);
  return it == table.end() ? juce::String() : it->second;
}

// A module faceplate: shadow, finish gradient, texture, bevel.
static void drawPlate(juce::Graphics& g, juce::Rectangle<float> r, const Finish& f) {
  g.setColour(juce::Colours::black.withAlpha(0.6f)); g.fillRoundedRectangle(r.translated(0.f, 3.f), 3.f);
  g.setGradientFill(juce::ColourGradient(f.top, 0.f, r.getY(), f.bot, 0.f, r.getBottom(), false));
  g.fillRoundedRectangle(r, 3.f);
  {
    juce::Graphics::ScopedSaveState ss(g);
    juce::Path clip; clip.addRoundedRectangle(r, 3.f); g.reduceClipRegion(clip);
    juce::Random rng(f.tex * 7 + 1);
    if (f.tex == 1) {  // brushed: fine vertical grain
      for (float x = r.getX(); x < r.getRight(); x += 2.f) {
        g.setColour(juce::Colours::white.withAlpha(0.012f + 0.03f * rng.nextFloat())); g.fillRect(x, r.getY(), 1.f, r.getHeight());
      }
    } else if (f.tex == 2) {  // hammertone: soft dimples
      const int n = (int)(r.getWidth() * r.getHeight() / 38.f);
      for (int i = 0; i < n; ++i) {
        const float x = r.getX() + rng.nextFloat() * r.getWidth(), y = r.getY() + rng.nextFloat() * r.getHeight(), s = 1.5f + 2.2f * rng.nextFloat();
        g.setColour(juce::Colours::white.withAlpha(0.05f)); g.fillEllipse(x, y, s, s);
        g.setColour(juce::Colours::black.withAlpha(0.07f)); g.fillEllipse(x + 1.f, y + 1.f, s, s);
      }
    }
  }
  g.setColour(juce::Colours::white.withAlpha(0.2f)); g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 2.f, r.getRight() - 2.f);
  g.setColour(juce::Colours::white.withAlpha(0.07f)); g.fillRect(r.getX() + 1.f, r.getY() + 2.f, 1.f, r.getHeight() - 4.f);
  g.setColour(juce::Colours::black.withAlpha(0.75f)); g.drawRoundedRectangle(r, 3.f, 1.f);
}

// Plate rendered once per size into an image (the hammertone texture is too many shapes to draw 15x a second).
static void cachedPlate(juce::Graphics& g, juce::Component& c, juce::Image& img, juce::Point<int>& size, float& scale, const Finish& f) {
  const float s = juce::jmax(1.f, juce::Component::getApproximateScaleFactorForComponent(&c));
  const auto sz = c.getLocalBounds().getBottomRight();
  if (img.isNull() || sz != size || s != scale) {
    size = sz; scale = s;
    img = juce::Image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(sz.x * s)), juce::jmax(1, juce::roundToInt(sz.y * s)), true);
    juce::Graphics ig(img); ig.addTransform(juce::AffineTransform::scale(s));
    drawPlate(ig, c.getLocalBounds().toFloat().withTrimmedBottom(3.f), f);
  }
  g.drawImage(img, c.getLocalBounds().toFloat());
}

static void drawLed(juce::Graphics& g, juce::Point<float> p, bool on) {
  auto r = juce::Rectangle<float>(8.f, 8.f).withCentre(p);
  if (on) { T().drawGlow(g, r, T().am(), 8.f, 0.8f); g.setColour(T().am()); }
  else g.setColour(juce::Colour(0xff3a1c08));
  g.fillEllipse(r);
  g.setColour(juce::Colours::white.withAlpha(on ? 0.6f : 0.12f)); g.fillEllipse(r.reduced(2.5f).translated(-0.5f, -0.5f));
}

static void styleSmall(juce::TextButton& b, double fontH = 12.0) { b.getProperties().set("noUpper", true); b.getProperties().set("fontH", fontH); }

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

// ---------------- the two looks share one card interface ----------------
// Cards (the original view) or Rack (500-series modules), chosen in Display > Hardware > Inserts.
class InsertCard : public juce::Component, public juce::DragAndDropTarget {
 public:
  virtual void sync() = 0;
  virtual int prefWidth() const = 0;
  virtual int prefHeight() const = 0;
};

static constexpr int kCloseCol = 44;  // the right-hand column holding the second X

// ---------------- Cards look (the original insert panel) ----------------
static constexpr int kHeaderH = 34, kcKnobW = 70, kcKnobH = 92, kcKnobGapX = 14, kcKnobGapY = 12, kcMaxCols = 8, kcPad = 12;

static void cardFrame(juce::Graphics& g, juce::Rectangle<float> r, bool active) {
  auto& t = T();
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1b1a19), 0, r.getY(), juce::Colour(0xff0f0e0d), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, T().rad(6.f));
  if (active) { g.setColour(t.am().withAlpha(t.glowAlpha(0.06f))); g.fillRoundedRectangle(r.reduced(1.f), T().rad(6.f)); }
  g.setColour(active ? t.c(0xff3d2c12) : juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), T().rad(6.f), 1.f);
}

class ClassicCard : public InsertCard {
 public:
  ClassicCard(InsertPanel& p, int slot) : p_(p), slot_(slot) {
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

  void sync() override {
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

  int cols() const { return juce::jlimit(1, kcMaxCols, (int)knobs_.size()); }
  int prefWidth() const override {
    const int head = (hosted_ ? 172 : 240) + juce::roundToInt(juce::GlyphArrangement::getStringWidth(Theme::spaced(Theme::uiFont(14.f, true), 0.05f), name_));
    if (!open_) return juce::jmax(170, head);
    if (hosted_) return juce::jmax(head, 260);
    return juce::jmax(head, cols() * kcKnobW + (cols() - 1) * kcKnobGapX + 2 * kcPad);
  }
  int prefHeight() const override {
    if (!open_) return kHeaderH;
    if (hosted_) return kHeaderH + 70;
    const int rows = ((int)knobs_.size() + kcMaxCols - 1) / kcMaxCols;
    return kHeaderH + kcPad + 24 + 10 + rows * kcKnobH + juce::jmax(0, rows - 1) * kcKnobGapY + kcPad;
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
    auto body = r.reduced(kcPad);
    if (hosted_) { window_.setBounds(body.removeFromTop(30).removeFromLeft(180)); return; }
    auto pre = body.removeFromTop(24); body.removeFromTop(10);
    auto place = [&](juce::TextButton& b) { const int w = b.getBestWidthForHeight(24) + 14; b.setBounds(pre.removeFromLeft(w)); pre.removeFromLeft(4); };
    place(def_); for (auto& b : presetBtns_) place(*b); place(save_);
    for (int i = 0; i < (int)knobs_.size(); ++i) {
      const int c = i % kcMaxCols, row = i / kcMaxCols;
      knobs_[(size_t)i]->setBounds(body.getX() + c * (kcKnobW + kcKnobGapX), body.getY() + row * (kcKnobH + kcKnobGapY), kcKnobW, kcKnobH);
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

// ---------------- Rack look: one 500-series module ----------------
class RackCard : public InsertCard {
 public:
  RackCard(InsertPanel& p, int slot) : p_(p), slot_(slot) {
    auto* e = p_.ctx_.engine(); auto* fx = e ? e->insertAt(p_.ch_, slot) : nullptr;
    if (!fx) return;
    name_ = fx->name();
    hosted_ = dynamic_cast<airdaw::FxInstance*>(fx) == nullptr;
    category_ = hosted_ ? juce::String() : categoryOf(name_);
    finish_ = finishFor(category_, hosted_);
    open_ = p_.isOpen(p_.ch_, slot);
    for (auto* b : {&byp_, &tog_, &rm_}) { addAndMakeVisible(*b); styleSmall(*b); }
    byp_.getProperties().set("fontH", 11.0); tog_.getProperties().set("fontH", 15.0); rm_.getProperties().set("fontH", 13.0);
    byp_.setButtonText("IN");
    tog_.setButtonText(juce::String(juce::CharPointer_UTF8(open_ ? "\xe2\x96\xbe" : "\xe2\x96\xb8")));
    rm_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95")));
    byp_.setTooltip("In / bypassed"); tog_.setTooltip(open_ ? "Fold the module" : "Open the module"); rm_.setTooltip("Remove from the chain");
    tagMidi(byp_, "fx." + juce::String(p_.ch_) + "." + juce::String(slot_) + ".bypass");
    byp_.onClick = [this] { if (auto* f = fxNow()) f->setBypass(!f->bypassed()); sync(); };
    tog_.onClick = [this] { p_.setOpen(slot_, !open_); };
    rm_.onClick = [this] {
      const int ch = p_.ch_, s = slot_;
      // Deferred: removing rebuilds the panel, which deletes this card.
      juce::MessageManager::callAsync([ch, s, sp = juce::Component::SafePointer<InsertPanel>(&p_)] {
        if (!sp) return;
        if (auto* en = sp->ctx_.engine()) en->setInsert(ch, s, "");
        sp->preset_[ch].erase(s);
        sp->setOpen(s, false);
      });
    };
    sync();
    if (!open_) return;
    if (hosted_) {
      window_.setButtonText("Open plugin window"); addAndMakeVisible(window_);
      window_.onClick = [this] { if (p_.ctx_.openPluginWindow) p_.ctx_.openPluginWindow(p_.ch_, slot_); };
      return;
    }
    for (auto* b : {&fav_, &info_}) { addAndMakeVisible(*b); styleSmall(*b, 13.0); }
    fav_.setTooltip("Favourite: show this plugin at the top of the plugin list");
    fav_.onClick = [this] { const bool on = !isFavourite(p_.ctx_, name_); setFavourite(p_.ctx_, name_, on);
                            p_.ctx_.note(name_ + (on ? " added to favourites" : " removed from favourites")); p_.key_.clear(); };
    const bool fav = isFavourite(p_.ctx_, name_);
    fav_.setButtonText(juce::String(juce::CharPointer_UTF8(fav ? "\xe2\x98\x85" : "\xe2\x98\x86")));
    fav_.setToggleState(fav, juce::dontSendNotification);
    info_.setButtonText("i");
    info_.setTooltip("About this plugin (Chris's description from Airwindows)");
    info_.onClick = [this] { if (p_.ctx_.showPluginInfo) p_.ctx_.showPluginInfo(name_); };
    preset_.getProperties().set("kind", "lcd"); styleSmall(preset_, 11.0);
    const auto cur = p_.preset_[p_.ch_][slot_];
    preset_.setButtonText(cur.isEmpty() ? "Preset" : cur);
    preset_.setTooltip("Presets: Default, your saved ones, save the current settings");
    preset_.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(preset_);
    for (int i = 0; i < fx->numParams(); ++i) {
      auto k = std::make_unique<PluginKnob>();
      k->label = fx->params()[(size_t)i].name;
      k->rackCap = finish_.cap; k->ink = finish_.ink; k->inkDim = finish_.dim;
      k->setRange(0.f, 1.f, fx->params()[(size_t)i].defaultValue);
      k->onChange = [this, i](float v) { if (auto* f = fxNow()) f->setParam(i, v); };
      tagMidi(*k, "fx." + juce::String(p_.ch_) + "." + juce::String(slot_) + "." + juce::String(i));
      addAndMakeVisible(*k); knobs_.push_back(std::move(k));
    }
    sync();
  }

  airdaw::InsertProcessor* fxNow() const { auto* e = p_.ctx_.engine(); return e ? e->insertAt(p_.ch_, slot_) : nullptr; }

  void sync() override {
    auto* f = fxNow(); if (!f) return;
    byp_.setToggleState(!f->bypassed(), juce::dontSendNotification);
    if (active_ != !f->bypassed()) { active_ = !f->bypassed(); repaint(); }
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
  }

  // Knobs wrap into a second row only past six, so the rack stays about as tall as the old card row.
  int rows() const { const int n = (int)knobs_.size(); return n == 0 ? 0 : (n + kMaxCols - 1) / kMaxCols; }
  int cols() const { const int n = (int)knobs_.size(); return n == 0 ? 0 : (n + rows() - 1) / rows(); }
  int prefWidth() const override {
    if (!open_) return kClosedW;
    if (hosted_) return 250;
    const int c = juce::jmax(1, cols());
    return juce::jmax(268, c * kKnobW + (c - 1) * kKnobGapX + 2 * kSide);
  }
  int prefHeight() const override {
    if (!open_ || hosted_) return kMinRowH;
    const int rr = juce::jmax(1, rows());
    return juce::jmax(kMinRowH, kTop + 6 + rr * kKnobH + (rr - 1) * kKnobGapY + 6 + kFoot + 6);
  }

  void paint(juce::Graphics& g) override {
    cachedPlate(g, *this, plate_, plateSize_, plateScale_, finish_);
    auto b = getLocalBounds().withTrimmedBottom(3).toFloat();
    const auto sub = juce::String(slot_ + 1) + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + (hosted_ ? juce::String("VST3") : category_.isEmpty() ? juce::String("Airwindows") : category_);
    if (!open_) {  // slim plate: name runs up the module
      T().drawScrew(g, {b.getCentreX(), b.getY() + 9.f});
      T().drawScrew(g, {b.getCentreX(), b.getBottom() - 10.f});
      auto band = juce::Rectangle<float>(b.getX(), b.getY() + 76.f, b.getWidth(), b.getHeight() - 76.f - 66.f);
      {
        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, band.getCentreX(), band.getCentreY()));
        auto rot = juce::Rectangle<float>(band.getHeight(), band.getWidth()).withCentre(band.getCentre());
        g.setColour(finish_.ink); g.setFont(Theme::spaced(Theme::uiFont(14.f, true), 0.12f));
        g.drawFittedText(name_.toUpperCase(), rot.removeFromTop(rot.getHeight() * 0.62f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        g.setColour(finish_.dim); g.setFont(Theme::spaced(Theme::uiFont(9.f), 0.12f));
        g.drawFittedText(sub.toUpperCase(), rot.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
      }
      drawLed(g, {b.getCentreX(), (float)byp_.getY() - 9.f}, active_);
      return;
    }
    T().drawScrew(g, {b.getX() + 10.f, b.getY() + 10.f});
    T().drawScrew(g, {b.getRight() - 10.f, b.getY() + 10.f});
    g.setColour(finish_.ink); g.setFont(Theme::spaced(Theme::uiFont(17.f, true), 0.12f));
    g.drawFittedText(name_.toUpperCase(), juce::Rectangle<float>(b.getX() + 22.f, b.getY() + 4.f, b.getWidth() - 44.f, 22.f).toNearestInt(),
                     juce::Justification::centred, 1, 0.6f);
    g.setColour(finish_.dim); g.setFont(Theme::spaced(Theme::uiFont(9.5f), 0.14f));
    g.drawText(sub.toUpperCase(), juce::Rectangle<float>(b.getX() + 22.f, b.getY() + 26.f, b.getWidth() - 44.f, 12.f), juce::Justification::centred, true);
    g.setColour(finish_.dim.withAlpha(0.35f)); g.fillRect(b.getX() + 14.f, b.getY() + (float)kTop - 2.f, b.getWidth() - 28.f, 1.f);
    drawLed(g, {(float)byp_.getX() - 9.f, (float)byp_.getBounds().getCentreY()}, active_);
    if (hosted_) {
      g.setColour(finish_.dim); g.setFont(Theme::uiFont(12.f));
      g.drawFittedText(name_.containsIgnoreCase("meter") ? "Hosted VST3. Its display is in the meter dock below." : "Hosted VST3 plugin.",
                       getLocalBounds().withTrimmedTop(kTop + 50).reduced(kSide, 0).withHeight(40), juce::Justification::topLeft, 2);
    }
  }

  void resized() override {
    auto r = getLocalBounds().withTrimmedBottom(3);
    if (!open_) {
      auto col = r.reduced(7, 0);
      tog_.setBounds(col.withY(20).withHeight(22)); rm_.setBounds(col.withY(46).withHeight(22));
      byp_.setBounds(col.withY(r.getBottom() - 50).withHeight(22));
      return;
    }
    auto foot = r.removeFromBottom(kFoot + 2).reduced(kSide - 4, 4);
    foot.removeFromLeft(14); byp_.setBounds(foot.removeFromLeft(38)); foot.removeFromLeft(6);
    rm_.setBounds(foot.removeFromRight(24)); foot.removeFromRight(3);
    tog_.setBounds(foot.removeFromRight(24)); foot.removeFromRight(3);
    auto body = r.withTrimmedTop(kTop + 6).reduced(kSide, 0);
    if (hosted_) { window_.setBounds(body.removeFromTop(30).withSizeKeepingCentre(juce::jmin(200, body.getWidth()), 30)); return; }
    info_.setBounds(foot.removeFromRight(24)); foot.removeFromRight(3);
    fav_.setBounds(foot.removeFromRight(24)); foot.removeFromRight(6);
    preset_.setBounds(foot.reduced(0, 1));
    const int c = juce::jmax(1, cols());
    const int total = c * kKnobW + (c - 1) * kKnobGapX;
    const int x0 = body.getX() + (body.getWidth() - total) / 2;
    for (int i = 0; i < (int)knobs_.size(); ++i) {
      const int cc = i % c, row = i / c;
      knobs_[(size_t)i]->setBounds(x0 + cc * (kKnobW + kKnobGapX), body.getY() + row * (kKnobH + kKnobGapY), kKnobW, kKnobH);
    }
  }

  void mouseUp(const juce::MouseEvent& e) override {  // clicking the name plate (or a folded module) opens / folds it
    if (e.mouseWasClicked() && (!open_ || e.y < kTop)) p_.setOpen(slot_, !open_);
  }
  void mouseDrag(const juce::MouseEvent& e) override {  // dragging the name plate moves the plugin in the chain
    if ((open_ && e.mouseDownPosition.y >= kTop) || e.getDistanceFromDragStart() < 6) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
      if (!dc->isDragAndDropActive()) dc->startDragging(insertDragId(p_.ch_, slot_), this);
  }
  void paintOverChildren(juce::Graphics& g) override { if (dropHover_) drawDropOutline(g, getLocalBounds().toFloat(), 4.f); }
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
  void setPresetName(const juce::String& n) {
    p_.preset_[p_.ch_][slot_] = n;
    preset_.setButtonText(n.isEmpty() ? "Preset" : n);
  }
  void showPresetMenu() {
    const auto names = p_.presets_.names(name_);
    const auto cur = p_.preset_[p_.ch_][slot_];
    juce::PopupMenu m;
    m.addItem(1, "Default", true, cur == "Default");
    if (!names.isEmpty()) { m.addSeparator(); for (int i = 0; i < names.size(); ++i) m.addItem(100 + i, names[i], true, names[i] == cur); }
    m.addSeparator();
    m.addItem(2, juce::String(juce::CharPointer_UTF8("Save current settings as a preset\xe2\x80\xa6")));
    if (!names.isEmpty()) {
      juce::PopupMenu del; for (int i = 0; i < names.size(); ++i) del.addItem(500 + i, names[i]);
      m.addSubMenu("Delete preset", del);
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&preset_),
                    [this, names, sp = juce::Component::SafePointer<RackCard>(this)](int id) {
      if (!sp || id <= 0) return;
      if (id == 1) {
        if (auto* f = fxNow()) for (int i = 0; i < f->numParams(); ++i) f->setParam(i, f->params()[(size_t)i].defaultValue);
        setPresetName("Default");
      } else if (id == 2) savePreset();
      else if (id >= 500) deletePreset(names[id - 500]);
      else if (id >= 100) {
        const auto n = names[id - 100];
        auto v = p_.presets_.values(name_, n);
        if (auto* f = fxNow()) for (int i = 0; i < (int)v.size() && i < f->numParams(); ++i) f->setParam(i, v[(size_t)i]);
        setPresetName(n);
      }
    });
  }
  void savePreset() {
    auto* w = new juce::AlertWindow("Save preset", "Name for this " + name_ + " preset:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "My " + name_, {});
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const int ch = p_.ch_, slot = slot_;
    const auto plugin = name_;
    w->enterModalState(true, juce::ModalCallbackFunction::create([w, ch, slot, plugin, sp = juce::Component::SafePointer<InsertPanel>(&p_)](int r) {
      const auto n = w->getTextEditorContents("name").trim();
      if (!sp || r != 1 || n.isEmpty()) return;
      auto* e = sp->ctx_.engine(); auto* f = e ? e->insertAt(ch, slot) : nullptr; if (!f) return;
      std::vector<float> v; for (int i = 0; i < f->numParams(); ++i) v.push_back(f->getParam(i));
      sp->presets_.save(plugin, n, v);
      sp->preset_[ch][slot] = n;
      sp->key_.clear(); sp->rebuild();
      sp->ctx_.note("Saved preset \"" + n + "\" for " + plugin);
    }), true);
  }
  void deletePreset(const juce::String& n) {
    const int ch = p_.ch_, slot = slot_;
    const auto plugin = name_;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Delete preset").withMessage("Delete the " + plugin + " preset \"" + n + "\"?")
                                     .withButton("Delete", 1).withButton("Cancel", 0),
                                 [n, ch, slot, plugin, sp = juce::Component::SafePointer<InsertPanel>(&p_)](int r) {
                                   if (!sp || r != 1) return;
                                   sp->presets_.remove(plugin, n);
                                   if (sp->preset_[ch][slot] == n) sp->preset_[ch].erase(slot);
                                   sp->key_.clear(); sp->rebuild();
                                 });
  }

  InsertPanel& p_;
  const int slot_;
  juce::String name_, category_;
  Finish finish_;
  bool hosted_ = false, open_ = false, active_ = true, dropHover_ = false;
  juce::TextButton byp_, tog_, rm_, fav_, info_, window_, preset_;
  std::vector<std::unique_ptr<PluginKnob>> knobs_;
  juce::Image plate_; juce::Point<int> plateSize_; float plateScale_ = 0.f;
};

// "+ Load into slot N" with the plugin picker: a card, or a blank module in the rack look.
class AddCard : public juce::Component {
 public:
  AddCard(InsertPanel& p, AppCtx& ctx, int ch, int slot, bool& showOld, bool rack, std::function<void(int)> refresh)
      : ctx_(ctx), ch_(ch), slot_(slot), showOld_(showOld), rack_(rack), refresh_(std::move(refresh)) {
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
    // Categories are collapsed: each one is a submenu that opens to its plugins (favourites stay on top, open).
    if (!favs.isEmpty()) box_.addSectionHeading("Categories");
    std::string cat;
    juce::PopupMenu sub; int inSub = 0;
    auto flush = [&] { if (inSub > 0) box_.getRootMenu()->addSubMenu(juce::String(cat) + "  (" + juce::String(inSub) + ")", sub); sub = {}; inSub = 0; };
    for (int i = 0; i < (int)list.size(); ++i) {
      if (list[(size_t)i].category != cat) { flush(); cat = list[(size_t)i].category; }
      const juce::String nm(list[(size_t)i].name);
      sub.addItem(favCount + i + 1, pluginlib::newEffects().contains(nm) ? nm + juce::String(juce::CharPointer_UTF8("  \xe2\x80\x94 new")) : nm);
      ++inSub;
      names_.add(list[(size_t)i].name);
    }
    flush();
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
    if (!rack_) {
      auto r = getLocalBounds().toFloat();
      cardFrame(g, r, false);
      T().drawBrush(g, r.reduced(1.f));
      g.setColour(Theme::cream()); g.setFont(Theme::uiFont(14.f, true));
      g.drawText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b Load into slot ")) + juce::String(slot_ + 1), getLocalBounds().reduced(12, 8).removeFromTop(22),
                 juce::Justification::centredLeft);
      g.setColour(Theme::dim()); g.setFont(Theme::uiFont(12.f));
      g.drawText(juce::String(count_) + " plugins" + (showOld_ ? "" : juce::String(juce::CharPointer_UTF8(" \xc2\xb7 latest versions"))),
                 getLocalBounds().reduced(12, 8).withTrimmedTop(64).removeFromTop(18), juce::Justification::centredLeft);
      return;
    }
    const auto f = blankFinish();
    cachedPlate(g, *this, plate_, plateSize_, plateScale_, f);
    auto b = getLocalBounds().withTrimmedBottom(3).toFloat();
    T().drawScrew(g, {b.getX() + 10.f, b.getY() + 10.f});
    T().drawScrew(g, {b.getRight() - 10.f, b.getY() + 10.f});
    g.setColour(Theme::cream()); g.setFont(Theme::spaced(Theme::uiFont(14.f, true), 0.08f));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xef\xbc\x8b LOAD SLOT ")) + juce::String(slot_ + 1), juce::Rectangle<float>(b.getX(), b.getY() + 4.f, b.getWidth(), 22.f),
               juce::Justification::centred);
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(11.5f));
    g.drawText(juce::String(count_) + " plugins" + (showOld_ ? "" : juce::String(juce::CharPointer_UTF8(" \xc2\xb7 latest versions"))),
               juce::Rectangle<float>(b.getX() + 12.f, b.getY() + 70.f, b.getWidth() - 24.f, 16.f), juce::Justification::centredLeft);
    for (float y = b.getY() + 126.f; y < b.getBottom() - 16.f; y += 9.f) {  // vents
      g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(b.getX() + 30.f, y, b.getWidth() - 60.f, 4.f, 2.f);
      g.setColour(juce::Colours::white.withAlpha(0.05f)); g.fillRect(b.getX() + 31.f, y + 4.f, b.getWidth() - 62.f, 1.f);
    }
  }
  void resized() override {
    if (!rack_) {
      auto r = getLocalBounds().reduced(12, 8);
      r.removeFromTop(26); box_.setBounds(r.removeFromTop(30)); r.removeFromTop(28);
      old_.setBounds(r.removeFromTop(24).removeFromLeft(170));
      return;
    }
    auto r = getLocalBounds().reduced(12, 0);
    box_.setBounds(r.withY(32).withHeight(30));
    old_.setBounds(r.withY(92).withHeight(24).withWidth(170));
  }
 private:
  AppCtx& ctx_; int ch_, slot_; bool& showOld_; bool rack_; std::function<void(int)> refresh_;
  juce::ComboBox box_; juce::TextButton old_; juce::StringArray names_; int count_ = 0;
  juce::Image plate_; juce::Point<int> plateSize_; float plateScale_ = 0.f;
};

// ---------------- RackWell: the chassis behind the modules ----------------
void RackWell::paint(juce::Graphics& g) {
  if (!rack) return;  // cards look: nothing behind the cards
  auto r = getLocalBounds().toFloat();
  g.setColour(juce::Colour(0xff060606)); g.fillRect(r);
  g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.8f), 0.f, r.getY(), juce::Colours::transparentBlack, 0.f, r.getY() + 14.f, false));
  g.fillRect(r.withHeight(14.f));
  const auto f = blankFinish();
  const float top = (float)kGap, h = r.getHeight() - 2.f * (float)kGap;
  for (float x = (float)used; x + (float)kBlankW <= r.getRight() - 2.f; x += (float)(kBlankW + kGap)) {  // blank plates in the unused slots
    juce::Rectangle<float> p(x, top, (float)kBlankW, h - 3.f);
    drawPlate(g, p, f);
    T().drawScrew(g, {p.getCentreX(), p.getY() + 9.f});
    T().drawScrew(g, {p.getCentreX(), p.getBottom() - 10.f});
    for (float y = p.getY() + 26.f; y < p.getBottom() - 24.f; y += 9.f) {
      g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(p.getX() + 14.f, y, p.getWidth() - 28.f, 4.f, 2.f);
      g.setColour(juce::Colours::white.withAlpha(0.05f)); g.fillRect(p.getX() + 15.f, y + 4.f, p.getWidth() - 30.f, 1.f);
    }
  }
}

// ---------------- InsertPanel ----------------
InsertPanel::InsertPanel(AppCtx& ctx, PresetStore& presets) : ctx_(ctx), presets_(presets) {
  view_.setViewedComponent(&row_, false);
  view_.setScrollBarsShown(true, true);  // short windows: the modules scroll instead of being cut off
  addAndMakeVisible(view_);
  for (auto* b : {&close1_, &close2_}) {
    addAndMakeVisible(*b); b->onClick = [this] { close(); };
    b->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95")));
    b->getProperties().set("noUpper", true); b->getProperties().set("fontH", 12.0);
    b->setTooltip("Close the insert panel");
  }
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

void InsertPanel::moveInsert(int channelId, int from, int to, bool quiet) {
  auto* e = ctx_.engine(); if (!e) return;
  if (!e->moveInsert(channelId, from, to)) { ctx_.note("Could not move the insert"); return; }
  // Same reorder for the open/closed state and the preset names, so each module stays as it was.
  std::vector<int> order; for (int s = 0; s < airdaw::kMaxInserts; ++s) if (s != from) order.push_back(s);
  order.insert(order.begin() + to, from);
  auto& m = open_[channelId];
  std::map<int, bool> next; for (int s = 0; s < airdaw::kMaxInserts; ++s) next[s] = m[order[(size_t)s]];
  m = next;
  auto& pm = preset_[channelId];
  std::map<int, juce::String> nextP;
  for (int s = 0; s < airdaw::kMaxInserts; ++s) { auto it = pm.find(order[(size_t)s]); if (it != pm.end()) nextP[s] = it->second; }
  pm = nextP;
  if (auto* c = channelOf(*e, channelId); c && !quiet) ctx_.note("Moved " + juce::String(e->insertAt(channelId, to)->name()) + " to slot " + juce::String(to + 1) +
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
  juce::String k; k << ch_ << "|" << target_ << "|" << (int)showOld_ << "|" << ctx_.props.getValue("favoritePlugins") << "|" << T().inserts << "|";
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
  rack_ = T().inserts == "rack";
  row_.rack = rack_;
  cards_.clear(); addCard_.reset();
  for (int s = 0; s < airdaw::kMaxInserts; ++s)
    if (e->insertAt(ch_, s)) {
      if (rack_) cards_.push_back(std::make_unique<RackCard>(*this, s));
      else cards_.push_back(std::make_unique<ClassicCard>(*this, s));
      row_.addAndMakeVisible(*cards_.back());
    }
  int free = target_;
  if (free < 0 || e->insertAt(ch_, free)) { free = -1; for (int s = 0; s < airdaw::kMaxInserts; ++s) if (!e->insertAt(ch_, s)) { free = s; break; } }
  addAt_ = (int)cards_.size();
  if (free >= 0) {
    // the add card stands where the new plugin will go in the chain (before a Hit Record Meter at the end)
    addAt_ = 0; for (int s = 0; s < free; ++s) addAt_ += e->insertAt(ch_, s) != nullptr;
    addCard_ = std::make_unique<AddCard>(*this, ctx_, ch_, free, showOld_, rack_, [this](int loaded) {
      target_ = -1; if (loaded >= 0) { open_[ch_][loaded] = true; preset_[ch_].erase(loaded); }
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
  int h = rack_ ? kMinRowH : 96;
  for (auto& c : cards_) h = juce::jmax(h, c->prefHeight());
  const int scroll = view_.isHorizontalScrollBarShown() ? 10 : 0;
  return rack_ ? h + 2 * kGap + kRail + 20 + scroll : h + 20 + scroll;
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
    if (rack_) g.drawFittedText("Insert rack\ndrag a module's name\nto reorder", hd.removeFromTop(46), juce::Justification::topLeft, 3);
    else g.drawFittedText(juce::String(juce::CharPointer_UTF8("Insert chain\ndrag \xe2\xa0\xbf to reorder")), hd.removeFromTop(34), juce::Justification::topLeft, 2);
  }
  if (!rack_ || chassis_.isEmpty()) return;
  // chassis: wood cheeks (the trim setting's material), a top rail with the power lamp, a dark lip round the well
  auto ch = chassis_;
  g.setColour(juce::Colours::black.withAlpha(0.6f)); g.fillRoundedRectangle(ch.toFloat().translated(0.f, 4.f), 6.f);
  T().drawTrim(g, ch.withWidth(kCheek), true, true);
  T().drawTrim(g, ch.withTrimmedLeft(ch.getWidth() - kCheek), true, false);
  auto rail = ch.reduced(kCheek, 0).withHeight(kRail).toFloat();
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3c), 0.f, rail.getY(), juce::Colour(0xff202022), 0.f, rail.getBottom(), false));
  g.fillRect(rail);
  g.setColour(juce::Colours::white.withAlpha(0.12f)); g.drawHorizontalLine((int)rail.getY(), rail.getX(), rail.getRight());
  g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(9.5f, true), 0.2f));
  auto pw = rail.reduced(12.f, 0.f).removeFromRight(60.f);
  g.drawText("POWER", pw, juce::Justification::centredRight);
  drawLed(g, {pw.getX() + 4.f, pw.getCentreY()}, true);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(ch.toFloat().reduced(0.5f), 4.f, 1.f);
}

void InsertPanel::resized() {
  auto r = getLocalBounds().reduced(10);
  auto hd = r.removeFromLeft(122);
  close1_.setBounds(hd.withTrimmedTop(rack_ ? 80 : 62).removeFromTop(26).removeFromLeft(30));
  // the right X: centred in its own column at the panel's right edge, halfway down
  close2_.setBounds(r.removeFromRight(kCloseCol).withSizeKeepingCentre(30, 26));
  if (!rack_) {  // cards look (the original layout)
    r.removeFromRight(8);
    chassis_ = {};
    view_.setBounds(r);
    int x = 0, h = 120;
    for (int i = 0; i <= (int)cards_.size(); ++i) {
      if (i == addAt_ && addCard_) { addCard_->setBounds(x, 0, 230, 120); x += 240; }
      if (i == (int)cards_.size()) break;
      auto& c = cards_[(size_t)i];
      const int w = c->prefWidth(); c->setBounds(x, 0, w, c->prefHeight()); x += w + 10; h = juce::jmax(h, c->prefHeight());
    }
    row_.used = x;
    row_.setSize(juce::jmax(x, 10), h);
    return;
  }
  r.removeFromRight(6);
  chassis_ = r;
  view_.setBounds(r.withTrimmedTop(kRail).reduced(kCheek, 0));
  int h = kMinRowH;
  for (auto& c : cards_) h = juce::jmax(h, c->prefHeight());
  int x = kGap;
  for (int i = 0; i <= (int)cards_.size(); ++i) {
    if (i == addAt_ && addCard_) { addCard_->setBounds(x, kGap, 230, h); x += 230 + kGap; }
    if (i == (int)cards_.size()) break;
    auto& c = cards_[(size_t)i];
    const int w = c->prefWidth(); c->setBounds(x, kGap, w, h); x += w + kGap;
  }
  row_.used = x;
  row_.setSize(juce::jmax(x, view_.getMaximumVisibleWidth()), juce::jmax(h + 2 * kGap, view_.getMaximumVisibleHeight()));
  row_.repaint();
}

}  // namespace ui
