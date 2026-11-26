// Anything that can sit in an insert slot: a built-in Airwindows effect, or a hosted VST3 plugin.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace airdaw {

struct FxParamInfo {
  std::string name;     // e.g. "Input"
  std::string label;    // unit label, e.g. "dB"
  float defaultValue;   // normalised 0..1
};

class InsertProcessor {
 public:
  virtual ~InsertProcessor() = default;
  virtual const std::string& name() const = 0;
  // Identifies the insert in session files. Airwindows: the effect name. Others: "<kind>:<locator>".
  virtual std::string sessionId() const { return name(); }
  virtual int numParams() const = 0;
  virtual const std::vector<FxParamInfo>& params() const = 0;

  // UI thread. Normalised 0..1.
  virtual void setParam(int i, float v) = 0;
  virtual float getParam(int i) const = 0;
  virtual std::string paramDisplay(int i) = 0;
  virtual void setBypass(bool b) = 0;
  virtual bool bypassed() const = 0;
  // Opaque state blob (base64 text) for plugins whose full state is not just their parameters.
  virtual std::string saveState() { return {}; }
  virtual void loadState(const std::string&) {}

  // Audio thread: in place on L/R. No allocation, no locks.
  virtual void process(float* l, float* r, int n) = 0;
  virtual int latencySamples() const { return 0; }
};

// Creates non-Airwindows inserts when a session is loaded (e.g. "vst3:/path/Meter.vst3").
using InsertFactory = std::function<std::unique_ptr<InsertProcessor>(const std::string& sessionId, double sampleRate)>;

}  // namespace airdaw
