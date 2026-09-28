#include "mydaq/dsp/measure.h"

#include "mydaq/dsp/trigger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace mydaq::dsp {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kTwoPi = 2.0 * std::numbers::pi;

double wrapPhase(double phase)
{
    double p = std::remainder(phase, kTwoPi);
    if (p <= -std::numbers::pi)
        p += kTwoPi;
    return p;
}

// Solves a * x = b in place (b becomes x) by Gaussian elimination with partial pivoting.
template <int N>
bool solveLinear(double (&a)[N][N], double (&b)[N])
{
    for (int col = 0; col < N; ++col) {
        int pivot = col;
        for (int r = col + 1; r < N; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                pivot = r;
        if (a[pivot][col] == 0.0)
            return false;
        if (pivot != col) {
            for (int c = 0; c < N; ++c)
                std::swap(a[col][c], a[pivot][c]);
            std::swap(b[col], b[pivot]);
        }
        for (int r = col + 1; r < N; ++r) {
            const double f = a[r][col] / a[col][col];
            for (int c = col; c < N; ++c)
                a[r][c] -= f * a[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int r = N - 1; r >= 0; --r) {
        double s = b[r];
        for (int c = r + 1; c < N; ++c)
            s -= a[r][c] * b[c];
        b[r] = s / a[r][r];
    }
    return true;
}

// Least-squares fit x[n] ~= s*sin(w*t) + c*cos(w*t) + dc [+ dw * t * (s0*cos(w*t) - c0*sin(w*t))]
// with t = n - center (centered for conditioning). The optional last column linearizes a frequency
// correction around (s0, c0) for the 4-parameter fit.
struct SineFit {
    double s = 0, c = 0, dc = 0, dw = 0;
    bool ok = false;
};

SineFit fitAt(const std::vector<double>& x, double w, double center, const SineFit* linearizeAround)
{
    SineFit fit;
    if (linearizeAround) {
        double a[4][4] = {};
        double b[4] = {};
        for (size_t n = 0; n < x.size(); ++n) {
            const double t = double(n) - center;
            const double sn = std::sin(w * t);
            const double cs = std::cos(w * t);
            const double col[4] = {sn, cs, 1.0, t * (linearizeAround->s * cs - linearizeAround->c * sn)};
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j)
                    a[i][j] += col[i] * col[j];
                b[i] += col[i] * x[n];
            }
        }
        fit.ok = solveLinear(a, b);
        fit.s = b[0];
        fit.c = b[1];
        fit.dc = b[2];
        fit.dw = b[3];
        return fit;
    }
    double a[3][3] = {};
    double b[3] = {};
    for (size_t n = 0; n < x.size(); ++n) {
        const double t = double(n) - center;
        const double col[3] = {std::sin(w * t), std::cos(w * t), 1.0};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                a[i][j] += col[i] * col[j];
            b[i] += col[i] * x[n];
        }
    }
    fit.ok = solveLinear(a, b);
    fit.s = b[0];
    fit.c = b[1];
    fit.dc = b[2];
    return fit;
}

// s*sin(w*t) + c*cos(w*t) = A*sin(w*t + p); shift the phase reference from t = 0 (n = center) to n = 0.
Tone toneFromFit(const SineFit& fit, double w, double center, double sampleRate)
{
    Tone tone;
    tone.frequency = w * sampleRate / kTwoPi;
    tone.amplitude = std::hypot(fit.s, fit.c);
    tone.phase = wrapPhase(std::atan2(fit.c, fit.s) - w * center);
    tone.offset = fit.dc;
    return tone;
}

Tone invalidTone() { return {kNaN, kNaN, kNaN, kNaN}; }

struct MidCrossings {
    std::vector<double> rising;
    std::vector<double> falling;
};

MidCrossings midCrossings(const std::vector<double>& x, double hysteresisFraction)
{
    if (x.size() < 3)
        return {};
    const auto [lo, hi] = std::minmax_element(x.begin(), x.end());
    const double vpp = *hi - *lo;
    if (!(vpp > 0.0))
        return {};
    const double mid = 0.5 * (*hi + *lo);
    const double hysteresis = std::max(0.0, hysteresisFraction) * vpp;
    return {findEdges(x, {mid, Slope::Rising, hysteresis}), findEdges(x, {mid, Slope::Falling, hysteresis})};
}

}  // namespace

// ---------------------------------------------------------------------------------------------

double mean(const std::vector<double>& x)
{
    if (x.empty())
        return kNaN;
    double sum = 0.0;
    for (double v : x)
        sum += v;
    return sum / double(x.size());
}

double rms(const std::vector<double>& x)
{
    if (x.empty())
        return kNaN;
    double sum = 0.0;
    for (double v : x)
        sum += v * v;
    return std::sqrt(sum / double(x.size()));
}

double acRms(const std::vector<double>& x)
{
    const double m = mean(x);
    if (std::isnan(m))
        return kNaN;
    double sum = 0.0;
    for (double v : x)
        sum += (v - m) * (v - m);
    return std::sqrt(sum / double(x.size()));
}

