#include "equalizer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mybench::instruments::aeq {

namespace {

constexpr double kPi = std::numbers::pi;

// Coefficients of the reverse Bessel polynomial theta_n(s) = sum a_k s^k (a_n = 1).
std::vector<double> besselPolynomial(int order)
{
    std::vector<double> a(size_t(order) + 1);
    for (int k = 0; k <= order; ++k) {
        // a_k = (2n - k)! / (2^(n-k) k! (n-k)!)
        double value = 1;
        for (int i = order - k + 1; i <= 2 * order - k; ++i)
            value *= i;
        for (int i = 1; i <= k; ++i)
            value /= i;
        value /= std::pow(2.0, order - k);
        a[size_t(k)] = value;
    }
    return a;
}

Complex evaluate(const std::vector<double>& a, Complex s)
{
    Complex value = 0;
    for (size_t k = a.size(); k-- > 0;)
        value = value * s + a[k];
    return value;
}

// Durand-Kerner roots of a monic polynomial.
std::vector<Complex> roots(const std::vector<double>& a)
{
    const size_t n = a.size() - 1;
    std::vector<Complex> z(n);
    for (size_t i = 0; i < n; ++i)
        z[i] = std::pow(Complex(0.4, 0.9), double(i)) * 3.0;
    for (int iteration = 0; iteration < 500; ++iteration) {
        double change = 0;
        for (size_t i = 0; i < n; ++i) {
            Complex denominator = 1;
            for (size_t j = 0; j < n; ++j) {
                if (j != i)
                    denominator *= z[i] - z[j];
            }
            const Complex step = evaluate(a, z[i]) / denominator;
            z[i] -= step;
            change = std::max(change, std::abs(step));
        }
        if (change < 1e-15)
            break;
    }
    return z;
}

Complex bilinear(Complex s, double sampleRate)
{
    const double k = 2.0 * sampleRate;
    return (k + s) / (k - s);
}

double prewarp(double frequency, double sampleRate)
{
    return 2.0 * sampleRate * std::tan(kPi * frequency / sampleRate);
}

Biquad sectionFromPole(Complex z, double b0, double b1, double b2)
{
    Biquad q;
    q.b0 = b0;
    q.b1 = b1;
    q.b2 = b2;
    q.a1 = -2.0 * z.real();
    q.a2 = std::norm(z);
    return q;
}

}  // namespace

Complex Biquad::response(double frequency, double sampleRate) const
{
    const Complex z1 = std::polar(1.0, -2.0 * kPi * frequency / sampleRate);   // z^-1
    const Complex z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

Complex Cascade::response(double frequency, double sampleRate) const
{
    Complex h = 1;
    for (const Biquad& s : sections)
        h *= s.response(frequency, sampleRate);
    return h;
}

void Cascade::reset()
{
    for (Biquad& s : sections)
        s.reset();
}

double Cascade::maxPoleRadius() const
{
    double radius = 0;
    for (const Biquad& s : sections) {
        // z^2 + a1 z + a2 = 0
        const Complex d = std::sqrt(Complex(s.a1 * s.a1 - 4.0 * s.a2, 0));
        radius = std::max({radius, std::abs((-s.a1 + d) / 2.0), std::abs((-s.a1 - d) / 2.0)});
    }
    return radius;
}

void Cascade::flushDenormals()
{
    for (Biquad& s : sections) {
        if (std::abs(s.s1) < 1e-30)
            s.s1 = 0;
        if (std::abs(s.s2) < 1e-30)
            s.s2 = 0;
    }
}

std::vector<Complex> besselPrototypePoles(int order)
{
    order = std::clamp(order, 1, 10);
    const std::vector<double> a = besselPolynomial(order);
    // |H(jw)|^2 of H(s) = a0 / theta(s) (delay-normalized) is monotonic: find the -3 dB point.
    auto magnitude = [&](double w) { return a[0] / std::abs(evaluate(a, Complex(0, w))); };
    double lo = 0.1;
    double hi = 10.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        (magnitude(mid) > std::sqrt(0.5) ? lo : hi) = mid;
    }
    const double w3db = 0.5 * (lo + hi);
    std::vector<Complex> poles = roots(a);
    for (Complex& p : poles)
        p /= w3db;
    std::sort(poles.begin(), poles.end(), [](Complex x, Complex y) { return x.imag() < y.imag(); });
    return poles;
}

