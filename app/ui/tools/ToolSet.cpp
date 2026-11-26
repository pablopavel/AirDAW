#include "ToolSet.h"
#include "Tools.h"

namespace ui {

// ---------------------------------------------------------------- ToolCard
ToolCard::ToolCard(juce::String id, juce::String title, std::unique_ptr<Tool> tool)
    : id_(std::move(id)), title_(std::move(title)), tool_(std::move(tool)) {
  addAndMakeVisible(*tool_);
  for (auto* b : {&x_, &minBtn_}) { addChildComponent(*b); b->getProperties().set("noUpper", true); b->getProperties().set("fontH", 11.0); }
  x_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x9c\x95")));
  x_.setTooltip("Remove from the reference suite (it stays available in Listen mode)");
  x_.onClick = [this] { if (onRemove) onRemove(); };
  minBtn_.setTooltip("Minimise / restore");
  minBtn_.onClick = [this] { if (onMinimise) onMinimise(); };
  setTooltip("Drag the header to move this tool");
}

void ToolCard::setPlace(Place p) { place_ = p; resized(); repaint(); }

void ToolCard::setMinimised(bool m) {
  min_ = m;
  minBtn_.setButtonText(juce::String(juce::CharPointer_UTF8(m ? "\xe2\x96\xb8" : "\xe2\x96\xbe")));
  resized(); repaint();
}

bool ToolCard::folded() const { return getWidth() > 0 && getWidth() < kFoldW; }

void ToolCard::paint(juce::Graphics& g) {
  auto& t = T();
  auto r = getLocalBounds().toFloat();
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0c0b0a), 0, r.getY(), juce::Colour(0xff050505), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, 5.f);
  g.setColour(juce::Colour(0x14ffffff)); g.drawHorizontalLine(1, r.getX() + 5.f, r.getRight() - 5.f);
  g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.reduced(0.5f), 5.f, 1.f);
  if (folded()) {  // squeezed by a wide Hit Record Meter: a slim tab with the title written upwards
    g.setColour(Theme::dim()); g.setFont(Theme::uiFont(14.f));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\xa0\xbf")), getLocalBounds().removeFromTop(kHeader), juce::Justification::centred);
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi).translated(0.f, (float)getHeight()));
    g.setColour(Theme::dim()); g.setFont(Theme::spaced(Theme::uiFont(10.5f), 0.16f));
    g.drawText(title_.toUpperCase(), juce::Rectangle<int>(10, 0, getHeight() - kHeader - 10, getWidth()), juce::Justification::centredLeft, true);
    g.restoreState();
    return;
  }
  auto head = getLocalBounds().removeFromTop(kHeader).reduced(8, 0);
  if (place_ == Place::Listen) {  // Listen cards: a brushed title bar like the mockup's .tch
    juce::Path p; p.addRoundedRectangle(0.f, 0.f, r.getWidth(), (float)kHeader, 5.f, 5.f, true, true, min_, min_);
    g.saveState(); g.reduceClipRegion(p); t.drawBrush(g, r.withHeight((float)kHeader)); g.restoreState();
  }
  g.setColour(Theme::dim()); g.setFont(Theme::uiFont(14.f));
  g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\xa0\xbf")), head.removeFromLeft(14), juce::Justification::centredLeft);
  if (x_.isVisible() || minBtn_.isVisible()) head.removeFromRight(26);
  g.setColour(place_ == Place::Listen ? Theme::cream() : Theme::dim());
  g.setFont(Theme::spaced(Theme::uiFont(10.5f, place_ == Place::Listen), 0.16f));
  g.drawText(title_.toUpperCase(), head, juce::Justification::centredLeft, true);
  const auto v = tool_->headerValue();
  if (v.isNotEmpty() && !(place_ == Place::Listen && min_)) {
    g.setFont(Theme::monoFont(11.f, false)); g.setColour(t.am());
    g.drawText(v, head, juce::Justification::centredRight, true);
  }
}

void ToolCard::resized() {
  const bool fold = folded();
  x_.setVisible(place_ == Place::Suite && !fold);
  minBtn_.setVisible(place_ == Place::Listen && !fold);
  tool_->setVisible(!fold && (place_ == Place::Suite || !min_));
  auto r = getLocalBounds();
  auto head = r.removeFromTop(kHeader);
  x_.setBounds(head.removeFromRight(26).reduced(2, 3));
  minBtn_.setBounds(x_.getBounds());
  tool_->setBounds(r.reduced(4, 2).withTrimmedBottom(4));
}

void ToolCard::mouseDrag(const juce::MouseEvent& e) {
  if (e.mouseDownPosition.y >= kHeader || e.getDistanceFromDragStart() < 6) return;
  if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
    if (!dc->isDragAndDropActive()) dc->startDragging("tool:" + id_, this);
}