double minimum(const std::vector<double>& x) { return x.empty() ? kNaN : *std::min_element(x.begin(), x.end()); }

double maximum(const std::vector<double>& x) { return x.empty() ? kNaN : *std::max_element(x.begin(), x.end()); }

double peakToPeak(const std::vector<double>& x)
{
    if (x.empty())
        return kNaN;
    const auto [lo, hi] = std::minmax_element(x.begin(), x.end());
    return *hi - *lo;
}

double period(const std::vector<double>& x, double sampleRate, double hysteresisFraction)
{
    const MidCrossings c = midCrossings(x, hysteresisFraction);
    double span = 0.0;
    size_t intervals = 0;
    for (const std::vector<double>* edges : {&c.rising, &c.falling}) {
        if (edges->size() >= 2) {
            span += edges->back() - edges->front();
            intervals += edges->size() - 1;
        }
    }
    return intervals > 0 ? span / double(intervals) / sampleRate : kNaN;
}

double frequency(const std::vector<double>& x, double sampleRate, double hysteresisFraction)
{
    return 1.0 / period(x, sampleRate, hysteresisFraction);
}

double dutyCycle(const std::vector<double>& x, double hysteresisFraction)
{
    const MidCrossings c = midCrossings(x, hysteresisFraction);
    double high = 0.0;
    double total = 0.0;
    size_t f = 0;
    for (size_t i = 0; i + 1 < c.rising.size(); ++i) {
        const double start = c.rising[i];
        const double next = c.rising[i + 1];
        while (f < c.falling.size() && c.falling[f] <= start)
            ++f;
        if (f < c.falling.size() && c.falling[f] < next) {
            high += c.falling[f] - start;
            total += next - start;
        }
    }
    return total > 0.0 ? high / total : kNaN;
}

// ---------------------------------------------------------------------------------------------

Complex singleBinDft(const std::vector<double>& x, double cyclesPerSample)
{
    constexpr size_t kResync = 1024;   // recompute the phasor exactly every kResync samples
    const Complex step = std::polar(1.0, -kTwoPi * cyclesPerSample);
    Complex phasor;
    Complex sum;
    for (size_t n = 0; n < x.size(); ++n) {
        if (n % kResync == 0) {
            const double cycles = cyclesPerSample * double(n);
            phasor = std::polar(1.0, -kTwoPi * (cycles - std::floor(cycles)));
        }
        sum += x[n] * phasor;
        phasor *= step;
    }
    return sum;
}

Tone toneAt(const std::vector<double>& x, double sampleRate, double frequency, WindowType window)
{
    if (x.empty())
        return invalidTone();
    const std::vector<double> w = makeWindow(window, x.size());
    std::vector<double> xw(x.size());
    double sumW = 0.0;
    double sumXw = 0.0;
    for (size_t n = 0; n < x.size(); ++n) {
        xw[n] = x[n] * w[n];
        sumW += w[n];
        sumXw += xw[n];
    }
    // A*sin(wn + p) = A/(2i) (e^{i(wn+p)} - e^{-i(wn+p)}): the bin is -i * A/2 * e^{ip} * sum(w).
    const Complex bin = singleBinDft(xw, frequency / sampleRate);
    Tone tone;
    tone.frequency = frequency;
    tone.amplitude = 2.0 * std::abs(bin) / sumW;
    tone.phase = wrapPhase(std::arg(bin) + 0.5 * std::numbers::pi);
    tone.offset = sumXw / sumW;
    return tone;
}

Tone fitSine(const std::vector<double>& x, double sampleRate, double frequency)
{
    if (x.size() < 3)
        return invalidTone();
    const double w = kTwoPi * frequency / sampleRate;
    const double center = 0.5 * double(x.size() - 1);
    const SineFit fit = fitAt(x, w, center, nullptr);
    if (!fit.ok)
        return invalidTone();
    return toneFromFit(fit, w, center, sampleRate);
}