Cascade besselLowpass(int order, double cutoff, double sampleRate)
{
    const double wc = prewarp(cutoff, sampleRate);
    Cascade cascade;
    for (const Complex p : besselPrototypePoles(order)) {
        const Complex z = bilinear(p * wc, sampleRate);
        if (p.imag() > 1e-12) {
            // Zeros at z = -1; unity gain at DC.
            Biquad q = sectionFromPole(z, 1, 2, 1);
            const double g = (1.0 + q.a1 + q.a2) / 4.0;
            q.b0 *= g;
            q.b1 *= g;
            q.b2 *= g;
            cascade.sections.push_back(q);
        } else if (std::abs(p.imag()) <= 1e-12) {
            // Real pole (odd orders): first-order section as a biquad.
            Biquad q;
            const double r = z.real();
            q.a1 = -r;
            q.a2 = 0;
            q.b0 = q.b1 = (1.0 - r) / 2.0;
            q.b2 = 0;
            cascade.sections.push_back(q);
        }
    }
    return cascade;
}

Cascade besselBandpass(int order, double low, double high, double sampleRate)
{
    const double w1 = prewarp(low, sampleRate);
    const double w2 = prewarp(high, sampleRate);
    const double w0 = std::sqrt(w1 * w2);
    const double bw = w2 - w1;
    std::vector<Complex> poles;
    for (const Complex p : besselPrototypePoles(order)) {
        // s^2 - p bw s + w0^2 = 0
        const Complex pb = p * bw;
        const Complex d = std::sqrt(pb * pb - 4.0 * w0 * w0);
        for (const Complex s : {(pb + d) / 2.0, (pb - d) / 2.0}) {
            if (s.imag() > 0)
                poles.push_back(s);
        }
    }
    const double centerDigital = sampleRate / kPi * std::atan(w0 / (2.0 * sampleRate));
    Cascade cascade;
    for (const Complex s : poles) {
        Biquad q = sectionFromPole(bilinear(s, sampleRate), 1, 0, -1);   // zeros at z = +1 and -1
        const double g = 1.0 / std::abs(q.response(centerDigital, sampleRate));
        q.b0 *= g;
        q.b2 *= g;
        cascade.sections.push_back(q);
    }
    return cascade;
}

std::array<double, BandCount> EqSettings::linearGains() const
{
    std::array<double, BandCount> g{};
    for (int b = 0; b < BandCount; ++b)
        g[size_t(b)] = std::pow(10.0, std::clamp(gainsDb[size_t(b)], -kMaxGainDb, kMaxGainDb) / 20.0);
    return g;
}

Equalizer::Equalizer(double sampleRate) : m_sampleRate(sampleRate)
{
    const double nyquistLimit = 0.45 * sampleRate;
    m_bands[Bass] = besselLowpass(kFilterOrder, std::min(kBassCutoff, nyquistLimit), sampleRate);
    m_bands[Mid] = besselBandpass(kFilterOrder, kMidLow, std::min(kMidHigh, nyquistLimit), sampleRate);
    m_bands[Treble] = besselBandpass(kFilterOrder, kTrebleLow, std::min(kTrebleHigh, nyquistLimit), sampleRate);
}

void Equalizer::reset()
{
    for (Cascade& c : m_bands)
        c.reset();
    m_volume = -1;
}

