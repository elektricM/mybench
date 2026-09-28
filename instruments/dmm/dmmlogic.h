// Pure Multimeter logic (no Qt objects, no device): range tables, ELVISmx autoranging, 3½-digit
// display formats, overload limits, statistics and the log file format. Kept separate from the
// controller so it can be unit-tested (instruments/dmm/tests/).
//
// Sources: docs/instruments/algorithms/dmm.md (ELVISmx behaviour recovered from the binaries,
// authoritative) and docs/instruments/dmm.md (functional spec).
#pragma once

#include <mydaq/device.h>

#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace mybench::instruments::dmm {

// Continuity: |R| below this closes the circuit (tone, indicator, algorithms doc 5).
inline constexpr double kContinuityThreshold = 15.0;
// Minimum period between two displayed readings in Run Continuously (spec 4.1).
inline constexpr int kMinimumReadingPeriodMs = 200;
// At most this many readings to settle the autoranger before a reading is displayed (spec 4.2).
inline constexpr int kMaximumAutoRangeReadings = 6;

// The myDAQ range table (largest first, as Device::dmmRanges() lists it), used when no device
// is available to ask.
std::vector<double> defaultRanges(mydaq::DmmFunction function);

// How a reading on a range is printed (algorithms doc 4): value / divisor with `decimals`
// decimals, followed by `unit` (prefix + base unit, e.g. "mV", "kΩ").
struct DisplayFormat {
    double divisor = 1.0;
    int decimals = 3;
    QString unit;

    // Smallest step of the displayed number, in base units.
    double resolution() const { return divisor * std::pow(10.0, -decimals); }
};

DisplayFormat displayFormat(mydaq::DmmFunction function, double rangeMax);

// ELVISmx-style range label without a space: "60V", "200mV", "1A", "20mA", "20MΩ", "200Ω".
QString rangeLabel(mydaq::DmmFunction function, double rangeMax);

// "Upper range" of the range (algorithms doc 2): a reading whose magnitude is above it is an
// overload. 2000-count ranges: 1.999, 19.99, 199.9 ...; 60 V: 59.99; 1 A: 0.999; Continuity:
// 199.99 Ω; Diode: 1.999 V.
double overloadLimit(mydaq::DmmFunction function, double rangeMax);

// Overload test of one reading: the device flag, or |value| above the range's upper limit.
bool isOverload(mydaq::DmmFunction function, double rangeMax, const mydaq::DmmReading& reading);

// ELVISmx software autoranging (algorithms doc 3): the range for the next reading, as an index
// into `ranges` (largest first), from the magnitude of the last reading taken on
// ranges[currentIndex]. No hysteresis: the smallest range whose upper limit is above the value;
// Resistance jumps from 20 MΩ straight to 20 kΩ below 10 kΩ. An overload counts as at least the
// full scale of the range it was measured on, so the ranger climbs. Diode and Continuity keep
// their single range.
int autoRangeIndex(mydaq::DmmFunction function, const std::vector<double>& ranges, int currentIndex,
                   double magnitude, bool overload);

// Number text of a value in a format, with the value rounded to the format's resolution:
// no leading zeros, "-" only for negative values ("-0.0" is printed "0.0"). Example: 1.2345 on
// the 2 V range -> "1.235" (unit "V" from the format).
QString formatNumber(double value, const DisplayFormat& format);

// Rounds a base-unit value to the resolution of the format.
double roundToResolution(double value, const DisplayFormat& format);

// Overload text (spec 4.3): "+OVER" for either sign, like ELVISmx's "+Over"; with `signedOverload`
// (MYBENCH option) a negative DC reading shows "-OVER".
QString overloadText(mydaq::DmmFunction function, double value, bool signedOverload = false);

// Functions whose value may be negative (bipolar % FS bar, "-Over").
bool isBipolar(mydaq::DmmFunction function);

// Running statistics of the displayed readings (Welford): overloads are counted separately.
class Statistics
{
public:
    void reset() { *this = Statistics(); }
    void add(double value);
    void addOverload() { ++m_overloads; }

    int64_t count() const { return m_count; }
    int64_t overloads() const { return m_overloads; }
    double minimum() const { return m_count ? m_min : qNaN(); }
    double maximum() const { return m_count ? m_max : qNaN(); }
    double mean() const { return m_count ? m_mean : qNaN(); }
    // Sample standard deviation (n - 1); 0 for a single reading.
    double standardDeviation() const;

private:
    static double qNaN() { return std::numeric_limits<double>::quiet_NaN(); }
    int64_t m_count = 0;
    int64_t m_overloads = 0;
    double m_min = 0;
    double m_max = 0;
    double m_mean = 0;
    double m_m2 = 0;
};

// One row of the reading log (spec "Logging").
struct LogRow {
    QString time;          // ISO 8601 with milliseconds and offset
    double elapsed = 0;    // s since the first row of the log
    QString function;      // "DC Voltage"
    QString range;         // "2V" (Auto: the range the reading was taken on)
    double value = 0;      // displayed value in base units (relative when Null is on)
    QString unit;          // base unit ("V", "A", "Ω")
    bool overload = false;
    bool relative = false;
    double nullValue = 0;  // base units, only meaningful when relative
};

// Column header line of the log, fields separated by `separator` (tab: like the ELVISmx Data
// Logger; comma: CSV).
QString logHeader(QChar separator);
// One data line (no newline). Values use '.' and up to 10 significant digits; the value cell is
// empty for an overload; the null cell is empty when not relative.
QString logLine(const LogRow& row, QChar separator);

}  // namespace mybench::instruments::dmm
