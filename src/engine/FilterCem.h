// (port) FILT -> TYPE "CEM": a nonlinear model of the SQ-80's Curtis CEM3379 4-pole lowpass,
// written for this port from published descriptions only:
//  * the structure: four OTA integrator stages, each a tanh of its input minus its own
//    output (Huovilainen, "Non-linear digital implementation of the Moog ladder filter",
//    DAFx-04, with the OTA cascade's one tanh per stage), resonance fed back from stage 4,
//    and the input fed forward to the resonance node (CEM3379/CEM3320 datasheets), which
//    makes the passband drop by about 6 dB at the edge of oscillation instead of 14;
//  * the discretisation: zero-delay feedback, every integrator trapezoidal (TPT, Zavalishin,
//    "The Art of VA Filter Design"), the implicit loop solved by Newton's method per sample;
//  * the linear limit, (1 + a r) / ((1 + s/wc)^4 + r), and the resonance control law, r
//    linear in CV_Q, as MAME's esq1_filters (BSD-3-Clause) has them.
// Not in the original: type 0 is FilterSQ, untouched.
//
// It takes what the other types take: the cutoff index 0..255 looked up in
// filterUnitTables().cutoffHz (the original's measured curve, not the chip's exponential
// law) and RES 0..31, smoothed per sample as FilterSvf smooths them. It runs at twice the
// sample rate (halfband up and down) so the saturation does not fold back.
//
// The calibration (input drive, feed-forward share, where it starts to self-oscillate) is a
// guess: nothing here has been measured against a real SQ-80. The constants are in
// FilterCem.cpp.
#pragma once

#include <cstdint>

namespace sq8l {

// Per sample rate: the integrator gain g = tan(pi fc / (2 sr)) of every cutoff index at
// twice the sample rate, fc clamped to 0.49 sr like the other types. Rebuilt on a sample
// rate change.
struct CemTable {
    double g[256];
    void compute(float sampleRate);
};

class FilterCem {
public:
    void init(const CemTable* t, float sr);
    void setSampleRate(float sr);
    // As FilterSvf::setParams: clamped to 0..255 and 0..31; `immediate` jumps instead of
    // smoothing (the first control tick of a note).
    void setParams(int32_t cutoff, int32_t reso, bool immediate);
    // Clears the filter (nullptr) or takes another voice's state, as FilterSQ does for a new
    // note and a legato steal.
    void copyStateFrom(const FilterCem* other);
    double process(double x);

    // The resonance gain r of a RES value 0..31 (4 = the linear oscillation threshold), and
    // the share of the input fed forward to the resonance node: a small signal sees
    // (1 + kFeedForward r) / ((1 + s/wc)^4 + r), so the passband is half (-6 dB) at r = 4,
    // where it would be a fifth (-14 dB) without. A guess like the rest of the calibration.
    static double resonance(int32_t reso);
    static constexpr double kFeedForward = 0.375;

    // The halfband's taps on each side (half the up- or downsampler's polyphase length).
    static constexpr int kHalfTaps = 8;

private:
    void step(double x, double g, double r);  // one sample at twice the rate into z_

    const CemTable* table_ = nullptr;
    double a_ = 0;  // one-pole smoothing factor per sample (FilterSQ's)
    // Smoothed values and their targets: cutoff index, resonance gain r (4 = the linear
    // oscillation threshold).
    double idx_ = 0, idxTarget_ = 0;
    double r_ = 0, rTarget_ = 0;
    // The four integrators: TPT states, and the OTA currents (tanh) of the last sample,
    // Newton's starting point.
    double s_[4] = {}, t_[4] = {};
    // Input history at the sample rate (upsampler), output history at twice the rate
    // (downsampler): rings of 2K and 4K samples, each written twice (at *Pos_ and one length
    // on) so the last length of samples always lies in one piece.
    double in_[4 * kHalfTaps] = {};
    double z_[8 * kHalfTaps] = {};
    int inPos_ = 0, zPos_ = 0;
};

}  // namespace sq8l
