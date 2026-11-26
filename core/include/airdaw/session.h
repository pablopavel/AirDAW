// Session (project) files and timed auto-save.
#pragma once
#include <string>
#include <vector>
#include "airdaw/engine.h"

namespace airdaw {

struct SessionResult { bool ok = false; std::string error; std::vector<std::string> warnings; };

// Serialises mixer state, inserts + parameters, takes, loop/punch to JSON text (UI thread).
std::string sessionToJson(Engine& e, const std::string& baseDir = {});
// Atomic write: temp file then rename; the previous file is kept as <path>.bak.
SessionResult saveSession(Engine& e, const std::string& path);
// Applies a session to the engine. Missing plugins/takes become warnings, not failures.
SessionResult loadSession(Engine& e, const std::string& path);

struct AutoSaveSettings {
  bool enabled = false;
  int intervalSec = 300;  // 30 s .. 1 h
  int keep = 10;          // rolling copies
};

class AutoSaver {
 public:
  explicit AutoSaver(Engine& e) : e_(e) {}
  void configure(const AutoSaveSettings& s);
  const AutoSaveSettings& settings() const { return s_; }
  // Where autosaves go and how they are named. Never touches the user's own session file.
  void setLocation(const std::string& dir, const std::string& baseName);
  // Call from a UI timer (about once a second). Returns the path written, or "" if nothing was saved.
  // Skips while recording (retries next tick) and when nothing changed since the last autosave.
  std::string tick(double nowSeconds);
  std::string saveNow();
  static std::string latestAutosave(const std::string& dir, const std::string& baseName);
 private:
  void prune();
  Engine& e_; AutoSaveSettings s_; std::string dir_, base_;
  double last_ = -1; size_t lastHash_ = 0; bool haveHash_ = false;
};
}  // namespace airdaw
