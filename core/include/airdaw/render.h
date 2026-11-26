// Offline mix export ("bounce"): renders an engine's mix to a stereo WAV file as fast as the CPU allows.
// Use a dedicated engine (e.g. a copy of the session loaded into a fresh Engine), never the one the audio
// device is running: rendering moves its transport and calls process() from this thread.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "airdaw/engine.h"

namespace airdaw {

struct RenderOptions {
  int64_t start = 0, end = 0;  // sample range of the song to render (end exclusive)
  double tailSeconds = 2.0;    // keep rendering after `end` so reverb/delay tails ring out
  int bits = 24;               // 24 = PCM, 32 = float
  int blockSize = 1024;
  double sampleRate = 0;       // informational (callers' reports); the engine's own rate is used for rendering
};

struct RenderResult {
  bool ok = false;
  std::string error;
  int64_t frames = 0;
  float peak = 0.f;  // highest absolute sample, linear
};

// End of the song: the latest end of any track's active take (0 if there are none).
int64_t songEnd(Engine& e);

// Renders [start, end) plus the tail. Disarms every track, turns the metronome and live input off, plays
// through the transport. `progress(fraction)` is called every block; return false from it to cancel.
RenderResult renderMix(Engine& e, const std::string& path, const RenderOptions& o,
                       const std::function<bool(double)>& progress = {});

}  // namespace airdaw
