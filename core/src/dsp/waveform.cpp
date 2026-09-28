#include "mydaq/dsp/waveform.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace mydaq::dsp {

namespace {

constexpr int kRateCandidates = 64;          // rates tried below the preferred one
constexpr double kRateCandidateStep = 5e-4;  // relative spacing of the candidate rates

// Visits the convergents p/q (q <= maxQ, increasing q) of the continued fraction of x > 0, then the
// best semiconvergent at the denominator limit. Stops early when visit() returns false.
template <class Visit>
void forEachConvergent(double x, uint64_t maxQ, Visit&& visit)
{
    uint64_t p0 = 0, q0 = 1, p1 = 1, q1 = 0;
    double r = x;
    for (int i = 0; i < 64; ++i) {
        const double a = std::floor(r);
        if (a > 1e15)
            return;
        const auto ai = uint64_t(a);
        const uint64_t p2 = ai * p1 + p0;
        const uint64_t q2 = ai * q1 + q0;
        if (q2 > maxQ) {
            if (q1 > 0) {
                const uint64_t m = (maxQ - q0) / q1;
                if (m > 0)
                    visit(m * p1 + p0, m * q1 + q0);
            }
            return;
        }
        if (!visit(p2, q2))
            return;
        p0 = p1;
        q0 = q1;
        p1 = p2;
        q1 = q2;
        const double fraction = r - a;
        if (fraction < 1e-15)
            return;
        r = 1.0 / fraction;
    }
}

// Highest coerced rate not above maxRate, starting from `requested`.
double rateAtMost(double requested, double maxRate, const std::function<double(double)>& coerceRate)
{
    double r = std::min(requested, maxRate);
    double rate = coerceRate(r);
    for (int i = 0; i < 64 && rate > maxRate * (1.0 + 1e-12); ++i) {
        r *= 1.0 - 1e-4;
        rate = coerceRate(r);
    }
    return rate;
}

void repeatToMinLength(BufferPlan& plan, size_t minLength, size_t maxLength)
{
    if (plan.length == 0 || plan.length >= minLength)
        return;
    const size_t repeat = (minLength + plan.length - 1) / plan.length;
    if (plan.length * repeat <= maxLength) {
        plan.length *= repeat;
        plan.cycles *= repeat;
    }
}

}  // namespace

double waveformShape(Waveform type, double cycles, double dutyCycle)
{
    const double p = cycles - std::floor(cycles);
    switch (type) {
    case Waveform::Sine: return std::sin(2.0 * std::numbers::pi * p);
    case Waveform::Square: return p < std::clamp(dutyCycle, 0.0, 1.0) ? 1.0 : -1.0;
    case Waveform::Triangle:
        if (p < 0.25)
            return 4.0 * p;
        if (p < 0.75)
            return 2.0 - 4.0 * p;
        return 4.0 * p - 4.0;
    case Waveform::Sawtooth: return p < 0.5 ? 2.0 * p : 2.0 * p - 2.0;
    case Waveform::Dc:
    case Waveform::Noise: return 0.0;
    }
    return 0.0;
}

std::vector<double> generate(const WaveformParams& params, double sampleRate, size_t n, double startTime,
                             uint64_t seed)
{
    std::vector<double> out(n);
    if (params.type == Waveform::Noise) {
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<double> uniform(-1.0, 1.0);
        for (double& v : out)
            v = params.offset + params.amplitude * uniform(rng);
        return out;
    }
    const double phaseCycles = params.phase / (2.0 * std::numbers::pi);
    for (size_t k = 0; k < n; ++k) {
        const double t = startTime + double(k) / sampleRate;
        out[k] = params.offset
            + params.amplitude * waveformShape(params.type, params.frequency * t + phaseCycles, params.dutyCycle);
    }
    return out;
}

BufferPlan planPeriodicBuffer(double frequency, double maxRate, const std::function<double(double)>& coerceRate,
                              size_t maxLength, size_t minLength, double tolerance)
{
    maxLength = std::max<size_t>(maxLength, 1);
    minLength = std::clamp<size_t>(minLength, 1, maxLength);

    BufferPlan best;
    if (!(frequency > 0.0)) {
        best.sampleRate = rateAtMost(maxRate, maxRate, coerceRate);
        best.length = minLength;
        return best;
    }

    // Highest rate that still fits one whole period in maxLength samples.
    const double target = std::min(maxRate, frequency * double(maxLength));
    best.relativeError = std::numeric_limits<double>::infinity();
    double previousRate = -1.0;
    for (int i = 0; i < kRateCandidates; ++i) {
        const double rate = rateAtMost(target * (1.0 - kRateCandidateStep * i), maxRate, coerceRate);
        if (!(rate > 0.0) || rate == previousRate)
            continue;
        previousRate = rate;
        bool found = false;
        forEachConvergent(frequency / rate, maxLength, [&](uint64_t cycles, uint64_t length) {
            if (cycles == 0)
                return true;
            const double f = double(cycles) * rate / double(length);
            const double error = std::abs(f - frequency) / frequency;
            if (error < best.relativeError)
                best = {rate, size_t(length), size_t(cycles), f, error};
            // Convergents come in increasing length: the first one within tolerance is the shortest.
            found = error < tolerance;
            return !found;
        });
        if (found)
            break;
    }
    repeatToMinLength(best, minLength, maxLength);
    return best;
}

std::vector<double> generatePeriodic(const WaveformParams& params, const BufferPlan& plan, uint64_t seed)
{
    const size_t n = plan.length;
    std::vector<double> out(n);
    if (params.type == Waveform::Noise) {
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<double> uniform(-1.0, 1.0);
        for (double& v : out)
            v = params.offset + params.amplitude * uniform(rng);
        return out;
    }
    const double phaseCycles = params.phase / (2.0 * std::numbers::pi);
    for (size_t k = 0; k < n; ++k) {
        const uint64_t numerator = (uint64_t(plan.cycles) * uint64_t(k)) % uint64_t(n);
        const double cycles = double(numerator) / double(n) + phaseCycles;
        out[k] = params.offset + params.amplitude * waveformShape(params.type, cycles, params.dutyCycle);
    }
    return out;
}

}  // namespace mydaq::dsp
