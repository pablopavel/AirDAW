#include "airdaw/work_pool.h"
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define AIRDAW_X86 1
#endif

namespace airdaw {

static inline void cpuRelax() {
#ifdef AIRDAW_X86
  _mm_pause();
#else
  std::this_thread::yield();
#endif
}

void WorkPool::start(int workers, std::function<void()> onThreadStart) {
  stop();
  onStart_ = std::move(onThreadStart);
  stop_ = false;
  for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { workerLoop(); });
}

void WorkPool::stop() {
  if (threads_.empty()) return;
  stop_ = true;
  { std::lock_guard<std::mutex> l(mu_); }
  cv_.notify_all();
  for (auto& t : threads_) t.join();
  threads_.clear();
}

void WorkPool::run(int count, Fn fn, void* ctx) {
  if (threads_.empty() || count <= 1 || count >= 4096) { for (int i = 0; i < count; ++i) fn(ctx, i); return; }
  fn_.store(fn, std::memory_order_relaxed); ctx_.store(ctx, std::memory_order_relaxed);
  done_.store(0, std::memory_order_relaxed);
  const uint64_t gen = (state_.load(std::memory_order_relaxed) >> 32) + 1;
  state_.store((gen << 32) | ((uint64_t)count << 20), std::memory_order_seq_cst);  // publishes the job
  if (parked_.load(std::memory_order_seq_cst) > 0) { { std::lock_guard<std::mutex> l(mu_); } cv_.notify_all(); }

  // The audio thread pulls work too.
  for (;;) {
    uint64_t s = state_.load(std::memory_order_acquire);
    if ((int)(s & 0xfffffu) >= count) break;
    uint32_t idx = (uint32_t)(s & 0xfffffu);
    if (state_.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel)) { fn(ctx, (int)idx); done_.fetch_add(1, std::memory_order_acq_rel); }
  }
  int spins = 0;
  while (done_.load(std::memory_order_acquire) < count) {
    if (++spins < 4000) cpuRelax(); else std::this_thread::yield();
  }
}

void WorkPool::workerLoop() {
#ifdef AIRDAW_X86
  _mm_setcsr(_mm_getcsr() | 0x8040);  // flush-to-zero / denormals-are-zero on this thread too
#endif
  if (onStart_) onStart_();
  uint64_t seenGen = state_.load() >> 32;
  int idle = 0;
  while (!stop_.load(std::memory_order_relaxed)) {
    uint64_t s = state_.load(std::memory_order_acquire);
    if ((s >> 32) == seenGen) {
      ++idle;
      if (idle < 4000) cpuRelax();
      else if (idle < 6000) std::this_thread::yield();
      else {  // idle for a while (transport stopped / device closed): sleep until work arrives
        std::unique_lock<std::mutex> l(mu_);
        parked_.fetch_add(1, std::memory_order_seq_cst);
        cv_.wait(l, [&] { return stop_.load() || (state_.load(std::memory_order_seq_cst) >> 32) != seenGen; });
        parked_.fetch_sub(1, std::memory_order_seq_cst);
        idle = 0;
      }
      continue;
    }
    idle = 0;
    const uint64_t gen = s >> 32;
    for (;;) {  // pull indices for this generation only
      s = state_.load(std::memory_order_acquire);
      if ((s >> 32) != gen) break;
      uint32_t idx = (uint32_t)(s & 0xfffffu), cnt = (uint32_t)((s >> 20) & 0xfffu);
      if (idx >= cnt) break;
      if (state_.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel)) { fn_.load(std::memory_order_relaxed)(ctx_.load(std::memory_order_relaxed), (int)idx); done_.fetch_add(1, std::memory_order_acq_rel); }
    }
    seenGen = gen;
  }
}

}  // namespace airdaw
