// Tiny real-time-safe fork/join pool. The audio thread calls run(); workers spin briefly for new
// work, then yield, then park on a condition variable when the audio stream is idle.
// The hot path (publish job, pull indices, wait for completion) uses only atomics: no locks or allocation.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace airdaw {

class WorkPool {
 public:
  using Fn = void (*)(void* ctx, int index);
  WorkPool() = default;
  ~WorkPool() { stop(); }
  // Not real-time safe: call while audio is stopped. `onThreadStart` runs on each new worker (e.g. set priority).
  void start(int workers, std::function<void()> onThreadStart = {});
  void stop();
  int workers() const { return (int)threads_.size(); }

  // Audio thread. Runs fn(ctx, 0..count-1) across the workers and the calling thread; returns when all are done.
  void run(int count, Fn fn, void* ctx);

 private:
  void workerLoop();
  std::vector<std::thread> threads_;
  std::function<void()> onStart_;
  // One word describes the whole job so a late worker can never mix two jobs:
  //   bits 63..32 generation | bits 31..20 task count (<4096) | bits 19..0 next index to hand out
  std::atomic<uint64_t> state_{0};
  std::atomic<int> done_{0};
  std::atomic<int> parked_{0};
  std::atomic<bool> stop_{false};
  // Only read after successfully claiming a task, which proves the job is still unfinished (so they are stable).
  std::atomic<Fn> fn_{nullptr}; std::atomic<void*> ctx_{nullptr};
  std::mutex mu_; std::condition_variable cv_;
};

}  // namespace airdaw
