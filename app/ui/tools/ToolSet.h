// Reference-suite / Listen tools: one shared set. Each tool lives in a ToolCard; ToolSet owns every card and
// remembers where it is shown:
//   reference suite (meter dock)  - the tools the user picked, in order, with their widths
//   Listen mode row               - every other tool, rearrangeable and minimisable
// A tool is in exactly one of the two places. All of this is saved in the app settings.
#pragma once
#include <map>
#include <memory>
#include <vector>
#include "../AppCtx.h"
#include "../Theme.h"
#include "AnalysisHub.h"

namespace ui {

class Tool : public juce::Component {
 public:
  virtual void update(const AnalysisHub&) = 0;           // ~15x/s
  virtual juce::String headerValue() const { return {}; } // small live value in the card header
  virtual int defaultWidth() const { return 300; }
  AppCtx* ctx = nullptr;                                  // for tools that need the engine or settings
};

class ToolCard : public juce::Component, public juce::SettableTooltipClient {
 public:
  enum class Place { Suite, Listen };
  ToolCard(juce::String id, juce::String title, std::unique_ptr<Tool> tool);
  const juce::String& id() const { return id_; }
  Tool& tool() { return *tool_; }
  void setPlace(Place p);
  Place place() const { return place_; }
  void setMinimised(bool m);
  bool minimised() const { return min_; }
  bool folded() const;                // squeezed to a slim tab (narrower than kFoldW)
  static constexpr int kFoldW = 60;
  std::function<void()> onRemove;     // suite: x
  std::function<void()> onMinimise;   // Listen: minimise / restore
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDrag(const juce::MouseEvent&) override;
  static constexpr int kHeader = 24;
 private:
  juce::String id_, title_;
  std::unique_ptr<Tool> tool_;
  juce::TextButton x_, minBtn_;
  Place place_ = Place::Listen;
  bool min_ = false;
};

class ToolSet {
 public:
  ToolSet(AppCtx& ctx);
  struct Info { const char* id; const char* title; };
  static const std::vector<Info>& registry();
  ToolCard& card(const juce::String& id);           // created on first use
  const std::vector<juce::String>& suite() const { return suite_; }
  std::vector<juce::String> listenOrder() const;    // every tool not in the suite, in the user's order
  bool inSuite(const juce::String& id) const;
  void addToSuite(const juce::String& id, int index = -1);
  void removeFromSuite(const juce::String& id, int listenIndex = -1);
  void moveInListen(const juce::String& id, int index);
  void toggleMinimised(const juce::String& id);
  // Widths are kept apart for the main view and Listen mode (each view keeps its own sizes); Listen mode starts
  // from the main view's widths until a tool is resized there.
  int width(const juce::String& id) const;
  void setWidth(const juce::String& id, int w);
  void setListenView(bool on) { listenView_ = on; }
  void updateAll(const AnalysisHub& hub);           // only cards that are showing
  std::function<void()> onLayoutChanged;
 private:
  void save();
  AppCtx& ctx_;
  std::map<juce::String, std::unique_ptr<ToolCard>> cards_;
  std::vector<juce::String> suite_, listen_;
  std::map<juce::String, int> widths_, widthsListen_;
  bool listenView_ = false;
  juce::StringArray minimised_;
};

}  // namespace ui