void Equalizer::process(std::span<const double> input, std::span<double> output, const EqSettings& settings)
{
    const size_t n = std::min(input.size(), output.size());
    const std::array<double, BandCount> target = settings.linearGains();
    const double volume = std::clamp(settings.volume, 0.0, 1.0);
    if (m_volume < 0) {
        m_gains = target;
        m_volume = volume;
        m_bypass = settings.bypass;
    }
    // Bypass crossfades over the block like any other change.
    const double wetFrom = m_bypass ? 0.0 : 1.0;
    const double wetTo = settings.bypass ? 0.0 : 1.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = n > 1 ? double(i + 1) / double(n) : 1.0;
        const double x = input[i];
        double sum = 0;
        for (int b = 0; b < BandCount; ++b) {
            const double g = m_gains[size_t(b)] + (target[size_t(b)] - m_gains[size_t(b)]) * t;
            sum += g * m_bands[size_t(b)].process(x);
        }
        const double wet = wetFrom + (wetTo - wetFrom) * t;
        const double v = m_volume + (volume - m_volume) * t;
        output[i] = v * (wet * sum + (1.0 - wet) * x);
    }
    m_gains = target;
    m_volume = volume;
    m_bypass = settings.bypass;
    for (Cascade& c : m_bands)
        c.flushDenormals();
}

Complex Equalizer::response(double frequency, const EqSettings& settings) const
{
    const double volume = std::clamp(settings.volume, 0.0, 1.0);
    if (settings.bypass)
        return volume;
    const std::array<double, BandCount> g = settings.linearGains();
    Complex h = 0;
    for (int b = 0; b < BandCount; ++b)
        h += g[size_t(b)] * m_bands[size_t(b)].response(frequency, m_sampleRate);
    return volume * h;
}

OctaveBands octaveBands(int fraction, double lowest, double highest)
{
    OctaveBands bands;
    fraction = std::max(1, fraction);
    const int kLow = int(std::ceil(double(fraction) * std::log2(lowest / 1000.0) - 1e-9));
    const int kHigh = int(std::floor(double(fraction) * std::log2(highest / 1000.0) + 1e-9));
    const double half = std::pow(2.0, 0.5 / double(fraction));
    for (int k = kLow; k <= kHigh; ++k) {
        const double center = 1000.0 * std::pow(2.0, double(k) / double(fraction));
        bands.centers.push_back(center);
        bands.lower.push_back(center / half);
        bands.upper.push_back(center * half);
    }
    return bands;
}

std::vector<double> bandPowers(const OctaveBands& bands, std::span<const double> power, double df)
{
    std::vector<double> result(bands.centers.size(), 0.0);
    if (power.empty() || !(df > 0))
        return result;
    for (size_t b = 0; b < result.size(); ++b) {
        const double lo = bands.lower[b];
        const double hi = bands.upper[b];
        const auto first = size_t(std::max(0.0, std::floor(lo / df - 0.5)));
        const auto last = std::min(power.size() - 1, size_t(std::ceil(hi / df + 0.5)));
        double sum = 0;
        for (size_t k = first; k <= last && k < power.size(); ++k) {
            const double binLo = (double(k) - 0.5) * df;
            const double binHi = (double(k) + 0.5) * df;
            const double overlap = std::min(hi, binHi) - std::max(lo, binLo);
            if (overlap > 0)
                sum += power[k] * overlap / df;
        }
        result[b] = sum;
    }
    return result;
}

Level measureLevel(std::span<const double> x, double fullScale)
{
    Level level;
    if (x.empty() || !(fullScale > 0))
        return level;
    double peak = 0;
    double sumSquares = 0;
    for (double v : x) {
        peak = std::max(peak, std::abs(v));
        sumSquares += v * v;
    }
    const double rms = std::sqrt(sumSquares / double(x.size()));
    level.peakDb = peak > 0 ? std::max(-120.0, 20.0 * std::log10(peak / fullScale)) : -120.0;
    level.rmsDb = rms > 0 ? std::max(-120.0, 20.0 * std::log10(rms / fullScale)) : -120.0;
    return level;
}

}  // namespace mybench::instruments::aeq
