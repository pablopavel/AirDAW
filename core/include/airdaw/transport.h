#pragma once
#include <atomic>
#include <cstdint>
namespace airdaw {
// Positions are in samples. The audio thread owns `pos`; the UI changes position with seek().
struct Transport {
  std::atomic<bool> playing{false}, recording{false}, loopOn{false}, punchOn{false};
  std::atomic<int64_t> pos{0}, loopIn{0}, loopOut{0}, punchIn{0}, punchOut{0};
  std::atomic<int64_t> seekReq{-1};
  void play() { playing = true; }
  void stop() { playing = false; recording = false; }
  void record() { recording = true; playing = true; }
  void seek(int64_t p) { seekReq.store(p < 0 ? 0 : p); }
};
}  // namespace airdaw
