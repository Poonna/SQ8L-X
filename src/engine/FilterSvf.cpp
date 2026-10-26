#include "FilterSvf.h"

#include <cmath>

#include "CrMath.h"
#include "FilterSQ.h"
#include "Program.h"

namespace sq8l {

static_assert(kNumFilterTypes == FilterType::Count, "programFilterType must know every type");

namespace {

constexpr double kPi = 3.14159265358979323846;
// FilterSQ's smoothing time (FilterSQ::init, 0x3c92ffab), so both types follow modulation
// alike.
constexpr double kSmoothTime = static_cast<double>(0x1.25ff56p-6f);
// Resonance: Q rises exponentially from the stage's Butterworth value at RES 0 to kMaxQ at
// RES 31. k = 1/Q never reaches 0, so the filter cannot self-oscillate or blow up.
constexpr double kMaxQ = 16.0;
constexpr double kQ2 = 0.70710678118654752;  // 2-pole Butterworth
// 4-pole Butterworth as two 2-pole stages: Q = 1 / (2 cos(pi/8)) and 1 / (2 cos(3 pi/8)).
// The resonance goes on the second.
constexpr double kQ4a = 0.54119610014619698;
constexpr double kQ4b = 1.30656296487637652;

double damping(double q0, int32_t reso) { return 1.0 / (q0 * std::pow(kMaxQ / q0, reso / 31.0)); }

}  // namespace

void SvfTable::compute(float sampleRate) {
    const FilterUnitTables& u = filterUnitTables();
    for (int c = 0; c < 256; c++) {
        double w = sampleRate > 0 ? u.cutoffHz[c] / static_cast<double>(sampleRate) : 0.0;
        if (w > 0.49) w = 0.49;
        g[c] = crmath::tan(kPi * w);
    }
}

void FilterSvf::init(const SvfTable* t, float sr) {
    table_ = t;
    setSampleRate(sr);
    setParams(0, 0, true);
    copyStateFrom(nullptr);
}

void FilterSvf::setSampleRate(float sr) {
    // As FilterSQ::computeSmoothing: 1% left after half the smoothing time.
    a_ = std::pow(0.01, 1.0 / (0.5 * kSmoothTime * sr + 1.0));
}

void FilterSvf::setParams(int32_t cutoff, int32_t reso, bool immediate) {
    const int32_t c = cutoff < 1 ? 0 : (cutoff < 255 ? cutoff : 255);
    const int32_t r = reso < 1 ? 0 : (reso < 31 ? reso : 31);
    idxTarget_ = c;
    kTarget_ = damping(type_ == FilterType::Lp4 ? kQ4b : kQ2, r);
    // Bandpass and notch never rise above 0 dB; the others peak by about Q at the cutoff and
    // take the original's resonance gain compensation.
    const bool peaks = type_ == FilterType::Lp2 || type_ == FilterType::Hp2 || type_ == FilterType::Lp4;
    gainTarget_ = peaks ? filterUnitTables().resoGain[r] : 1.0;
    if (immediate) {
        idx_ = idxTarget_;
        k_ = kTarget_;
        gain_ = gainTarget_;
    }
}

void FilterSvf::copyStateFrom(const FilterSvf* o) {
    if (!o) {
        for (int i = 0; i < 2; i++) s1_[i] = s2_[i] = 0;
        return;
    }
    idx_ = o->idx_;
    k_ = o->k_;
    gain_ = o->gain_;
    for (int i = 0; i < 2; i++) {
        s1_[i] = o->s1_[i];
        s2_[i] = o->s2_[i];
    }
}

double FilterSvf::process(double x) {
    const double b = 1.0 - a_;
    idx_ = idx_ * a_ + idxTarget_ * b;
    k_ = k_ * a_ + kTarget_ * b;
    gain_ = gain_ * a_ + gainTarget_ * b;

    // g between the two table entries around the smoothed index: neighbours differ by about
    // 3%, so a straight line between them is as good as the tan itself.
    int i = static_cast<int>(idx_);
    if (i < 0) i = 0;
    if (i > 254) i = 254;
    const double f = idx_ - i;
    const double g = table_->g[i] + f * (table_->g[i + 1] - table_->g[i]);

    // One SvfLinearTrapOptimised2 tick: v1 band, v2 low.
    double v1, v2;
    auto tick = [g](double v0, double k, double& ic1, double& ic2, double& band, double& low) {
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const double v3 = v0 - ic2;
        band = a1 * ic1 + a2 * v3;
        low = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * band - ic1;
        ic2 = 2.0 * low - ic2;
    };

    double y;
    switch (type_) {
    case FilterType::Bp2:
        tick(x, k_, s1_[0], s2_[0], v1, v2);
        y = k_ * v1;
        break;
    case FilterType::Hp2:
        tick(x, k_, s1_[0], s2_[0], v1, v2);
        y = x - k_ * v1 - v2;
        break;
    case FilterType::Notch:
        tick(x, k_, s1_[0], s2_[0], v1, v2);
        y = x - k_ * v1;
        break;
    case FilterType::Lp4:
        tick(x, 1.0 / kQ4a, s1_[0], s2_[0], v1, v2);
        tick(v2, k_, s1_[1], s2_[1], v1, v2);
        y = v2;
        break;
    default:  // Lp2
        tick(x, k_, s1_[0], s2_[0], v1, v2);
        y = v2;
        break;
    }
    return y * gain_;
}

}  // namespace sq8l
