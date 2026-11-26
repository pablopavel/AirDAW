#include "airdaw/render.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "airdaw/wav_io.h"

namespace airdaw {

int64_t songEnd(Engine& e) {
  int64_t end = 0;
  for (int t = 0; t < kMaxTracks; ++t)
    if (const Take* tk = e.track(t).take.load()) end = std::max(end, tk->start + (int64_t)tk->l.size());
  return end;
}

RenderResult renderMix(Engine& e, const std::string& path, const RenderOptions& o, const std::function<bool(double)>& progress) {
  RenderResult r;
  if (o.end <= o.start) { r.error = "nothing to render (the range is empty)"; return r; }
  // An export is the mix of the recorded takes only: no live input, no click, no recording, no looping.
  for (int t = 0; t < kMaxTracks; ++t) { e.track(t).armed = false; e.track(t).audition = false; }
  auto& m = e.metronome(); m.enabled = false; m.countInBars = 0;
  auto& tr = e.transport(); tr.stop(); tr.loopOn = false; tr.punchOn = false;
  tr.seek(o.start);

  WavWriter wav;
  if (!wav.open(path, 2, (int)std::lround(e.sampleRate()), o.bits)) { r.error = "cannot create " + path; return r; }
  const int64_t total = (o.end - o.start) + (int64_t)std::llround(std::max(0.0, o.tailSeconds) * e.sampleRate());
  const int bs = std::clamp(o.blockSize, 16, kMaxBlock);
  std::vector<float> l((size_t)bs), rr((size_t)bs), inter((size_t)bs * 2);
  float* out[2] = {l.data(), rr.data()};

  auto discard = [&](const char* why) {  // stop, and remove the incomplete file
    tr.stop(); wav.close();
    std::error_code ec; std::filesystem::remove(std::filesystem::u8path(path), ec);
    r.error = why; return r;
  };
  tr.play();  // the first block applies the seek, then plays from there
  int64_t done = 0;
  while (done < total) {
    const int n = (int)std::min<int64_t>(bs, total - done);
    e.process(nullptr, 0, out, 2, n);
    for (int i = 0; i < n; ++i) {
      inter[(size_t)(2 * i)] = l[(size_t)i]; inter[(size_t)(2 * i + 1)] = rr[(size_t)i];
      r.peak = std::max(r.peak, std::max(std::fabs(l[(size_t)i]), std::fabs(rr[(size_t)i])));
    }
    if (!wav.write(inter.data(), (size_t)n)) return discard("disk full or write error");
    done += n;
    if (progress && !progress((double)done / (double)total)) return discard("cancelled");
  }
  tr.stop();
  wav.close();
  r.ok = true; r.frames = done;
  return r;
}

}  // namespace airdaw
