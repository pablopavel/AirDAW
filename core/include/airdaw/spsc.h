// Single-producer / single-consumer lock-free ring. UI thread -> audio thread commands,
// and audio thread -> UI thread "retired object" returns (so the audio thread never frees memory).
#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

namespace airdaw {
template <typename T, size_t N>
class Spsc {
  static_assert((N & (N - 1)) == 0, "N must be a power of two");
 public:
  bool push(const T& v) {
    auto h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) == N) return false;
    buf_[h & (N - 1)] = v;
    head_.store(h + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& out) {
    auto t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;
    out = buf_[t & (N - 1)];
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }
 bool empty() const { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }
 private:
  T buf_[N];
  std::atomic<size_t> head_{0}, tail_{0};
};
}  // namespace airdaw
