// Pure helpers of the Counter/Timer (no Qt objects, unit-tested in tests/tst_countermath.cpp):
// the myDAQ counter's timing rules as documented in mydaq/device.h and docs/device.md, the
// DIO terminals each function uses, FREQOUT's divider table and running statistics.
#pragma once

#include <mydaq/device.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace mybench::instruments::ctr {

constexpr double kCounterRange = 4294967296.0;   // 32-bit counter
constexpr double kPulseTimebase = 100e6;         // ctr0 pulse generation timebase
constexpr int kPulseOutputLine = 3;              // CTR 0 OUT
constexpr int kFreqOutLine = 4;                  // FREQOUT

// Interval measurements (Frequency, Period, PulseWidth, SemiPeriod, TwoEdgeSeparation) count a
// timebase between input edges; CountEdges and the encoders count input edges.
bool isIntervalMeasurement(mydaq::CounterMeasurement measurement);
bool isEncoderMeasurement(mydaq::CounterMeasurement measurement);

// Timebase (Hz) picked for an interval measurement from its expected range: the fastest of
// 100 MHz, 20 MHz and 100 kHz whose 32-bit count does not overflow over the longest expected
// interval (a period of `minValue` for Frequency, `maxValue` otherwise). Counting measurements
// report 100 MHz (unused).
double inputTimebase(mydaq::CounterMeasurement measurement, double minValue, double maxValue);
// Longest interval (s) the timebase can count before the 32-bit register wraps.
inline double longestInterval(double timebase) { return kCounterRange / timebase; }

// DIO lines (bit n = DIO n) a counter input task reserves (default terminals of device.h).
uint8_t inputLines(const mydaq::CounterInputConfig& config);

// Role of each DIO line for a counter input configuration ("Input", "A", "Z"...; empty = unused).
struct PinRole {
    int line = 0;
    const char* role = "";       // short name shown on the pin
    const char* signal = "";     // longer description ("Signal to measure", "Count direction")
};
std::vector<PinRole> inputPinRoles(const mydaq::CounterInputConfig& config);

// Pulse train (ctr0): frequency = 100 MHz / n with n in [4, 2^32]; duty coerced so that the high
// and low times are at least 2 ticks. Mirrors Device::coerceCounterFrequency for the display.
double coercePulseFrequency(double requested);
double coercePulseDuty(double frequency, double duty);
double minimumPulseFrequency();   // 100 MHz / 2^32
double maximumPulseFrequency();   // 25 MHz

// FREQOUT: timebase / n, timebase 20 MHz, 10 MHz or 100 kHz, n = 1..16.
struct FreqOutSetting {
    double frequency = 0;
    double timebase = 0;
    int divisor = 1;
};
// Every distinct FREQOUT frequency, highest first (each with the fastest timebase producing it).
const std::vector<FreqOutSetting>& freqOutTable();
FreqOutSetting coerceFreqOut(double requested);   // nearest entry of the table

// Welford running statistics (min, max, mean, standard deviation) over finite values.
class RunningStats
{
public:
    void add(double value);
    void reset() { *this = RunningStats(); }
    int64_t count() const { return m_count; }
    double minimum() const { return m_count ? m_min : std::numeric_limits<double>::quiet_NaN(); }
    double maximum() const { return m_count ? m_max : std::numeric_limits<double>::quiet_NaN(); }
    double mean() const { return m_count ? m_mean : std::numeric_limits<double>::quiet_NaN(); }
    // Sample standard deviation (n - 1); NaN below two values.
    double standardDeviation() const;

private:
    int64_t m_count = 0;
    double m_mean = 0;
    double m_m2 = 0;
    double m_min = 0;
    double m_max = 0;
};

// Follows a wrapping 32-bit edge count as a signed 64-bit count: each reading is compared with
// the previous one (difference taken modulo 2^32 as a signed 32-bit step), so counting down
// past 0 gives -1 instead of 4294967295.
class CountUnwrapper
{
public:
    explicit CountUnwrapper(double initial = 0) { reset(initial); }
    void reset(double initial)
    {
        m_raw = initial;
        m_value = initial;
    }
    double add(double raw);

private:
    double m_raw = 0;
    double m_value = 0;
};

// Rate of change of a counted value (edges/s, encoder velocity) from successive readings,
// updated once at least `window` seconds separate two readings.
class RateEstimator
{
public:
    explicit RateEstimator(double window = 0.5) : m_window(window) {}
    // Returns the current estimate (NaN until two readings `window` apart exist).
    double add(double time, double value);
    void reset();

private:
    double m_window;
    bool m_hasReference = false;
    double m_time = 0;
    double m_value = 0;
    double m_rate = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace mybench::instruments::ctr
