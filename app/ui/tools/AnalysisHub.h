// Everything the reference-suite / Listen tools display, measured from the engine's master tap on the UI side.
#pragma once
#include <array>
#include <memory>
#include <vector>
#include "airdaw/analysis.h"
#include "HitMeter.h"

namespace ui {

class AnalysisHub {
 public:
  explicit AnalysisHub(double sampleRate)
      : sr_(sampleRate), loud_(sampleRate), bands_(sampleRate), stereo_(sampleRate), hit_(sampleRate) { scope_.assign(kScope * 2, 0.f); }

  // Master audio (interleaved stereo). Integrated loudness / LRA / DR only accumulate while `playing`.
  void feed(const float* il, size_t frames, bool playing) {
    playing_ = playing;
    loud_.process(il, frames, playing);
    bands_.process(il, frames);
    stereo_.process(il, frames);
    for (size_t i = 0; i < frames; i += 2) {  // goniometer: every 2nd frame into a ring
      scope_[2 * scopePos_] = il[2 * i]; scope_[2 * scopePos_ + 1] = il[2 * i + 1];
      scopePos_ = (scopePos_ + 1) % kScope;
    }
    hit_.process(il, frames);  // the Hit Record Meter's dark view (same measurements as the plugin)
  }

  HitMeterAnalyzer* hitMeter() { return &hit_; }
  void refresh() { last_ = loud_.read(); }  // ~15x/s: the readout is cheap to copy, read() is not free
  void resetProgramme() { loud_.resetIntegrated(); last_ = loud_.read(); }
  void resetPeaks() { loud_.resetPeaks(); last_ = loud_.read(); }

  const airdaw::LoudnessReadout& loud() const { return last_; }
  float band(int b) const { return bands_.bandDb(b); }
  float bandCorrelation(int b) const { return stereo_.correlation(b); }
  const std::vector<float>& history() const { return loud_.history(); }
  bool playing() const { return playing_; }
  double sampleRate() const { return sr_; }
  // The latest n goniometer points (L, R pairs), oldest first.
  void latestScope(std::vector<float>& out, size_t n) const {
    n = std::min(n, kScope); out.resize(2 * n);
    for (size_t k = 0; k < n; ++k) { const size_t p = (scopePos_ + kScope - n + k) % kScope; out[2 * k] = scope_[2 * p]; out[2 * k + 1] = scope_[2 * p + 1]; }
  }

  // Set by the app: where the playhead is (notes), and whether audio is flowing at all.
  double playheadSeconds = 0.0;

 private:
  static constexpr size_t kScope = 4096;
  double sr_;
  airdaw::LoudnessMeter loud_;
  airdaw::BandAnalyzer bands_;
  airdaw::StereoBandMeter stereo_;
  HitMeterAnalyzer hit_;
  airdaw::LoudnessReadout last_;
  std::vector<float> scope_;
  size_t scopePos_ = 0;
  bool playing_ = false;
};

}  // namespace ui
