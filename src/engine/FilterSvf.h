// (port) The filter types other than the original's (FILT -> TYPE, ofs::Filter::Type): a
// linear trapezoidal state variable filter after Andy Simper, "Linear Trap Integrated State
// Variable Filter" (Cytomic technical paper SvfLinearTrapOptimised2), written from the
// published equations. Not in the original: type 0 is FilterSQ, untouched.
//
// It takes what FilterSQ takes: the cutoff index 0..255 (the same filterUnitTables().cutoffHz
// frequencies, with keyboard tracking and the FREQ modulation already folded in by the
// master) and the resonance 0..31. The cutoff is smoothed as an index, i.e. in log
// frequency, so a modulation step sweeps instead of clicking, over the same time FilterSQ
// smooths its coefficients in.
#pragma once

#include <cstdint>

namespace sq8l {

// The program's filter types (ofs::Filter::Type). The values are stored in programs: append
// only.
namespace FilterType {
constexpr int Sq80 = 0;   // the original 4-pole lowpass (FilterSQ)
constexpr int Lp2 = 1;    // 2-pole lowpass, 12 dB/oct
constexpr int Bp2 = 2;    // 2-pole bandpass, 0 dB at the cutoff
constexpr int Hp2 = 3;    // 2-pole highpass, 12 dB/oct
constexpr int Notch = 4;  // 2-pole band reject
constexpr int Lp4 = 5;    // 4-pole lowpass, 24 dB/oct: two cascaded SVFs
constexpr int Count = 6;
}  // namespace FilterType

// Per sample rate: the integrator gain g = tan(pi fc / sr) of every cutoff index, with fc
// clamped below Nyquist as FilterSQ clamps it (0.49 sr). Rebuilt on a sample rate change.
struct SvfTable {
    double g[256];
    void compute(float sampleRate);
};

class FilterSvf {
public:
    void init(const SvfTable* t, float sr);
    void setSampleRate(float sr);
    void setType(int t) { type_ = t; }
    int type() const { return type_; }
    // As FilterSQ::setParams: clamped to 0..255 and 0..31; `immediate` jumps instead of
    // smoothing (the first control tick of a note).
    void setParams(int32_t cutoff, int32_t reso, bool immediate);
    // Clears the integrators (nullptr) or takes another voice's state, as FilterSQ does for a
    // new note and a legato steal.
    void copyStateFrom(const FilterSvf* other);
    double process(double x);

private:
    const SvfTable* table_ = nullptr;
    int type_ = FilterType::Lp2;
    double a_ = 0;  // one-pole smoothing factor per sample (FilterSQ's)
    // Smoothed values and their targets: cutoff index, damping k = 1/Q of the (last) stage,
    // output gain.
    double idx_ = 0, idxTarget_ = 0;
    double k_ = 2, kTarget_ = 2;
    double gain_ = 1, gainTarget_ = 1;
    // Integrator states (ic1eq, ic2eq) of the two stages; the second is only used by Lp4.
    double s1_[2] = {}, s2_[2] = {};
};

}  // namespace sq8l