Tone extractSingleTone(const std::vector<double>& x, double sampleRate)
{
    const size_t n = x.size();
    if (n < 8)
        return invalidTone();

    // Coarse estimate: Hanning spectrum of the mean-free signal, interpolated between bins.
    const double m = mean(x);
    std::vector<double> centered(n);
    for (size_t k = 0; k < n; ++k)
        centered[k] = x[k] - m;
    const ComplexSpectrum s = complexSpectrum(centered, sampleRate, WindowType::Hanning);
    size_t peak = 1;
    for (size_t k = 1; k < s.bins.size(); ++k)
        if (std::abs(s.bins[k]) > std::abs(s.bins[peak]))
            peak = k;
    double delta = 0.0;
    if (peak + 1 < s.bins.size()) {
        const double mk = std::abs(s.bins[peak]);
        const double left = std::abs(s.bins[peak - 1]);
        const double right = std::abs(s.bins[peak + 1]);
        // Hanning: ratio a of the larger neighbour gives the offset (2a - 1) / (a + 1).
        if (mk > 0.0) {
            const double a = std::max(left, right) / mk;
            delta = (right >= left ? 1.0 : -1.0) * (2.0 * a - 1.0) / (a + 1.0);
        }
    }
    const double binWidth = kTwoPi / double(n);   // rad/sample per bin
    const double w0 = (double(peak) + delta) * binWidth;

    // Refine with the iterative 4-parameter fit; keep the coarse frequency if it wanders off.
    const double center = 0.5 * double(n - 1);
    double w = w0;
    SineFit fit = fitAt(x, w, center, nullptr);
    for (int i = 0; i < 30 && fit.ok; ++i) {
        const SineFit step = fitAt(x, w, center, &fit);
        if (!step.ok || !std::isfinite(step.dw))
            break;
        w += step.dw;
        if (std::abs(w - w0) > binWidth || w <= 0.0 || w >= std::numbers::pi) {
            w = w0;
            break;
        }
        fit = step;
        if (std::abs(step.dw) < 1e-13 * w)
            break;
    }
    fit = fitAt(x, w, center, nullptr);
    if (!fit.ok)
        return invalidTone();
    return toneFromFit(fit, w, center, sampleRate);
}

// ---------------------------------------------------------------------------------------------

double HarmonicAnalysis::harmonicLevelDb(int order) const
{
    if (order < 1 || size_t(order) > harmonicPower.size() || !(fundamentalPower > 0.0))
        return kNaN;
    return 10.0 * std::log10(harmonicPower[size_t(order) - 1] / fundamentalPower);
}

HarmonicAnalysis analyzeHarmonics(const PowerSpectrum& spectrum, int maxHarmonic, double minFrequency)
{
    HarmonicAnalysis h;
    h.thd = h.thdPlusNoise = h.sinad = h.snr = kNaN;
    const std::vector<double>& p = spectrum.power;
    const size_t n = p.size();
    const size_t lobe = size_t(std::max(1, spectrum.info.lobeHalfWidth));
    const double enbw = spectrum.info.enbw;
    if (n < 2 * lobe + 3 || !(spectrum.info.df > 0.0))
        return h;

    for (size_t k = lobe + 1; k < n; ++k)
        h.totalPower += p[k];
    h.totalPower /= enbw;

    const PeakEstimate fundamental = findPeak(spectrum, int(lobe), minFrequency);
    if (!(fundamental.power > 0.0))
        return h;
    h.fundamentalFrequency = fundamental.frequency;

    // Integrates the main lobe around `center`, skipping lines already assigned to a component.
    std::vector<bool> used(n, false);
    for (size_t k = 0; k <= lobe; ++k)
        used[k] = true;
    auto integrate = [&](size_t center) {
        const size_t first = center > lobe ? center - lobe : 0;
        const size_t last = std::min(n - 1, center + lobe);
        double sum = 0.0;
        for (size_t k = first; k <= last; ++k) {
            if (!used[k]) {
                sum += p[k];
                used[k] = true;
            }
        }
        return sum / enbw;
    };

    h.fundamentalPower = integrate(fundamental.bin);
    h.harmonicFrequency.push_back(h.fundamentalFrequency);
    h.harmonicPower.push_back(h.fundamentalPower);
    const double lastBin = double(n - 1);
    for (int order = 2; maxHarmonic <= 0 || order <= maxHarmonic; ++order) {
        const double expected = order * h.fundamentalFrequency / spectrum.info.df;
        if (expected >= lastBin)
            break;
        // The harmonic's peak is within a line of the expected position.
        size_t center = size_t(std::lround(expected));
        for (size_t k = center > 0 ? center - 1 : 0; k <= std::min(n - 1, center + 1); ++k)
            if (p[k] > p[center])
                center = k;
        h.harmonicFrequency.push_back(order * h.fundamentalFrequency);
        h.harmonicPower.push_back(integrate(center));
    }

    double harmonics = 0.0;
    for (size_t i = 1; i < h.harmonicPower.size(); ++i)
        harmonics += h.harmonicPower[i];
    const double p1 = h.fundamentalPower;
    const double rest = std::max(h.totalPower - p1, 0.0);
    h.noisePower = std::max(rest - harmonics, 0.0);
    h.thd = std::sqrt(harmonics / p1);
    h.thdPlusNoise = std::sqrt(rest / p1);
    h.sinad = rest > 0.0 ? 10.0 * std::log10(h.totalPower / rest) : std::numeric_limits<double>::infinity();
    h.snr = h.noisePower > 0.0 ? 10.0 * std::log10(p1 / h.noisePower) : std::numeric_limits<double>::infinity();
    return h;
}

HarmonicAnalysis analyzeHarmonics(const std::vector<double>& x, double sampleRate, WindowType window, int maxHarmonic)
{
    return analyzeHarmonics(powerSpectrum(x, sampleRate, window), maxHarmonic);
}

}  // namespace mydaq::dsp
