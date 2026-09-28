#include "bodemath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace mybench::instruments::bode {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

std::vector<double> sweepFrequencies(double start, double stop, int stepsPerDecade)
{
    std::vector<double> list;
    if (!(start > 0) || !(stop > 0) || start > stop || stepsPerDecade < 1)
        return list;
    const double first = std::log10(start);
    const double last = std::log10(stop);
    // Integer stepping (no accumulated rounding); the tolerance keeps a stop that is on the grid.
    for (int k = 0;; ++k) {
        const double x = first + double(k) / double(stepsPerDecade);
        if (x > last + 1e-9)
            break;
        list.push_back(k == 0 ? start : std::pow(10.0, x));
    }
    return list;
}

double acquisitionRate(double frequency, double maxRate)
{
    return std::clamp(kAiRatePerHertz * frequency, kMinAiRate, std::max(kMinAiRate, maxRate));
}

double stimulusRateLimit(double frequency, double maxRate)
{
    return std::min(kAoSamplesPerPeriod * frequency, kAoRateFraction * maxRate);
}

double selectRange(double amplitude)
{
    return kRangeHeadroom * amplitude <= 2.0 ? 2.0 : 10.0;
}

bool isClipped(const std::vector<double>& samples, double range)
{
    const double limit = kClipFraction * range;
    return std::any_of(samples.begin(), samples.end(), [limit](double v) { return std::abs(v) >= limit; });
}

double peakMagnitude(const std::vector<double>& samples)
{
    double peak = 0;
    for (double v : samples)
        peak = std::max(peak, std::abs(v));
    return peak;
}

double wrapDegrees(double degrees)
{
    if (!std::isfinite(degrees))
        return degrees;
    double d = std::fmod(degrees, 360.0);
    if (d <= -180.0)
        d += 360.0;
    else if (d > 180.0)
        d -= 360.0;
    return d;
}

std::vector<double> unwrapDegrees(const std::vector<double>& wrapped)
{
    std::vector<double> out(wrapped.size());
    double offset = 0;
    for (size_t i = 0; i < wrapped.size(); ++i) {
        if (i > 0 && std::isfinite(wrapped[i]) && std::isfinite(wrapped[i - 1])) {
            const double jump = wrapped[i] - wrapped[i - 1];
            offset -= 360.0 * std::round(jump / 360.0);
        }
        out[i] = wrapped[i] + offset;
    }
    return out;
}

std::complex<double> response(double stimulusAmplitude, double stimulusPhase, double responseAmplitude,
                              double responsePhase, double frequency, double skew)
{
    if (!(stimulusAmplitude > 0))
        return {kNaN, kNaN};
    const double phase = responsePhase - stimulusPhase - 2.0 * std::numbers::pi * frequency * skew;
    return std::polar(responseAmplitude / stimulusAmplitude, phase);
}

PointValues pointValues(std::complex<double> h, bool inverted)
{
    if (inverted)
        h = -h;
    PointValues v;
    v.gain = std::abs(h);
    v.gainDb = v.gain > 0 ? 20.0 * std::log10(v.gain) : -std::numeric_limits<double>::infinity();
    v.phase = v.gain > 0 ? wrapDegrees(std::arg(h) * 180.0 / std::numbers::pi) : 0.0;
    return v;
}

std::vector<double> levelCrossings(const std::vector<double>& frequency, const std::vector<double>& y,
                                   double level)
{
    std::vector<double> out;
    const size_t n = std::min(frequency.size(), y.size());
    for (size_t i = 1; i < n; ++i) {
        const double a = y[i - 1] - level;
        const double b = y[i] - level;
        if (!std::isfinite(a) || !std::isfinite(b) || !(frequency[i - 1] > 0) || !(frequency[i] > 0))
            continue;
        if (a == 0 && i == 1) {
            out.push_back(frequency[0]);
            continue;
        }
        if ((a < 0 && b >= 0) || (a > 0 && b <= 0)) {
            const double t = a / (a - b);
            const double l0 = std::log10(frequency[i - 1]);
            const double l1 = std::log10(frequency[i]);
            out.push_back(std::pow(10.0, l0 + t * (l1 - l0)));
        }
    }
    return out;
}

double interpolateLog(const std::vector<double>& frequency, const std::vector<double>& y, double f)
{
    const size_t n = std::min(frequency.size(), y.size());
    if (n == 0 || !(f > 0) || f < frequency.front() || f > frequency[n - 1])
        return kNaN;
    for (size_t i = 1; i < n; ++i) {
        if (f <= frequency[i]) {
            const double l0 = std::log10(frequency[i - 1]);
            const double l1 = std::log10(frequency[i]);
            const double t = l1 > l0 ? (std::log10(f) - l0) / (l1 - l0) : 0.0;
            return y[i - 1] + t * (y[i] - y[i - 1]);
        }
    }
    return y[0];
}

Markers computeMarkers(const std::vector<double>& frequency, const std::vector<double>& gainDb,
                       const std::vector<double>& phase)
{
    Markers m;
    m.unityGainFrequency = m.phaseMargin = m.phaseCrossoverFrequency = m.gainMargin = kNaN;
    m.maxGainDb = kNaN;
    for (double g : gainDb)
        if (std::isfinite(g) && !(g <= m.maxGainDb))
            m.maxGainDb = g;
    if (!std::isfinite(m.maxGainDb))
        return m;
    m.cutoff = levelCrossings(frequency, gainDb, m.maxGainDb - 3.0);

    const std::vector<double> unwrapped = unwrapDegrees(phase);
    const std::vector<double> unity = levelCrossings(frequency, gainDb, 0.0);
    if (!unity.empty()) {
        m.unityGainFrequency = unity.front();
        m.phaseMargin = 180.0 + interpolateLog(frequency, unwrapped, m.unityGainFrequency);
    }
    const std::vector<double> crossover = levelCrossings(frequency, unwrapped, -180.0);
    if (!crossover.empty()) {
        m.phaseCrossoverFrequency = crossover.front();
        m.gainMargin = -interpolateLog(frequency, gainDb, m.phaseCrossoverFrequency);
    }
    return m;
}

double settlingSeconds(double frequency)
{
    if (!(frequency > 0))
        return 0.02;
    return std::clamp(3.0 / frequency, 0.02, 3.0);
}

double estimatedPointSeconds(double frequency, double maxAiRate, bool rangeProbe, int averages)
{
    const double prepare = rangeProbe ? double(kRangeProbeSamples) / kRangeProbeRate : settlingSeconds(frequency);
    const double acquire = double(kAiSamples) / acquisitionRate(frequency, maxAiRate);
    return prepare + std::max(1, averages) * (acquire + 0.01) + 0.03;
}

}  // namespace mybench::instruments::bode
