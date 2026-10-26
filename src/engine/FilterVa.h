// (port) FILT -> TYPE MOOG, MS-20, SSM, WASP and 303: five nonlinear zero-delay-feedback
// filters ported from Joep Vanlier's Filther (github.com/JoepVanlier/JSFX, MIT, (c) 2019 Joep
// Vanlier), whose 303 model is Dominique Wurtz's diode ladder (MIT, (c) 2012 Dominique Wurtz);
// see THIRD_PARTY_NOTICES.md. Translated from Filther's EEL2 into the engine:
//
//  * MOOG: Filther's "LP Expensive Moog (ZDF)", a transistor ladder of four tanh stages with
//    resonance fed back from stage 4, every integrator trapezoidal, the loop solved by Newton's
//    method (Filther's evalfancymoog).
//  * MS-20: "LP Kr0g MS-20 NL (ZDF)", the Korg MS-20's two-integrator Sallen-Key loop with
//    atan integrator inputs and a diode-clipped resonance path (eval_MS20_nonlin).
//  * SSM: "LP SSM 2040 NL (ZDF)", four OTA stages with tanh and a tanh-saturated resonance
//    path (eval_ssm_nonlin).
//  * WASP: "LP Waspey Non-Linear (ZDF)", Filther's model of the EDP Wasp's CMOS-inverter
//    state-variable lowpass (eval_wasp_nonlin).
//  * 303: "LP Diode Ladder", Wurtz's TB-303 diode ladder: the linear ladder solved without
//    delay, the signal entering it hard-clipped (eval303).
//
// What is the port's rather than Filther's: the cutoff. Each model takes its integrator gain
// from the engine's cutoff curve (filterUnitTables().cutoffHz through CemTable, prewarped at
// twice the sample rate), set so that the model's linear natural frequency is the cutoff, where
// Filther scales its own frequency axis with per-model factors. Everything runs at twice the
// sample rate (the CEM model's halfband up and down) so the nonlinearities do not fold back,
// with cutoff and resonance smoothed per sample and the filter FM offset delayed with the
// input, as FilterCem does. RES 0..31 is Filther's resonance 0..1. The other differences are
// listed at each model in FilterVa.cpp. Not in the original: type 0 is FilterSQ, untouched.
//
// None of it has been measured against the hardware it is named after; the models are
// Filther's.
#pragma once

#include <cstdint>

#include "FilterCem.h"

namespace sq8l {

class FilterVa {
public:
    enum Model : int { Moog = 0, Ms20 = 1, Ssm = 2, Wasp = 3, Diode303 = 4, NumModels = 5 };

    void init(const CemTable* t, float sr);
    void setSampleRate(float sr);
    // The model this filter runs. Changing it does not clear the state: the master clears it
    // when a voice switches model (Master::controlUpdate), as it does between filter objects.
    void setModel(int m) { model_ = m >= 0 && m < NumModels ? m : Moog; }
    int model() const { return model_; }
    // As FilterCem::setParams: cutoff index 0..255, RES 0..31, `immediate` jumps instead of
    // smoothing (the first control tick of a note).
    void setParams(int32_t cutoff, int32_t reso, bool immediate);
    // Clears the filter (nullptr) or takes another voice's state, as the other types do.
    void copyStateFrom(const FilterVa* other);
    double process(double x) { return processFm(x, 0.0); }
    // As FilterCem::processFm: the cutoff index moved by `fm` steps this sample.
    double processFm(double x, double fm);

    static constexpr int kHalfTaps = FilterCem::kHalfTaps;

private:
    double gAt(double idx) const;  // integrator gain tan(pi fc / 2 sr) of a cutoff index
    // One sample at twice the rate: the model's state advanced, its output returned. g is the
    // integrator gain, q the resonance 0..1.
    double step(double x, double g, double q);
    double stepMoog(double x, double g, double q);
    double stepMs20(double x, double g, double q);
    double stepSsm(double x, double g, double q);
    double stepWasp(double x, double g, double q);
    double step303(double x, double g, double q);

    const CemTable* table_ = nullptr;
    int model_ = Moog;
    double a_ = 0;  // one-pole smoothing factor per sample (FilterSQ's)
    double idx_ = 0, idxTarget_ = 0;
    double q_ = 0, qTarget_ = 0;
    // The model's state at twice the rate: its node voltages after the last step (s_) and the
    // values the trapezoidal rule needs from the step before (d_). What each holds is noted
    // at the model.
    double s_[5] = {}, d_[5] = {};
    // Resampler rings and the FM offsets, as FilterCem's.
    double in_[4 * kHalfTaps] = {};
    double z_[8 * kHalfTaps] = {};
    double fm_[4 * kHalfTaps] = {};
    int inPos_ = 0, zPos_ = 0;
};

}  // namespace sq8l
