#include "FilterCem.h"

#include <cmath>

#include "CrMath.h"
#include "FilterSQ.h"

namespace sq8l {

namespace {

constexpr double kPi = 3.14159265358979323846;
// FilterSQ's smoothing time (FilterSQ::init, 0x3c92ffab), as FilterSvf.
constexpr double kSmoothTime = static_cast<double>(0x1.25ff56p-6f);

// Calibration. A guess until it is measured against a real SQ-80.
//
// Input drive: the DOC sums three oscillators of up to +-1 each (1/127 per sample step), so
// one full-level oscillator is the nominal level. With this drive a sine of amplitude 1 an
// octave below the cutoff (RES 0) comes out with about 1% THD; a quieter input is cleaner,
// all three oscillators at full level audibly dirtier. The output is divided by it again, so
// a small signal passes at unity like the other types.
constexpr double kDrive = 1.35;
// Resonance: the SQ-80 shifts RES 0..31 left by 2 into an 8-bit DAC (5 V full scale), so RES
// 31 is CV_Q = 124/255 * 5 = 2.43 V (Rainer Buchty, sq8l-revival/SQ8L issue #41). The chip
// oscillates from 2.2 V (datasheet minimum; 2.8 V typical, 3.4 V maximum), and r is linear
// in CV_Q as in MAME's esq1_filters, reaching the linear oscillation threshold r = 4 at that
// voltage. So this models a part at the hot end of the spread: RES 29..31 self-oscillate,
// gently, and a typical part would not at all.
constexpr double kCvPerRes = 4.0 * 5.0 / 255.0;
constexpr double kOscillationCv = 2.2;
// Newton's method: at most this many iterations per sample, fewer once the last correction
// is below kNewtonTolerance. The states are of the order of 1 and the convergence quadratic,
// so what is left after a correction of 1e-5 is of the order of 1e-10.
constexpr int kNewtonIterations = 4;
constexpr double kNewtonTolerance = 1e-5;

// tanh by Lambert's continued fraction cut after the x^7 / x^6 terms: within 2e-7 of tanh
// up to |x| = 4.97, where it reaches 1, and clamped to +-1 beyond. Odd, monotonic, bounded,
// and several times cheaper than std::tanh.
inline double tanhApprox(double x) {
    if (x > 4.97) return 1.0;
    if (x < -4.97) return -1.0;
    const double x2 = x * x;
    const double n = x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
    const double d = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
    return n / d;
}

// The halfband filter both resamplers use, a Kaiser-windowed sinc (beta 7: -0.3 dB at 0.41
// of the sample rate, -67 dB from 0.64) with FilterCem::kHalfTaps nonzero taps on each side
// of the centre. Every other tap of a halfband is 0 and the centre is 1/2, so only the odd
// ones are kept: c[j] is the tap at +-(2j + 1), doubled (the upsampler's gain of 2).
struct Halfband {
    double c[FilterCem::kHalfTaps];
    Halfband() {
        constexpr int K = FilterCem::kHalfTaps;
        constexpr double beta = 7.0;
        auto i0 = [](double x) {  // modified Bessel function of the first kind, order 0
            double sum = 1, term = 1;
            for (int k = 1; k < 40; k++) {
                term *= (x / (2 * k)) * (x / (2 * k));
                sum += term;
            }
            return sum;
        };
        double sum = 0;
        for (int j = 0; j < K; j++) {
            const int n = 2 * j + 1;
            const double t = static_cast<double>(n) / (2 * K);
            const double w = i0(beta * std::sqrt(1 - t * t)) / i0(beta);
            c[j] = (j & 1 ? -2.0 : 2.0) / (kPi * n) * w;
            sum += c[j];
        }
        // Exactly unity at DC: the doubled filter's centre is 1, so its odd taps sum to 1/2 on
        // each side.
        for (double& v : c) v *= 0.5 / sum;
    }
};

const Halfband& halfband() {
    static const Halfband h;
    return h;
}

}  // namespace

void CemTable::compute(float sampleRate) {
    const FilterUnitTables& u = filterUnitTables();
    for (int c = 0; c < 256; c++) {
        double w = sampleRate > 0 ? u.cutoffHz[c] / static_cast<double>(sampleRate) : 0.0;
        if (w > 0.49) w = 0.49;
        g[c] = crmath::tan(kPi * w / 2);
    }
}

double FilterCem::resonance(int32_t reso) { return 4.0 * (reso * kCvPerRes) / kOscillationCv; }

void FilterCem::init(const CemTable* t, float sr) {
    table_ = t;
    setSampleRate(sr);
    setParams(0, 0, true);
    copyStateFrom(nullptr);
}

void FilterCem::setSampleRate(float sr) {
    // As FilterSQ::computeSmoothing: 1% left after half the smoothing time.
    a_ = std::pow(0.01, 1.0 / (0.5 * kSmoothTime * sr + 1.0));
}

void FilterCem::setParams(int32_t cutoff, int32_t reso, bool immediate) {
    const int32_t c = cutoff < 1 ? 0 : (cutoff < 255 ? cutoff : 255);
    const int32_t r = reso < 1 ? 0 : (reso < 31 ? reso : 31);
    idxTarget_ = c;
    rTarget_ = resonance(r);
    if (immediate) {
        idx_ = idxTarget_;
        r_ = rTarget_;
    }
}

void FilterCem::copyStateFrom(const FilterCem* o) {
    if (!o) {
        for (int i = 0; i < 4; i++) s_[i] = t_[i] = 0;
        for (double& v : in_) v = 0;
        for (double& v : z_) v = 0;
        for (double& v : fm_) v = 0;
        inPos_ = zPos_ = 0;
        return;
    }
    *this = *o;
}

// One sample at twice the rate: solves the stages for the input x (already driven) and puts
// stage 4 into the downsampler's ring.
//
//   u   = (1 + a r) x - r y4            resonance node: input, feed-forward, feedback
//   y1  = s1 + g tanh(u - y1)           each OTA stage, a trapezoidal integrator whose
//   yi  = si + g tanh(y(i-1) - yi)      input is the OTA's current; then si = 2 yi - si
//
// Newton's method on F(y) = 0 with Fi the difference of the two sides. The Jacobian is
// lower bidiagonal plus the feedback in the corner (row 1, column 4), so a correction is
// one pass: each dyi as Ai + Bi dy4, then dy4 = A4 / (1 - B4). B4 is never positive, so the
// solve never divides by less than 1.
void FilterCem::step(double x, double g, double r) {
    const double in = (1 + kFeedForward * r) * x;
    // Start from the stages' currents of the last sample: y = s + g t.
    double y0 = s_[0] + g * t_[0], y1 = s_[1] + g * t_[1];
    double y2 = s_[2] + g * t_[2], y3 = s_[3] + g * t_[3];
    double t0 = t_[0], t1 = t_[1], t2 = t_[2], t3 = t_[3];
    for (int it = 0; it < kNewtonIterations; it++) {
        t0 = tanhApprox(in - r * y3 - y0);
        t1 = tanhApprox(y0 - y1);
        t2 = tanhApprox(y1 - y2);
        t3 = tanhApprox(y2 - y3);
        const double f0 = y0 - s_[0] - g * t0;
        const double f1 = y1 - s_[1] - g * t1;
        const double f2 = y2 - s_[2] - g * t2;
        const double f3 = y3 - s_[3] - g * t3;
        // dFi/dyi = 1 + g ci, dFi/dy(i-1) = -g ci, dF1/dy4 = g c1 r, ci = tanh' = 1 - ti^2.
        const double c0 = g * (1 - t0 * t0), c1 = g * (1 - t1 * t1);
        const double c2 = g * (1 - t2 * t2), c3 = g * (1 - t3 * t3);
        const double d0 = 1 / (1 + c0), d1 = 1 / (1 + c1), d2 = 1 / (1 + c2), d3 = 1 / (1 + c3);
        const double a0 = -f0 * d0, b0 = -c0 * r * d0;
        const double a1 = (-f1 + c1 * a0) * d1, b1 = c1 * b0 * d1;
        const double a2 = (-f2 + c2 * a1) * d2, b2 = c2 * b1 * d2;
        const double a3 = (-f3 + c3 * a2) * d3, b3 = c3 * b2 * d3;
        const double e3 = a3 / (1 - b3);
        const double e0 = a0 + b0 * e3, e1 = a1 + b1 * e3, e2 = a2 + b2 * e3;
        y0 += e0;
        y1 += e1;
        y2 += e2;
        y3 += e3;
        if (std::fabs(e0) + std::fabs(e1) + std::fabs(e2) + std::fabs(e3) < kNewtonTolerance) break;
    }
    // The currents of the last iteration, from before its correction: that is below the
    // tolerance and moves them by as little.
    t_[0] = t0;
    t_[1] = t1;
    t_[2] = t2;
    t_[3] = t3;
    s_[0] = 2 * y0 - s_[0];
    s_[1] = 2 * y1 - s_[1];
    s_[2] = 2 * y2 - s_[2];
    s_[3] = 2 * y3 - s_[3];
    z_[zPos_] = z_[zPos_ + 4 * kHalfTaps] = y3;
    if (++zPos_ == 4 * kHalfTaps) zPos_ = 0;
}

// g between the two table entries around an index, as FilterSvf: neighbours differ by about
// 3%, so a straight line between them is as good as the tan. The index is clamped to the
// table, whose top is below Nyquist at twice the rate, so no FM amount takes g past it.
double FilterCem::gAt(double idx) const {
    if (idx < 0.0) idx = 0.0;
    if (idx > 255.0) idx = 255.0;
    int i = static_cast<int>(idx);
    if (i > 254) i = 254;
    const double f = idx - i;
    return table_->g[i] + f * (table_->g[i + 1] - table_->g[i]);
}

double FilterCem::processFm(double x, double fm) {
    const double b = 1.0 - a_;
    idx_ = idx_ * a_ + idxTarget_ * b;
    r_ = r_ * a_ + rTarget_ * b;

    // Upsample: the input delayed by kHalfTaps samples, then the point halfway to the next.
    // The FM offset is delayed alongside, so with the carrier's own oscillator as the
    // modulator the two keep their phase (which is the timbre); halfway, the average of its
    // two neighbours. With no FM both steps see the smoothed index, as before.
    constexpr int K = kHalfTaps;
    const Halfband& h = halfband();
    in_[inPos_] = in_[inPos_ + 2 * K] = x * kDrive;
    fm_[inPos_] = fm_[inPos_ + 2 * K] = fm;
    const double* in = in_ + inPos_ + 2 * K;  // in[-age]
    const double* m = fm_ + inPos_ + 2 * K;   // m[-age]
    double mid = 0;
    for (int j = 0; j < K; j++) mid += h.c[j] * (in[-(K - 1 - j)] + in[-(K + j)]);
    const double now = in[-K];
    const double fmNow = m[-K], fmMid = 0.5 * (m[-K] + m[-(K - 1)]);
    if (++inPos_ == 2 * K) inPos_ = 0;
    step(now, gAt(idx_ + fmNow), r_);
    step(mid, gAt(idx_ + fmMid), r_);

    // Downsample: the halfband centred 2K - 1 samples back, its centre tap 1/2 and the others
    // the doubled taps halved again.
    const double* z = z_ + zPos_ + 4 * K - 1;  // z[-age]
    double y = 0.5 * z[-(2 * K - 1)];
    for (int j = 0; j < K; j++) y += 0.5 * h.c[j] * (z[-(2 * K - 2 - 2 * j)] + z[-(2 * K + 2 * j)]);
    return y / kDrive;
}

}  // namespace sq8l
