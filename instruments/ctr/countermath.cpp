#include "countermath.h"

#include <algorithm>
#include <cmath>

namespace mybench::instruments::ctr {

using mydaq::CounterInputConfig;
using mydaq::CounterMeasurement;

bool isIntervalMeasurement(CounterMeasurement measurement)
{
    return measurement != CounterMeasurement::CountEdges && !isEncoderMeasurement(measurement);
}

bool isEncoderMeasurement(CounterMeasurement measurement)
{
    return measurement == CounterMeasurement::LinearEncoder || measurement == CounterMeasurement::AngularEncoder;
}

double inputTimebase(CounterMeasurement measurement, double minValue, double maxValue)
{
    if (!isIntervalMeasurement(measurement))
        return 100e6;
    const double longest = measurement == CounterMeasurement::Frequency ? (minValue > 0 ? 1.0 / minValue : HUGE_VAL)
                                                                       : maxValue;
    for (const double timebase : {100e6, 20e6}) {
        if (longest * timebase < kCounterRange)
            return timebase;
    }
    return 100e3;
}

uint8_t inputLines(const CounterInputConfig& config)
{
    uint8_t mask = 0;
    for (const PinRole& pin : inputPinRoles(config))
        mask |= uint8_t(1u << pin.line);
    return mask;
}

std::vector<PinRole> inputPinRoles(const CounterInputConfig& config)
{
    switch (config.measurement) {
    case CounterMeasurement::CountEdges: {
        std::vector<PinRole> pins{{0, "Source", "Edges to count"}};
        if (config.resetOnDio1)
            pins.push_back({1, "Reset", "Count reset (rising edge)"});
        if (config.direction == CounterInputConfig::Direction::External)
            pins.push_back({2, "Up/Dn", "Count direction (high = up)"});
        return pins;
    }
    case CounterMeasurement::Frequency:
    case CounterMeasurement::Period:
    case CounterMeasurement::PulseWidth:
    case CounterMeasurement::SemiPeriod:
        return {{1, "Input", "Signal to measure"}};
    case CounterMeasurement::TwoEdgeSeparation:
        return {{1, "Stop", "Second edge"}, {2, "Start", "First edge"}};
    case CounterMeasurement::LinearEncoder:
    case CounterMeasurement::AngularEncoder: {
        const bool twoPulse = config.decoding == CounterInputConfig::Decoding::TwoPulse;
        std::vector<PinRole> pins{{0, twoPulse ? "Up" : "A", twoPulse ? "Count-up pulses" : "Encoder phase A"}};
        if (config.zIndexEnabled)
            pins.push_back({1, "Z", "Index pulse"});
        pins.push_back({2, twoPulse ? "Down" : "B", twoPulse ? "Count-down pulses" : "Encoder phase B"});
        return pins;
    }
    }
    return {};
}

double minimumPulseFrequency() { return kPulseTimebase / kCounterRange; }
double maximumPulseFrequency() { return kPulseTimebase / 4.0; }

double coercePulseFrequency(double requested)
{
    if (!(requested > 0.0))
        return minimumPulseFrequency();
    return kPulseTimebase / std::clamp(std::round(kPulseTimebase / requested), 4.0, kCounterRange);
}

double coercePulseDuty(double frequency, double duty)
{
    const double ticks = std::round(kPulseTimebase / coercePulseFrequency(frequency));
    return std::clamp(std::round(duty * ticks), 2.0, ticks - 2.0) / ticks;
}

const std::vector<FreqOutSetting>& freqOutTable()
{
    static const std::vector<FreqOutSetting> table = [] {
        std::vector<FreqOutSetting> entries;
        for (const double timebase : {20e6, 10e6, 100e3}) {
            for (int n = 1; n <= 16; ++n) {
                const double f = timebase / n;
                const bool duplicate = std::any_of(entries.begin(), entries.end(), [f](const FreqOutSetting& e) {
                    return std::abs(e.frequency - f) <= 1e-9 * f;
                });
                if (!duplicate)
                    entries.push_back({f, timebase, n});
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const FreqOutSetting& a, const FreqOutSetting& b) { return a.frequency > b.frequency; });
        return entries;
    }();
    return table;
}

FreqOutSetting coerceFreqOut(double requested)
{
    const auto& table = freqOutTable();
    FreqOutSetting best = table.front();
    for (const FreqOutSetting& entry : table) {
        if (std::abs(entry.frequency - requested) < std::abs(best.frequency - requested))
            best = entry;
    }
    return best;
}

void RunningStats::add(double value)
{
    if (!std::isfinite(value))
        return;
    if (m_count == 0) {
        m_min = m_max = value;
    } else {
        m_min = std::min(m_min, value);
        m_max = std::max(m_max, value);
    }
    ++m_count;
    const double delta = value - m_mean;
    m_mean += delta / double(m_count);
    m_m2 += delta * (value - m_mean);
}

double RunningStats::standardDeviation() const
{
    if (m_count < 2)
        return std::numeric_limits<double>::quiet_NaN();
    return std::sqrt(std::max(0.0, m_m2 / double(m_count - 1)));
}

double CountUnwrapper::add(double raw)
{
    if (!std::isfinite(raw))
        return m_value;
    double delta = std::fmod(raw - m_raw, kCounterRange);
    if (delta >= kCounterRange / 2)
        delta -= kCounterRange;
    else if (delta < -kCounterRange / 2)
        delta += kCounterRange;
    m_raw = raw;
    m_value += delta;
    return m_value;
}

double RateEstimator::add(double time, double value)
{
    if (!std::isfinite(value))
        return m_rate;
    if (!m_hasReference) {
        m_hasReference = true;
        m_time = time;
        m_value = value;
        return m_rate;
    }
    const double dt = time - m_time;
    if (dt >= m_window) {
        m_rate = (value - m_value) / dt;
        m_time = time;
        m_value = value;
    }
    return m_rate;
}

void RateEstimator::reset()
{
    m_hasReference = false;
    m_rate = std::numeric_limits<double>::quiet_NaN();
}

}  // namespace mybench::instruments::ctr
