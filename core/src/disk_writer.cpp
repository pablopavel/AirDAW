#include "airdaw/disk_writer.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace airdaw {

DiskWriter::DiskWriter(int numTracks, int sampleRate) : sr_(sampleRate) {
  for (int i = 0; i < numTracks; ++i) recs_.push_back(std::make_unique<Rec>());
  names_.assign(numTracks, "track");
  th_ = std::thread([this] { loop(); });
}
DiskWriter::~DiskWriter() { run_ = false; th_.join(); for (int t = 0; t < (int)recs_.size(); ++t) if (recs_[t]->wav.isOpen()) recs_[t]->wav.close(); }

void DiskWriter::setFolder(const std::string& dir, const std::vector<std::string>& names) {
  std::lock_guard<std::mutex> l(mu_);
  dir_ = dir; for (size_t i = 0; i < names.size() && i < names_.size(); ++i) names_[i] = names[i];
}

void DiskWriter::beginTake(int t, int64_t startPos, int ch) { recs_[t]->markers.push({recs_[t]->ring.written(), startPos, ch}); }
void DiskWriter::endTake(int t) { recs_[t]->markers.push({recs_[t]->ring.written(), -1, 0}); }

void DiskWriter::push(int t, const float* l, const float* r, int n, int ch) {
  Rec& rc = *recs_[t];
  const size_t need = (size_t)n * ch;
  if (rc.ring.freeSpace() < need) { dropped_ += n; rc.droppedTake += n; return; }
  if (ch == 1) { rc.ring.write(l, n); return; }
  float tmp[512];
  for (int i = 0; i < n; i += 256) {
    int m = std::min(256, n - i);
    for (int k = 0; k < m; ++k) { tmp[2 * k] = l[i + k]; tmp[2 * k + 1] = r[i + k]; }
    rc.ring.write(tmp, (size_t)m * 2);
  }
}

std::string DiskWriter::nextPath(int t) {
  std::string base;
  { std::lock_guard<std::mutex> l(mu_); base = dir_ + "/" + names_[t]; }
  for (char& c : base) if (c == ' ') c = '_';
  Rec& rc = *recs_[t];
  for (;;) {
    char suffix[32]; std::snprintf(suffix, sizeof suffix, "_take%03d.wav", ++rc.takeNo);
    std::string p = base + suffix;
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); continue; }  // never overwrite
    return p;
  }
}

void DiskWriter::sweep(int t, std::vector<float>& tmp) {
  Rec& rc = *recs_[t];
  for (;;) {
    if (!rc.hasPending && rc.markers.pop(rc.pending)) rc.hasPending = true;
    uint64_t popped = rc.ring.written() - rc.ring.readable();
    uint64_t limit = rc.hasPending ? rc.pending.atCount : rc.ring.written();
    size_t avail = (size_t)std::min<uint64_t>(limit - popped, rc.ring.readable());
    size_t frameSize = (size_t)std::max(1, rc.channels);
    if (avail >= frameSize) {
      size_t take = std::min(avail, tmp.size()); take -= take % frameSize;
      rc.ring.read(tmp.data(), take);
      if (rc.wav.isOpen()) rc.wav.write(tmp.data(), take / frameSize);
      continue;
    }
    if (rc.hasPending && popped >= rc.pending.atCount) {
      if (rc.wav.isOpen()) {
        FinishedTake ft{t, rc.path, rc.start, (int64_t)rc.wav.framesWritten(), rc.droppedTake.exchange(0)};
        rc.wav.close();
        std::lock_guard<std::mutex> l(mu_); finished_.push_back(ft);
      }
      if (rc.pending.start >= 0) {
        rc.channels = rc.pending.channels; rc.start = rc.pending.start;
        rc.path = nextPath(t); rc.droppedTake = 0;
        if (!rc.wav.open(rc.path, rc.channels, sr_)) std::fprintf(stderr, "AirDAW: cannot open %s\n", rc.path.c_str());
      }
      rc.hasPending = false;
      continue;
    }
    break;
  }
}

void DiskWriter::loop() {
  std::vector<float> tmp(1 << 16);
  while (run_) {
    for (int t = 0; t < (int)recs_.size(); ++t) sweep(t, tmp);
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  for (int t = 0; t < (int)recs_.size(); ++t) sweep(t, tmp);
}

std::vector<FinishedTake> DiskWriter::pollFinished() {
  std::lock_guard<std::mutex> l(mu_);
  auto out = std::move(finished_); finished_.clear(); return out;
}

void DiskWriter::flushAndWait() {
  for (int i = 0; i < 500; ++i) {  // up to ~2 s
    bool idle = true;
    for (auto& r : recs_) idle &= (r->ring.readable() == 0 && r->markers.empty());
    if (idle) { std::this_thread::sleep_for(std::chrono::milliseconds(15)); return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
}
}  // namespace airdaw
