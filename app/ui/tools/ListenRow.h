// Listen mode's tool row (mockup #tools): every tool that is not in the reference suite, in the user's order.
// Drag a card sideways to reorder, up into the reference suite to add it there, or drop a suite tool here.
#pragma once
#include "ToolSet.h"

namespace ui {

class ListenRow : public juce::Component, public juce::DragAndDropTarget {
 public:
  explicit ListenRow(ToolSet& tools) : tools_(tools) {
    view_.setViewedComponent(&content_, false);
    view_.setScrollBarsShown(false, true);
    view_.setScrollBarThickness(8);
    addAndMakeVisible(view_);
  }
  void rebuild() {
    const auto ids = tools_.listenOrder();
    for (int i = content_.getNumChildComponents(); --i >= 0;)
      if (auto* c = dynamic_cast<ToolCard*>(content_.getChildComponent(i)); c && std::find(ids.begin(), ids.end(), c->id()) == ids.end())
        content_.removeChildComponent(c);
    for (auto& id : ids) { auto& c = tools_.card(id); c.setPlace(ToolCard::Place::Listen); content_.addAndMakeVisible(c); }
    resized();
  }
  void resized() override {
    view_.setBounds(getLocalBounds());
    const int h = juce::jmax(60, getHeight() - (view_.isHorizontalScrollBarShown() ? 10 : 0));
    int x = 0;
    for (auto& id : tools_.listenOrder()) {
      auto& c = tools_.card(id);
      const int w = c.minimised() ? 190 : 310;
      c.setBounds(x, 0, w, c.minimised() ? ToolCard::kHeader : h); x += w + 10;
    }
    content_.setSize(juce::jmax(x, 10), h);
  }
  void paintOverChildren(juce::Graphics& g) override {
    if (dropX_ >= 0) { T().drawGlow(g, {(float)dropX_ - 1.f, 0.f, 3.f, (float)getHeight()}, T().am(), 12.f, 0.9f);
                       g.setColour(T().am()); g.fillRect(dropX_ - 1, 0, 3, getHeight()); }
  }
  bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("tool:"); }
  void itemDragMove(const SourceDetails& d) override { const int i = indexAt(d.localPosition.x); dropX_ = insertX(i); repaint(); }
  void itemDragExit(const SourceDetails&) override { dropX_ = -1; repaint(); }
  void itemDropped(const SourceDetails& d) override {
    dropX_ = -1; repaint();
    const auto id = d.description.toString().fromFirstOccurrenceOf("tool:", false, false);
    int idx = indexAt(d.localPosition.x);
    const auto row = tools_.listenOrder();
    auto cur = std::find(row.begin(), row.end(), id);
    if (cur != row.end() && (int)(cur - row.begin()) < idx) --idx;  // moving right within the row
    juce::MessageManager::callAsync([this, id, idx] {
      if (tools_.inSuite(id)) tools_.removeFromSuite(id, idx); else tools_.moveInListen(id, idx);
    });
  }
 private:
  int indexAt(int x) const {
    x += view_.getViewPositionX();
    int i = 0;
    for (auto& id : tools_.listenOrder()) { auto& c = tools_.card(id); if (x < c.getX() + c.getWidth() / 2) return i; ++i; }
    return i;
  }
  int insertX(int idx) const {
    const auto row = tools_.listenOrder();
    int x = 4;
    if (idx < (int)row.size()) x = tools_.card(row[(size_t)idx]).getX() - 5;
    else if (!row.empty()) { auto& c = tools_.card(row.back()); x = c.getRight() + 5; }
    return x - view_.getViewPositionX();
  }
  ToolSet& tools_;
  juce::Viewport view_;
  juce::Component content_;
  int dropX_ = -1;
};

}  // namespace ui
