#include "airdaw/analysis.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace airdaw {

static constexpr double kPi = 3.14159265358979323846;
static float lufsOf(double meanEnergy) { return meanEnergy > 1e-20 ? (float)(-0.691 + 10.0 * std::log10(meanEnergy)) : -200.f; }
static float dbOf(double lin) { return lin > 1e-10 ? (float)(20.0 * std::log10(lin)) : -200.f; }

LoudnessMeter::LoudnessMeter(double sr) : sr_(sr) {
  // K-weighting for any sample rate (BS.1770-4 stage 1 high shelf + stage 2 high-pass, bilinear designs).
  {
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan(kPi * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    Biquad b{(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
             2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
    shelf_ = {b, b};
  }
  {
    const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(kPi * f0 / sr);
    const double a0 = 1.0 + K / Q + K * K;
    Biquad b{1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
    hp_ = {b, b};
  }
  subLen_ = (size_t)std::llround(sr * 0.1);
  subs_.assign(30, 0.0);
  // True peak: 48-tap windowed-sinc low-pass at the original Nyquist, split into 4 phases of 12 taps.
  const int N = kPhases * kTaps;
  for (int p = 0; p < kPhases; ++p)
    for (int k = 0; k < kTaps; ++k) {
      const int n = k * kPhases + p;
      const double x = (n - (N - 1) / 2.0) / kPhases;
      const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
      const double w = 0.42 - 0.5 * std::cos(2 * kPi * (n + 0.5) / N) + 0.08 * std::cos(4 * kPi * (n + 0.5) / N);  // Blackman
      fir_[(size_t)p][(size_t)k] = (float)(sinc * w);
    }
  for (auto& ph : fir_) { double s = 0; for (float c : ph) s += c; for (float& c : ph) c = (float)(c / s); }  // unity gain per phase
  coef_ = 1.0 - std::exp(-1.0 / (0.3 * sr));       // ~300 ms averages
  tpDecay_ = std::exp(-1.0 / (0.3 * sr));
  drLen_ = (size_t)std::llround(3.0 * sr);          // DR meter blocks: 3 s
}

void LoudnessMeter::resetIntegrated() {
  gatedBlocks_.clear(); shortTerms_.clear();
  tpHold_ = 0.f; spHold_ = 0.f;
  sumSq_ = 0; sumN_ = 0; drBlocks_.clear(); drSq_[0] = drSq_[1] = 0; drPeak_[0] = drPeak_[1] = 0; drFill_ = 0;
  overs_ = 0; history_.clear(); subsSinceHistory_ = 0;
}

void LoudnessMeter::process(const float* in, size_t frames, bool accumulate) {
  for (size_t i = 0; i < frames; ++i) {
    const float l = in[2 * i], r = in[2 * i + 1];
    // K-weighted energy (stereo: G = 1 for both channels)
    const double kl = hp_[0].run(shelf_[0].run(l)), kr = hp_[1].run(shelf_[1].run(r));
    subSum_ += kl * kl + kr * kr;
    // true peak
    hist_[0][histPos_] = l; hist_[1][histPos_] = r;
    float tp = 0.f;
    for (int c = 0; c < 2; ++c)
      for (int p = 0; p < kPhases; ++p) {
        float acc = 0.f;
        for (int k = 0; k < kTaps; ++k) acc += fir_[(size_t)p][(size_t)k] * hist_[(size_t)c][(histPos_ + kTaps - (size_t)k) % kTaps];
        tp = std::max(tp, std::fabs(acc));
      }
    tp = std::max(tp, std::max(std::fabs(l), std::fabs(r)));  // never below the sample peak
    histPos_ = (histPos_ + 1) % kTaps;
    tpNow_ = std::max(tp, (float)(tpNow_ * tpDecay_));
    tpHold_ = std::max(tpHold_, tp);
    if (tp >= 1.f) { if (!over_) ++overs_; over_ = true; } else over_ = false;  // count each excursion once
    spHold_ = std::max(spHold_, std::max(std::fabs(l), std::fabs(r)));
    // correlation, RMS, balance
    cLR_ += coef_ * ((double)l * r - cLR_); cLL_ += coef_ * ((double)l * l - cLL_); cRR_ += coef_ * ((double)r * r - cRR_);
    rmsAcc_ += coef_ * (0.5 * ((double)l * l + (double)r * r) - rmsAcc_);
    const double mid = 0.5 * ((double)l + r), side = 0.5 * ((double)l - r);
    midAcc_ += coef_ * (mid * mid - midAcc_); sideAcc_ += coef_ * (side * side - sideAcc_);
    if (accumulate) {  // programme statistics: crest factor and DR blocks
      sumSq_ += 0.5 * ((double)l * l + (double)r * r); ++sumN_;
      drSq_[0] += (double)l * l; drSq_[1] += (double)r * r;
      drPeak_[0] = std::max(drPeak_[0], std::fabs(l)); drPeak_[1] = std::max(drPeak_[1], std::fabs(r));
      if (++drFill_ >= drLen_) {
        DrBlock b{};
        for (int c = 0; c < 2; ++c) { b.peak[c] = drPeak_[c]; b.rms[c] = (float)std::sqrt(2.0 * drSq_[c] / (double)drLen_); drSq_[c] = 0; drPeak_[c] = 0; }
        drBlocks_.push_back(b); drFill_ = 0;
      }
    }

    if (++subFill_ >= subLen_) {  // a 100 ms sub-block is complete
      subs_[subCount_ % 30] = subSum_ / (double)subLen_;
      ++subCount_; subSum_ = 0; subFill_ = 0;
      auto meanOf = [&](size_t n) { double s = 0; for (size_t k = 0; k < n; ++k) s += subs_[(subCount_ - 1 - k) % 30]; return s / (double)n; };
      if (subCount_ >= 4) {
        const double m = meanOf(4);
        momentary_ = lufsOf(m);
        if (accumulate) gatedBlocks_.push_back(m);
      }
      if (subCount_ >= 30) {
        const double s = meanOf(30);
        shortTerm_ = lufsOf(s);
        if (accumulate) {
          shortTerms_.push_back(s);
          if (++subsSinceHistory_ >= 10) {  // once per second
            subsSinceHistory_ = 0;
            history_.push_back(shortTerm_);
            if (history_.size() > 3600) history_.erase(history_.begin());
          }
        }
      }
    }
  }
}

LoudnessReadout LoudnessMeter::read() const {
  LoudnessReadout r;
  r.momentary = momentary_; r.shortTerm = shortTerm_;
  // Integrated: absolute gate -70 LUFS, then relative gate 10 LU below the mean of what passed.
  auto gatedMean = [](const std::vector<double>& v, double absGate, double relOffsetLu, std::vector<double>* kept) {
    double sum = 0; size_t n = 0;
    for (double e : v) if (lufsOf(e) > absGate) { sum += e; ++n; }
    if (!n) return -1.0;
    const double rel = lufsOf(sum / (double)n) + relOffsetLu;
    sum = 0; n = 0;
    for (double e : v) if (lufsOf(e) > absGate && lufsOf(e) > rel) { sum += e; ++n; if (kept) kept->push_back(e); }
    return n ? sum / (double)n : -1.0;
  };
  const double im = gatedMean(gatedBlocks_, -70.0, -10.0, nullptr);
  r.integrated = im > 0 ? lufsOf(im) : -200.f;
  // LRA (EBU Tech 3342): short-term values gated -70 absolute / -20 relative; 95th minus 10th percentile.
  std::vector<double> st;
  if (gatedMean(shortTerms_, -70.0, -20.0, &st) > 0 && st.size() >= 2) {
    std::vector<float> l; l.reserve(st.size()); for (double e : st) l.push_back(lufsOf(e));
    std::sort(l.begin(), l.end());
    auto pct = [&](double p) { return l[(size_t)std::llround(p * (double)(l.size() - 1))]; };
    r.lra = pct(0.95) - pct(0.10);
  }
  r.truePeak = dbOf(tpHold_); r.truePeakNow = dbOf(tpNow_); r.samplePeak = dbOf(spHold_);
  r.rms = rmsAcc_ > 1e-20 ? (float)(10.0 * std::log10(rmsAcc_)) : -200.f;
  r.correlation = (cLL_ > 1e-12 && cRR_ > 1e-12) ? (float)std::clamp(cLR_ / std::sqrt(cLL_ * cRR_), -1.0, 1.0) : 0.f;
  const double sumLR = cLL_ + cRR_;
  r.balance = sumLR > 1e-12 ? (float)std::clamp((std::sqrt(cRR_) - std::sqrt(cLL_)) / (std::sqrt(cRR_) + std::sqrt(cLL_)), -1.0, 1.0) : 0.f;
  // programme statistics
  if (r.truePeak > -100.f && r.integrated > -100.f) r.plr = r.truePeak - r.integrated;
  if (sumN_ > 0 && sumSq_ > 1e-20 && spHold_ > 0.f) r.crest = r.samplePeak - (float)(10.0 * std::log10(sumSq_ / (double)sumN_));
  if (!drBlocks_.empty()) {  // DR meter method per channel, then averaged
    float drSum = 0.f;
    for (int c = 0; c < 2; ++c) {
      std::vector<float> rms, pk;
      for (auto& b : drBlocks_) { rms.push_back(b.rms[c]); pk.push_back(b.peak[c]); }
      std::sort(rms.begin(), rms.end(), std::greater<float>()); std::sort(pk.begin(), pk.end(), std::greater<float>());
      const size_t top = std::max<size_t>(1, rms.size() / 5);
      double sq = 0; for (size_t i = 0; i < top; ++i) sq += (double)rms[i] * rms[i];
      const double rmsTop = std::sqrt(sq / (double)top), peak2 = pk.size() > 1 ? pk[1] : pk[0];
      drSum += (rmsTop > 1e-10 && peak2 > 1e-10) ? (float)(20.0 * std::log10(peak2 / rmsTop)) : 0.f;
    }
    r.dr = drSum / 2.f;
  }
  // stereo image
  r.midDb = midAcc_ > 1e-20 ? (float)(10.0 * std::log10(midAcc_)) : -200.f;
  r.sideDb = sideAcc_ > 1e-20 ? (float)(10.0 * std::log10(sideAcc_)) : -200.f;
  r.width = midAcc_ > 1e-20 ? (float)std::min(999.0, 100.0 * std::sqrt(sideAcc_ / midAcc_)) : 0.f;
  const double avgPow = 0.5 * (cLL_ + cRR_);
  r.monoLoss = avgPow > 1e-20 ? (float)std::max(-60.0, 10.0 * std::log10(std::max(1e-30, midAcc_) / avgPow)) : 0.f;
  r.overs = overs_;
  return r;
}

// ---------------- BandAnalyzer ----------------
const float BandAnalyzer::kCentres[BandAnalyzer::kBands] = {20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
                                                            800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};

BandAnalyzer::BandAnalyzer(double sr, double smoothing) {
  for (int b = 0; b < kBands; ++b) {
    const double f0 = kCentres[b];
    active_[(size_t)b] = f0 < 0.45 * sr;
    if (!active_[(size_t)b]) continue;
    const double w0 = 2 * kPi * f0 / sr, alpha = std::sin(w0) / (2.0 * 2.9), a0 = 1 + alpha;  // RBJ band-pass, 0 dB peak, Q 2.9
    Biquad q{alpha / a0, 0.0, -alpha / a0, -2 * std::cos(w0) / a0, (1 - alpha) / a0};
    f_[(size_t)b] = {q, q};
  }
  coef_ = 1.0 - std::exp(-1.0 / (smoothing * sr));
}

void BandAnalyzer::process(const float* in, size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    const double m = 0.5 * ((double)in[2 * i] + in[2 * i + 1]);
    for (int b = 0; b < kBands; ++b) {
      if (!active_[(size_t)b]) continue;
      auto& f = f_[(size_t)b];
      const double y = f[1].run(f[0].run(m));
      energy_[(size_t)b] += coef_ * (y * y - energy_[(size_t)b]);
    }
  }
}

float BandAnalyzer::bandDb(int b) const {
  const double e = energy_[(size_t)b];
  return (active_[(size_t)b] && e > 1e-20) ? (float)(10.0 * std::log10(2.0 * e)) : -200.f;  // x2: a sine reads its peak level
}

// ---------------- StereoBandMeter ----------------
StereoBandMeter::StereoBandMeter(double sr) {
  auto lp = [&](double f) { const double w0 = 2 * kPi * f / sr, al = std::sin(w0) / (2 * 0.7071), a0 = 1 + al, c = std::cos(w0);
                            return Biquad{(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0}; };
  auto hp = [&](double f) { const double w0 = 2 * kPi * f / sr, al = std::sin(w0) / (2 * 0.7071), a0 = 1 + al, c = std::cos(w0);
                            return Biquad{(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0}; };
  const std::array<std::array<Biquad, 2>, kBands> stages = {{{lp(120), lp(120)}, {hp(120), lp(500)}, {hp(500), lp(4000)}, {hp(4000), hp(4000)}}};
  for (int b = 0; b < kBands; ++b) for (int c = 0; c < 2; ++c) f_[(size_t)b][(size_t)c] = stages[(size_t)b];
  coef_ = 1.0 - std::exp(-1.0 / (0.3 * sr));
}

void StereoBandMeter::process(const float* in, size_t frames) {
  for (size_t i = 0; i < frames; ++i)
    for (int b = 0; b < kBands; ++b) {
      auto& fb = f_[(size_t)b];
      const double l = fb[0][1].run(fb[0][0].run(in[2 * i])), r = fb[1][1].run(fb[1][0].run(in[2 * i + 1]));
      lr_[(size_t)b] += coef_ * (l * r - lr_[(size_t)b]); ll_[(size_t)b] += coef_ * (l * l - ll_[(size_t)b]); rr_[(size_t)b] += coef_ * (r * r - rr_[(size_t)b]);
    }
}

float StereoBandMeter::correlation(int b) const {
  const double ll = ll_[(size_t)b], rr = rr_[(size_t)b];
  return (ll > 1e-14 && rr > 1e-14) ? (float)std::clamp(lr_[(size_t)b] / std::sqrt(ll * rr), -1.0, 1.0) : 0.f;
}

}  // namespace airdaw
