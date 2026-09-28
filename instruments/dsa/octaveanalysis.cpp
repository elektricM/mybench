#include "octaveanalysis.h"

#include <mydaq/dsp/units.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace Qt::StringLiterals;

namespace mybench::instruments::octave {

namespace {

using Complex = std::complex<double>;

// Nominal centres of one decade (IEC 61260 preferred values), repeated ×10.
constexpr std::array kNominalMantissa{1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0};
constexpr int kFirstBand = -17;   // 1000 · 10^(-17/10) ≈ 20 Hz (nominal 20 Hz): 31 ISO bands 20 Hz … 20 kHz
constexpr int kLastBand = 13;     // 1000 · 10^(13/10) ≈ 20 kHz

std::vector<Band> makeBands()
{
    std::vector<Band> list;
    for (int n = kFirstBand; n <= kLastBand; ++n) {
        Band b;
        b.centre = 1000.0 * std::pow(10.0, n / 10.0);
        b.lower = b.centre * std::pow(10.0, -1.0 / 20.0);
        b.upper = b.centre * std::pow(10.0, 1.0 / 20.0);
        const int index = ((n % 10) + 10) % 10;
        const int decade = int(std::floor(n / 10.0)) + 3;   // n = 0 → 1000 = 1.0 × 10^3
        b.nominal = kNominalMantissa[size_t(index)] * std::pow(10.0, decade);
        b.label = b.nominal >= 1000.0 ? QString::number(b.nominal / 1000.0, 'g', 3) + u" kHz"_s
                                      : QString::number(b.nominal, 'g', 3) + u" Hz"_s;
        list.push_back(b);
    }
    return list;
}

double squared(double v)
{
    return v * v;
}

double alphaFor(double tau, double sampleRate)
{
    return tau > 0.0 ? 1.0 - std::exp(-1.0 / (tau * sampleRate)) : 1.0;
}

}  // namespace

const std::vector<Band>& bands()
{
    static const std::vector<Band> list = makeBands();
    return list;
}

int bandIndex(double hertz)
{
    const std::vector<Band>& list = bands();
    int best = 0;
    for (int i = 1; i < int(list.size()); ++i) {
        if (std::abs(std::log(list[size_t(i)].nominal / hertz)) < std::abs(std::log(list[size_t(best)].nominal / hertz)))
            best = i;
    }
    return best;
}

double weightingDb(Weighting weighting, double frequency)
{
    const double f2 = squared(frequency);
    const double c1 = squared(20.598997), c2 = squared(107.65265), c3 = squared(737.86223), c4 = squared(12194.217);
    switch (weighting) {
    case Weighting::Z:
        return 0.0;
    case Weighting::A: {
        const double r = c4 * f2 * f2 / ((f2 + c1) * std::sqrt((f2 + c2) * (f2 + c3)) * (f2 + c4));
        return 20.0 * std::log10(r) + 2.000;
    }
    case Weighting::B: {
        const double r = c4 * f2 * frequency / ((f2 + c1) * std::sqrt(f2 + squared(158.48932)) * (f2 + c4));
        return 20.0 * std::log10(r) + 0.170;
    }
    case Weighting::C: {
        const double r = c4 * f2 / ((f2 + c1) * (f2 + c4));
        return 20.0 * std::log10(r) + 0.062;
    }
    }
    return 0.0;
}

TimeConstants timeConstants(Averaging averaging, double customMs)
{
    switch (averaging) {
    case Averaging::Slow: return {1.0, 1.0};
    case Averaging::Fast: return {0.125, 0.125};
    case Averaging::Impulse: return {0.035, 1.5};
    case Averaging::Custom: {
        const double tau = std::clamp(customMs, 0.0, 10000.0) / 1000.0;
        return {tau, tau};
    }
    }
    return {};
}

Complex Biquad::response(double omega) const
{
    const Complex z1 = std::polar(1.0, -omega);
    const Complex z2 = z1 * z1;
    return (b0 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

BandPassFilter::BandPassFilter(double lower, double upper, double centre, double sampleRate)
    : m_sampleRate(sampleRate)
{
    // Prewarped analog edges, geometric centre and bandwidth (rad/s).
    const double k = 2.0 * sampleRate;
    const double w1 = k * std::tan(std::numbers::pi * lower / sampleRate);
    const double w2 = k * std::tan(std::numbers::pi * upper / sampleRate);
    const double w0 = std::sqrt(w1 * w2);
    const double bw = w2 - w1;

    // 3rd-order Butterworth low-pass prototype poles; the low-pass → band-pass substitution
    // s → (s² + w0²) / (bw · s) maps each to the roots of s² − p·bw·s + w0². Of the six poles
    // (three conjugate pairs) keep the upper half-plane ones: one biquad per pair.
    std::vector<Complex> poles;
    for (int i = 0; i < 3; ++i) {
        const Complex p = std::polar(1.0, std::numbers::pi * (2.0 * i + 4.0) / 6.0);   // 2π/3, π, 4π/3
        const Complex root = std::sqrt(p * p * bw * bw - 4.0 * w0 * w0);
        for (const Complex s : {(p * bw + root) / 2.0, (p * bw - root) / 2.0}) {
            if (s.imag() > 0.0)
                poles.push_back(s);
        }
    }
    std::sort(poles.begin(), poles.end(), [](Complex a, Complex b) { return a.imag() < b.imag(); });
    poles.resize(3);

    // Each section bw·s / (s² − 2 Re(p) s + |p|²), bilinear s = k (1 − z⁻¹)/(1 + z⁻¹).
    double slowest = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < 3; ++i) {
        const double a1 = -2.0 * poles[i].real();
        const double a0 = std::norm(poles[i]);
        const double c0 = k * k + a1 * k + a0;
        Biquad& q = m_sections[i];
        q.b0 = bw * k / c0;
        q.b2 = -q.b0;
        q.a1 = (2.0 * a0 - 2.0 * k * k) / c0;
        q.a2 = (k * k - a1 * k + a0) / c0;
        // |z|² = a2 for a complex pole pair: decay rate σ = −ln|z| · fs.
        slowest = std::min(slowest, -0.5 * std::log(q.a2) * sampleRate);
    }
    m_settlingTime = slowest > 0.0 ? std::log(100.0) / slowest : 0.0;

    // Exactly 0 dB at the band centre.
    const double gain = std::abs(response(centre));
    if (gain > 0.0) {
        m_sections[0].b0 /= gain;
        m_sections[0].b2 /= gain;
    }
}

void BandPassFilter::reset()
{
    for (Biquad& q : m_sections)
        q.reset();
}

Complex BandPassFilter::response(double frequency) const
{
    const double omega = 2.0 * std::numbers::pi * frequency / m_sampleRate;
    return m_sections[0].response(omega) * m_sections[1].response(omega) * m_sections[2].response(omega);
}

double BandPassFilter::responseDb(double frequency) const
{
    return 20.0 * std::log10(std::max(std::abs(response(frequency)), 1e-20));
}

OctaveFilterBank::OctaveFilterBank(double sampleRate) : m_sampleRate(sampleRate)
{
    for (const Band& band : bands()) {
        // Bands at or above Nyquist cannot exist at this rate (never the case at 50 kS/s).
        if (band.upper >= 0.5 * sampleRate)
            break;
        m_filters.emplace_back(band.lower, band.upper, band.centre, sampleRate);
    }
    m_meanSquare.assign(m_filters.size(), 0.0);
    setTimeConstants(timeConstants(Averaging::Fast, 125));
}

void OctaveFilterBank::setTimeConstants(const TimeConstants& constants)
{
    m_riseAlpha = alphaFor(constants.rise, m_sampleRate);
    m_fallAlpha = alphaFor(constants.fall, m_sampleRate);
}

void OctaveFilterBank::reset()
{
    for (BandPassFilter& filter : m_filters)
        filter.reset();
    std::fill(m_meanSquare.begin(), m_meanSquare.end(), 0.0);
    m_samples = 0;
}

void OctaveFilterBank::process(std::span<const double> samples)
{
    const double rise = m_riseAlpha;
    const double fall = m_fallAlpha;
    for (size_t b = 0; b < m_filters.size(); ++b) {
        BandPassFilter& filter = m_filters[b];
        double m = m_meanSquare[b];
        for (double x : samples) {
            const double y = filter.process(x);
            const double p = y * y;
            m += (p > m ? rise : fall) * (p - m);
        }
        m_meanSquare[b] = m;
    }
    m_samples += (long long)samples.size();
}

}  // namespace mybench::instruments::octave