// ---------------------------------------------------------------- ToolSet
const std::vector<ToolSet::Info>& ToolSet::registry() {
  static const std::vector<Info> r = {
      {"stereo", "Stereo field"}, {"rta", "RTA \xc2\xb7 31 band"}, {"loudness", "Loudness"},
      {"target", "Target loudness"}, {"tonal", "Tonal balance"}, {"spectrogram", "Spectrogram"},
      {"history", "Loudness history"}, {"bandphase", "Band phase / width"}, {"headroom", "Headroom"},
      {"notes", "Listening notes"}, {"earbreak", "Ear break"},
  };
  return r;
}

ToolSet::ToolSet(AppCtx& ctx) : ctx_(ctx) {
  auto known = [](const juce::String& id) { for (auto& i : registry()) if (id == i.id) return true; return false; };
  for (auto& id : juce::StringArray::fromTokens(ctx_.props.getValue("tools.suite", "stereo,rta,loudness"), ",", ""))
    if (known(id) && std::find(suite_.begin(), suite_.end(), id) == suite_.end()) suite_.push_back(id);
  for (auto& id : juce::StringArray::fromTokens(ctx_.props.getValue("tools.listen"), ",", ""))
    if (known(id) && std::find(listen_.begin(), listen_.end(), id) == listen_.end()) listen_.push_back(id);
  for (auto& i : registry())  // tools added in newer versions go to the end
    if (std::find(listen_.begin(), listen_.end(), juce::String(i.id)) == listen_.end()) listen_.push_back(i.id);
  for (auto& i : registry()) { const int w = ctx_.props.getIntValue(juce::String("tools.w.") + i.id, 0); if (w > 0) widths_[i.id] = w; }
  minimised_ = juce::StringArray::fromTokens(ctx_.props.getValue("tools.min"), ",", "");
}

ToolCard& ToolSet::card(const juce::String& id) {
  auto it = cards_.find(id);
  if (it != cards_.end()) return *it->second;
  juce::String title = id;
  for (auto& i : registry()) if (id == i.id) title = juce::String(juce::CharPointer_UTF8(i.title));
  auto tool = createTool(id);
  tool->ctx = &ctx_;
  auto c = std::make_unique<ToolCard>(id, title, std::move(tool));
  c->setMinimised(minimised_.contains(id));
  c->onRemove = [this, id] { removeFromSuite(id); };
  c->onMinimise = [this, id] { toggleMinimised(id); };
  auto& ref = *c;
  cards_[id] = std::move(c);
  return ref;
}

std::vector<juce::String> ToolSet::listenOrder() const {
  std::vector<juce::String> v;
  for (auto& id : listen_) if (!inSuite(id)) v.push_back(id);
  return v;
}
bool ToolSet::inSuite(const juce::String& id) const { return std::find(suite_.begin(), suite_.end(), id) != suite_.end(); }

void ToolSet::addToSuite(const juce::String& id, int index) {
  suite_.erase(std::remove(suite_.begin(), suite_.end(), id), suite_.end());
  if (index < 0 || index > (int)suite_.size()) index = (int)suite_.size();
  suite_.insert(suite_.begin() + index, id);
  save();
}

void ToolSet::removeFromSuite(const juce::String& id, int listenIndex) {
  suite_.erase(std::remove(suite_.begin(), suite_.end(), id), suite_.end());
  if (listenIndex >= 0) moveInListen(id, listenIndex);
  save();
}

// index: position among the tools currently in the Listen row.
void ToolSet::moveInListen(const juce::String& id, int index) {
  auto row = listenOrder();
  row.erase(std::remove(row.begin(), row.end(), id), row.end());
  index = juce::jlimit(0, (int)row.size(), index);
  row.insert(row.begin() + index, id);
  for (auto& s : suite_) row.push_back(s);  // suite tools keep a place in the full order (when they come back)
  listen_ = row;
  save();
}

void ToolSet::toggleMinimised(const juce::String& id) {
  if (minimised_.contains(id)) minimised_.removeString(id); else minimised_.add(id);
  card(id).setMinimised(minimised_.contains(id));
  save();
}

int ToolSet::width(const juce::String& id) const {
  auto it = widths_.find(id);
  if (it != widths_.end()) return it->second;
  auto c = cards_.find(id);
  return c != cards_.end() ? c->second->tool().defaultWidth() : 300;
}
void ToolSet::setWidth(const juce::String& id, int w) { widths_[id] = w; ctx_.props.setValue("tools.w." + id, w); }

void ToolSet::updateAll(const AnalysisHub& hub) {
  for (auto& [id, c] : cards_)
    if (c->isShowing() && c->tool().isVisible()) { c->tool().update(hub); c->repaint(0, 0, c->getWidth(), ToolCard::kHeader); }
}

void ToolSet::save() {
  juce::StringArray s; for (auto& x : suite_) s.add(x);
  juce::StringArray l; for (auto& x : listen_) l.add(x);
  ctx_.props.setValue("tools.suite", s.joinIntoString(","));
  ctx_.props.setValue("tools.listen", l.joinIntoString(","));
  ctx_.props.setValue("tools.min", minimised_.joinIntoString(","));
  if (onLayoutChanged) onLayoutChanged();
}

}  // namespace ui
