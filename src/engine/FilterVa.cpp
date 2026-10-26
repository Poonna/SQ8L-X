#include "FilterVa.h"

#include <algorithm>
#include <cmath>

namespace sq8l {

namespace {

// FilterSQ's smoothing time (FilterSQ::init, 0x3c92ffab), as FilterSvf and FilterCem.
constexpr double kSmoothTime = static_cast<double>(0x1.25ff56p-6f);
// Newton iterations per step: Filther runs a fixed 3 for every model, from the last state.
constexpr int kIterations = 3;

// tanh as FilterCem has it (Lambert's continued fraction to x^7/x^6, within 2e-7 up to |x| =
// 4.97, clamped beyond): Filther's actual_tanh, which is exact, at a fraction of the cost.
inline double tanhA(double x) {
    if (x > 4.97) return 1.0;
    if (x < -4.97) return -1.0;
    const double x2 = x * x;
    const double n = x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
    const double d = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
    return n / d;
}

// Filther's own helpers, as the Wasp model uses them.
// tanh(s) in Filther is 0.25 tanh(4 s): slope 1 at 0, saturating at +-0.25.
inline double filtherTanh(double s) { return 0.25 * tanhA(4.0 * s); }
// fasttanh: x / (1 + |2x|), saturating at +-0.5.
inline double fastTanh(double x) { return x / (1.0 + std::fabs(2.0 * x)); }
// clamp: linear (slope 1) within +-1.25, slope 0.3 beyond.
inline double waspClamp(double x) {
    x *= 0.8;
    const double y = 0.5 * (std::fabs(x + 1.0) - std::fabs(x - 1.0));
    return 1.25 * (y + 0.3 * (x - y));
}

// A model whose resonance does not peak at its integrators' frequency is tuned by scaling the
// prewarped angle: g = tan(w / 2) becomes tan(f w / 2), held below Nyquist.
inline double retune(double g, double f) { return std::tan(std::min(std::atan(g) * f, 1.55)); }
// The factors, measured with render_check --filter-types (RES 28 small-signal peak at the
// cutoff): the diode ladder's coupled stages put its peak at 0.70 of wc's frequency, the
// Wasp's damping at 0.92.
constexpr double kTune303 = 1.0 / 0.70;
constexpr double kTuneWasp = 1.0 / 0.92;

}  // namespace

void FilterVa::init(const CemTable* t, float sr) {
    table_ = t;
    setSampleRate(sr);
    setParams(0, 0, true);
    copyStateFrom(nullptr);
}

void FilterVa::setSampleRate(float sr) { a_ = std::pow(0.01, 1.0 / (0.5 * kSmoothTime * sr + 1.0)); }

void FilterVa::setParams(int32_t cutoff, int32_t reso, bool immediate) {
    const int32_t c = cutoff < 1 ? 0 : (cutoff < 255 ? cutoff : 255);
    const int32_t r = reso < 1 ? 0 : (reso < 31 ? reso : 31);
    idxTarget_ = c;
    qTarget_ = r / 31.0;
    if (immediate) {
        idx_ = idxTarget_;
        q_ = qTarget_;
    }
}

void FilterVa::copyStateFrom(const FilterVa* o) {
    if (!o) {
        for (double& v : s_) v = 0;
        for (double& v : d_) v = 0;
        for (double& v : in_) v = 0;
        for (double& v : z_) v = 0;
        for (double& v : fm_) v = 0;
        inPos_ = zPos_ = 0;
        return;
    }
    const int m = model_;
    *this = *o;
    model_ = m;  // the model is the program's, not the stolen voice's (the master sets both)
}

double FilterVa::gAt(double idx) const {
    if (idx < 0.0) idx = 0.0;
    if (idx > 255.0) idx = 255.0;
    int i = static_cast<int>(idx);
    if (i > 254) i = 254;
    const double f = idx - i;
    return table_->g[i] + f * (table_->g[i + 1] - table_->g[i]);
}

double FilterVa::processFm(double x, double fm) {
    const double b = 1.0 - a_;
    idx_ = idx_ * a_ + idxTarget_ * b;
    q_ = q_ * a_ + qTarget_ * b;

    // Up: the input delayed by kHalfTaps samples and the point halfway to the next, the FM
    // offset delayed alongside (FilterCem::processFm).
    constexpr int K = kHalfTaps;
    const double* c = FilterCem::halfbandTaps();
    in_[inPos_] = in_[inPos_ + 2 * K] = x;
    fm_[inPos_] = fm_[inPos_ + 2 * K] = fm;
    const double* in = in_ + inPos_ + 2 * K;
    const double* m = fm_ + inPos_ + 2 * K;
    double mid = 0;
    for (int j = 0; j < K; j++) mid += c[j] * (in[-(K - 1 - j)] + in[-(K + j)]);
    const double now = in[-K];
    const double fmNow = m[-K], fmMid = 0.5 * (m[-K] + m[-(K - 1)]);
    if (++inPos_ == 2 * K) inPos_ = 0;
    for (int half = 0; half < 2; half++) {
        double v = half == 0 ? step(now, gAt(idx_ + fmNow), q_) : step(mid, gAt(idx_ + fmMid), q_);
        if (!std::isfinite(v)) {  // a model gone astray starts again from rest, silent
            for (double& s : s_) s = 0;
            for (double& s : d_) s = 0;
            v = 0.0;
        }
        z_[zPos_] = z_[zPos_ + 4 * K] = v;
        if (++zPos_ == 4 * K) zPos_ = 0;
    }

    // Down: the halfband centred 2K - 1 samples back.
    const double* z = z_ + zPos_ + 4 * K - 1;
    double y = 0.5 * z[-(2 * K - 1)];
    for (int j = 0; j < K; j++) y += 0.5 * c[j] * (z[-(2 * K - 2 - 2 * j)] + z[-(2 * K + 2 * j)]);
    return y;
}

double FilterVa::step(double x, double g, double q) {
    switch (model_) {
    case Ms20: return stepMs20(x, g, q);
    case Ssm: return stepSsm(x, g, q);
    case Wasp: return stepWasp(x, g, q);
    case Diode303: return step303(x, g, q);
    default: return stepMoog(x, g, q);
    }
}

// MOOG (Filther's evalfancymoog). s_[0..3] the stage outputs y1..y4 (Newton's starting point),
// d_[0..4] the input and stage outputs of the step before. The signal is scaled by -40 into
// the ladder and by 1/40 out, as Filther feeds it; with the thermal voltage below that puts
// tanh(0.52 x) at the first stage, so a full-level oscillator drives it gently. Linear limit:
// four one-poles of gain g = wcT hA, which the port sets to the table's g.
double FilterVa::stepMoog(double x, double g, double q) {
    constexpr double kVt = 26.0 / 1000.0;
    constexpr double kHA = 0.25 * kVt;  // hA = A / 2, A = VT / 2
    const double wcT = g / kHA;
    const double k = 1.1 * q * 4.0;  // 0..4.4
    const double y0 = -40.0 * x;
    double y1 = s_[0], y2 = s_[1], y3 = s_[2], y4 = s_[3];
    const double d0 = d_[0], d1 = d_[1], d2 = d_[2], d3 = d_[3], d4 = d_[4];
    for (int it = 0; it < kIterations; it++) {
        const double ws0 = tanhA(kHA * (y0 + d0 + k * (y4 + d4)));
        const double ws1 = tanhA(kHA * (y1 + d1));
        const double ws2 = tanhA(kHA * (y2 + d2));
        const double ws3 = tanhA(kHA * (y3 + d3));
        const double ws4 = tanhA(kHA * (y4 + d4));
        const double gJ0 = g * (1 - ws0 * ws0), gJ1 = g * (1 - ws1 * ws1), gJ2 = g * (1 - ws2 * ws2);
        const double gJ3 = g * (1 - ws3 * ws3), gJ4 = g * (1 - ws4 * ws4);
        const double f1 = y1 - d1 + wcT * (ws1 + ws0);
        const double f2 = y2 - d2 - wcT * (ws1 - ws2);
        const double f3 = y3 - d3 - wcT * (ws2 - ws3);
        const double f4 = y4 - d4 - wcT * (ws3 - ws4);
        // The inverse Jacobian, as Filther writes it out.
        const double t2 = gJ2 * gJ3, t3 = gJ2 * gJ4, t4 = gJ3 * gJ4, t5 = gJ2 * gJ3 * gJ4;
        const double t6 = gJ1 * gJ2, t7 = gJ1 * gJ3, t8 = gJ1 * gJ4, t9 = gJ1 * gJ2 * gJ3;
        const double t10 = gJ1 * gJ2 * gJ4, t11 = gJ1 * gJ3 * gJ4, t12 = gJ1 * gJ2 * gJ3 * gJ4;
        const double t13 = gJ0 * gJ1 * gJ2 * gJ3 * k;
        const double t15 = 1.0 / (gJ1 + gJ2 + gJ3 + gJ4 + t2 + t3 + t4 + t5 + t6 + t7 + t8 + t9 + t10 + t11 + t12 + t13 + 1.0);
        const double t16 = gJ2 + 1.0, t17 = gJ3 + 1.0, t18 = gJ1 + 1.0;
        const double a00 = t15 * (gJ2 + gJ3 + gJ4 + t2 + t3 + t4 + t5 + 1.0);
        const double a01 = -gJ0 * gJ2 * gJ3 * k * t15;
        const double a02 = -gJ0 * gJ3 * k * t15 * t16;
        const double a03 = -gJ0 * k * t15 * t16 * t17;
        const double a10 = t15 * (gJ1 + t7 + t8 + t11);
        const double a11 = t15 * (gJ1 + gJ3 + gJ4 + t4 + t7 + t8 + t11 + 1.0);
        const double a12 = -gJ0 * gJ1 * gJ3 * k * t15;
        const double a13 = -gJ0 * gJ1 * k * t15 * t17;
        const double a20 = t15 * (t6 + t10);
        const double a21 = t15 * (gJ2 + t3 + t6 + t10);
        const double a22 = t15 * (gJ1 + gJ2 + gJ4 + t3 + t6 + t8 + t10 + 1.0);
        const double a23 = -gJ0 * gJ1 * gJ2 * k * t15;
        const double a30 = gJ1 * gJ2 * gJ3 * t15;
        const double a31 = gJ2 * gJ3 * t15 * t18;
        const double a32 = gJ3 * t15 * t16 * t18;
        const double a33 = t15 * t16 * t17 * t18;
        y1 -= a00 * f1 + a01 * f2 + a02 * f3 + a03 * f4;
        y2 -= a10 * f1 + a11 * f2 + a12 * f3 + a13 * f4;
        y3 -= a20 * f1 + a21 * f2 + a22 * f3 + a23 * f4;
        y4 -= a30 * f1 + a31 * f2 + a32 * f3 + a33 * f4;
    }
    s_[0] = y1, s_[1] = y2, s_[2] = y3, s_[3] = y4;
    d_[0] = y0, d_[1] = y1, d_[2] = y2, d_[3] = y3, d_[4] = y4;
    return y4 / 40.0;
}

// MS-20 (Filther's eval_MS20_nonlin). s_[0..1] y1, y2; d_[0..1] their values the step before,
// d_[2] the input the step before. Linear limit: s^2 + (2 - k) s + 1 over the integrators'
// rate, so the natural frequency is the cutoff with hh = g, and k = 2 (RES 31) is the edge of
// self-oscillation, which the clipped resonance path bounds. One fix: Filther's first atan
// term, the trapezoidal rule's previous half, takes the current input; here it takes the
// previous one.
double FilterVa::stepMs20(double x, double g, double q) {
    const double hh = g, h = 2.0 * g;
    const double k = 2.0 * q;
    const double hk = h * k;
    double y1 = s_[0], y2 = s_[1];
    const double d1 = d_[0], d2 = d_[1], xPrev = d_[2];
    const double gkd2 = std::clamp(k * d2, -1.0, 1.0);
    const double atan1 = std::atan(d1 - xPrev + gkd2);
    const double atan2 = std::atan(d1 - d2 + gkd2);
    for (int it = 0; it < kIterations; it++) {
        double gky2 = k * y2;
        const double dgky2 = std::fabs(gky2) > 1.0 ? 0.0 : 1.0;
        gky2 = std::clamp(gky2, -1.0, 1.0);
        const double sig1 = y1 - y2 + gky2;
        const double sig2 = y1 - x + gky2;
        const double f1 = y1 - d1 + hh * (atan1 + std::atan(sig2));
        const double f2 = y2 - d2 - hh * (atan2 + std::atan(sig1));
        const double sub3i = 1.0 / (2.0 * (sig2 * sig2 + 1.0));
        const double sub5 = 1.0 / (2.0 * (sig1 * sig1 + 1.0));
        const double a = h * sub3i + 1.0;
        const double b = hk * dgky2 * sub3i;
        const double c = -h * sub5;
        const double d = 1.0 - (hk * dgky2 - h) * sub5;
        const double norm = 1.0 / (a * d - b * c);
        y1 -= (d * f1 - b * f2) * norm;
        y2 -= (a * f2 - c * f1) * norm;
    }
    s_[0] = y1, s_[1] = y2;
    d_[0] = y1, d_[1] = y2, d_[2] = x;
    return y2;
}

// SSM (Filther's eval_ssm_nonlin). s_[0..3] U0..U3; d_[0..3] D0..D3 the step before, d_[4] the
// input the step before. Four inverting OTA stages, so the output is in phase. Linear limit:
// four one-poles of gain hh itau, which the port sets to the table's g (Filther:
// 1.9 * 0.96 * tan of its own axis).
double FilterVa::stepSsm(double x, double g, double q) {
    const double hhitau = g;
    const double k2 = 5.0 * q;
    double u0 = s_[0], u1 = s_[1], u2 = s_[2], u3 = s_[3];
    const double D0 = d_[0], D1 = d_[1], D2 = d_[2], D3 = d_[3], Di = d_[4];
    const double dd01 = tanhA(D0 + D1), dd12 = tanhA(D1 + D2), dd23 = tanhA(D2 + D3);
    const double dd0i = tanhA(D0 + Di - tanhA(k2 * D3));
    for (int it = 0; it < kIterations; it++) {
        const double du01 = tanhA(u0 + u1), du12 = tanhA(u1 + u2), du23 = tanhA(u2 + u3);
        const double fb = tanhA(k2 * u3);
        const double du0i = tanhA(u0 + x - fb);
        const double dd0i_ = 1 - du0i * du0i, d01 = 1 - du01 * du01, d12 = 1 - du12 * du12;
        const double d23 = 1 - du23 * du23, dfb = 1 - fb * fb;
        const double f0 = -D0 + u0 + hhitau * (dd0i + du0i);
        const double f1 = -D1 + u1 + hhitau * (dd01 + du01);
        const double f2 = -D2 + u2 + hhitau * (dd12 + du12);
        const double f3 = -D3 + u3 + hhitau * (dd23 + du23);
        const double a = hhitau * dd0i_ + 1;
        const double b = -k2 * hhitau * dfb * dd0i_;
        const double c = hhitau * d01;
        const double d = c + 1;
        const double e = hhitau * d12;
        const double f = e + 1;
        const double gg = hhitau * d23;
        const double h = gg + 1;
        const double idet = 1.0 / (a * d * f * h - b * c * e * gg);
        const double n0 = u0 - (-b * d * f * f3 + b * d * f2 * gg - b * e * f1 * gg + d * f * f0 * h) * idet;
        const double n1 = u1 - (a * f * f1 * h + b * c * f * f3 - b * c * f2 * gg - c * f * f0 * h) * idet;
        const double n2 = u2 - (a * d * f2 * h - a * e * f1 * h - b * c * e * f3 + c * e * f0 * h) * idet;
        const double n3 = u3 - (a * d * f * f3 - a * d * f2 * gg + a * e * f1 * gg - c * e * f0 * gg) * idet;
        u0 = n0, u1 = n1, u2 = n2, u3 = n3;
    }
    s_[0] = u0, s_[1] = u1, s_[2] = u2, s_[3] = u3;
    d_[0] = u0, d_[1] = u1, d_[2] = u2, d_[3] = u3, d_[4] = x;
    return u3;
}

// WASP (Filther's eval_wasp_nonlin). d_[0..1] V1, V2 after the step before (also Newton's
// starting point, where Filther starts from variables it never sets), d_[2] the input the step
// before. Linear limit, with nl the inverters' gain: V2 / Vi = 1 / nl over
// s^2 + a r nl^2 s + a^2 nl^3, a the integrators' rate, so the natural frequency a nl^1.5 is the
// cutoff with hah = g / nl^1.5 (tuned, see kTuneWasp), and the output is V2 * nl (Filther: -V2 at 1 / nl, with its
// cutoff about a third of its own axis). The damping r runs from 0.8 (RES 0, Q about 2) to
// 0.2 (RES 31, Q about 8): a Wasp rings but does not self-oscillate.
double FilterVa::stepWasp(double x, double g, double q) {
    constexpr double nl = 0.35, offset = 0.01;
    static const double kNl15 = std::pow(nl, 1.5);
    const double r = 0.8 - 0.6 * q;
    const double hah = retune(g, kTuneWasp) / kNl15;
    const double hahnl = hah * nl;
    const double vi = waspClamp(x);
    const double D1 = d_[0], D2 = d_[1], Di = d_[2];
    double v1 = D1, v2 = D2;
    double tv1nl = fastTanh(v1 * nl + offset);
    double tv2nl = fastTanh(v2 * nl + offset);
    const double td1nl = fastTanh(D1 * nl + offset);
    const double td2nl = fastTanh(D2 * nl + offset);
    const double ttd1nl = filtherTanh(td1nl);
    const double fixterm = fastTanh(nl * (Di + waspClamp(-r * td1nl) - td2nl));
    for (int it = 0; it < kIterations; it++) {
        const double resclamp = waspClamp(-r * tv1nl);
        const double clterm = nl * (vi + resclamp - tv2nl);
        const double f0 = -D1 + v1 - hah * (fixterm + clterm);
        const double f1 = -D2 + v2 - hah * (ttd1nl + tv1nl);
        const double shrterm = hahnl * nl * (1 - clterm * clterm) * (1 - clterm * clterm);
        const double jb = shrterm * (1 - tv2nl * tv2nl);
        const double ja = r * shrterm * (1 - tv1nl * tv1nl) + 1;
        const double jc = -hahnl * (1 - tv1nl * tv1nl) * (1 - tv1nl * tv1nl);
        const double idet = 1.0 / (ja - jb * jc);
        v1 -= (-jb * f1 + f0) * idet;
        v2 -= (ja * f1 - jc * f0) * idet;
        tv1nl = fastTanh(v1 * nl);
        tv2nl = filtherTanh(v2 * nl);
    }
    // Filther: "Forced to do this to keep things stable".
    v1 = waspClamp(v1);
    v2 = waspClamp(v2);
    d_[0] = v1, d_[1] = v2, d_[2] = vi;
    return v2 * nl;
}

// 303 (Wurtz's diode ladder, Filther's eval303). s_[0..3] the capacitor states z0..z3. The
// linear ladder is solved for its output without delay, then the signal entering it is
// hard-clipped to +-1 (the TB-303's character: a full-level oscillator already clips). wc is a
// quarter of Filther's angular frequency, which the port takes as g / 2, its prewarped
// equivalent, tuned so the resonance peaks at the cutoff (kTune303); at RES 0 the ladder's
// spread poles still make it much darker than the other types at the same CUT, as a 303 is. Output A y4 with A = 1 + k / 2, Wurtz's passband compensation. One fix: Filther's
// reset leaves z0 as it was; copyStateFrom(nullptr) clears it.
double FilterVa::step303(double x, double g, double q) {
    const double wc = 0.5 * retune(g, kTune303);
    const double wc2 = wc * wc, wc3 = wc2 * wc, wc4 = wc3 * wc;
    const double b = 1.0 / (1.0 + 8.0 * wc + 20.0 * wc2 + 16.0 * wc3 + 2.0 * wc4);
    const double gg = 2.0 * wc4 * b;
    const double k = 20.0 * q;
    const double A = 1.0 + 0.5 * k;
    const double dwc = 2 * wc, dwc2 = 2 * wc2, qwc2 = 4 * wc2, dwc3 = 2 * wc3, qwc3 = 4 * wc3;
    const double b0 = dwc + 12 * wc2 + 20 * wc3 + 8 * wc4;
    const double a0 = 1 + 6 * wc + 10 * wc2 + qwc3;
    const double a1 = dwc + 8 * wc2 + 6 * wc3;
    const double a2 = dwc2 + wc3;
    const double a3 = dwc3;
    const double b10 = dwc2 + 8 * wc3 + 6 * wc4;
    const double a10 = wc + 4 * wc2 + 3 * wc3;
    const double a11 = 1 + 6 * wc + 11 * wc2 + 6 * wc3;
    const double a12 = wc + qwc2 + qwc3;
    const double a13 = wc2 + dwc3;
    const double b20 = dwc3 + 4 * wc4;
    const double a20 = a13;
    const double a21 = wc + qwc2 + 4 * wc3;
    const double a22 = 1 + 6 * wc + 10 * wc2 + qwc3;
    const double a23 = wc + qwc2 + dwc3;
    const double c2 = a21 - a3;
    const double c3 = 1 + 6 * wc + 9 * wc2 + dwc3;
    double& z0 = s_[0];
    double& z1 = s_[1];
    double& z2 = s_[2];
    double& z3 = s_[3];
    const double s = (z0 * wc3 + z1 * a20 + z2 * c2 + z3 * c3) * b;
    double y4 = (gg * x + s) / (1 + gg * k);
    const double y0 = std::clamp(x - k * y4, -1.0, 1.0);
    const double y1 = b * (y0 * b0 + z0 * a0 + z1 * a1 + z2 * a2 + z3 * a3);
    const double y2 = b * (y0 * b10 + z0 * a10 + z1 * a11 + z2 * a12 + z3 * a13);
    const double y3 = b * (y0 * b20 + z0 * a20 + z1 * a21 + z2 * a22 + z3 * a23);
    y4 = gg * y0 + s;
    z0 += 4 * wc * (y0 - y1 + y2);
    z1 += 2 * wc * (y1 - 2 * y2 + y3);
    z2 += 2 * wc * (y2 - 2 * y3 + y4);
    z3 += 2 * wc * (y3 - 2 * y4);
    return A * y4;
}

}  // namespace sq8l
