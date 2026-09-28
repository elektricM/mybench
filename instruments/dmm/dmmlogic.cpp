#include "dmmlogic.h"

#include <QStringList>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mybench::instruments::dmm {

namespace {

bool nearly(double a, double b)
{
    return std::abs(a - b) <= 1e-9 * std::max(std::abs(a), std::abs(b));
}

// Engineering exponent (multiple of 3) that puts the full scale in [1, 1000).
int engineeringExponent(double rangeMax)
{
    return int(std::floor(std::log10(rangeMax) / 3.0 + 1e-9)) * 3;
}

QString prefixOf(int exponent)
{
    switch (exponent) {
    case 9: return u"G"_s;
    case 6: return u"M"_s;
    case 3: return u"k"_s;
    case 0: return {};
    case -3: return u"m"_s;
    case -6: return u"µ"_s;
    case -9: return u"n"_s;
    default: return u"e%1 "_s.arg(exponent);
    }
}

// Leading digit of the full scale (2 for 200 mV, 6 for 60 V, 1 for 1 A).
int leadingDigit(double rangeMax)
{
    const double mantissa = rangeMax / std::pow(10.0, std::floor(std::log10(rangeMax) + 1e-9));
    return std::clamp(int(std::floor(mantissa + 1e-9)), 1, 9);
}

QString numberText(double value)
{
    if (!std::isfinite(value))
        return {};
    return QString::number(value, 'g', 10);
}

}  // namespace

std::vector<double> defaultRanges(mydaq::DmmFunction function)
{
    using F = mydaq::DmmFunction;
    switch (function) {
    case F::DcVoltage: return {60.0, 20.0, 2.0, 0.2};
    case F::AcVoltage: return {20.0, 2.0, 0.2};
    case F::DcCurrent:
    case F::AcCurrent: return {1.0, 0.2, 0.02};
    case F::Resistance: return {20e6, 2e6, 200e3, 20e3, 2e3, 200.0};
    case F::Diode: return {2.0};
    case F::Continuity: return {200.0};
    }
    return {};
}

DisplayFormat displayFormat(mydaq::DmmFunction function, double rangeMax)
{
    DisplayFormat format;
    const QString base = mydaq::unitOf(function);
    if (!(rangeMax > 0)) {
        format.unit = base;
        return format;
    }
    const int exponent = engineeringExponent(rangeMax);
    const double scaled = rangeMax / std::pow(10.0, exponent);
    const int integerDigits = int(std::floor(std::log10(scaled) + 1e-9)) + 1;
    // 3½ digits: "1999"/"1.999"-count ranges (leading digit 1 or 2) show 4 digits, the 60 V
    // range 3 ("59.9"). Matches the ELVISmx table (%3.1f on 60 V, %2.2f on 20 V, %1.3f on 1 A ...).
    const int digits = leadingDigit(rangeMax) <= 2 ? 4 : 3;
    format.divisor = std::pow(10.0, exponent);
    format.decimals = std::max(0, digits - integerDigits);
    format.unit = prefixOf(exponent) + base;
    return format;
}

QString rangeLabel(mydaq::DmmFunction function, double rangeMax)
{
    if (!(rangeMax > 0))
        return {};
    const int exponent = engineeringExponent(rangeMax);
    const double scaled = rangeMax / std::pow(10.0, exponent);
    return QString::number(scaled, 'g', 4) + prefixOf(exponent) + mydaq::unitOf(function);
}

double overloadLimit(mydaq::DmmFunction function, double rangeMax)
{
    if (function == mydaq::DmmFunction::Continuity)
        return 199.99;
    if (!(rangeMax > 0))
        return 0;
    // One count below full scale of a 1999 / 5999 / 999 count display.
    return rangeMax * (1.0 - 1.0 / (1000.0 * leadingDigit(rangeMax)));
}

bool isOverload(mydaq::DmmFunction function, double rangeMax, const mydaq::DmmReading& reading)
{
    return reading.overload || !std::isfinite(reading.value)
           || std::abs(reading.value) > overloadLimit(function, rangeMax) * (1.0 + 1e-9);
}

int autoRangeIndex(mydaq::DmmFunction function, const std::vector<double>& ranges, int currentIndex,
                   double magnitude, bool overload)
{
    const int count = int(ranges.size());
    if (count == 0)
        return 0;
    currentIndex = std::clamp(currentIndex, 0, count - 1);
    if (count == 1 || function == mydaq::DmmFunction::Diode || function == mydaq::DmmFunction::Continuity)
        return currentIndex;

    double m = std::isfinite(magnitude) ? std::abs(magnitude) : std::numeric_limits<double>::infinity();
    if (overload)
        m = std::max(m, ranges[size_t(currentIndex)]);

    if (function == mydaq::DmmFunction::Resistance && nearly(ranges[size_t(currentIndex)], 20e6) && m < 10e3) {
        for (int i = 0; i < count; ++i) {
            if (nearly(ranges[size_t(i)], 20e3))
                return i;
        }
    }
    for (int i = count - 1; i >= 0; --i) {   // smallest first
        const double limit = overloadLimit(function, ranges[size_t(i)]);
        if (m < limit * (1.0 - 1e-9))   // ">= limit" climbs; tolerant of 19.990000000000002
            return i;
    }
    return 0;   // largest
}

double roundToResolution(double value, const DisplayFormat& format)
{
    if (!std::isfinite(value))
        return value;
    const double scale = std::pow(10.0, format.decimals);
    const double rounded = std::round(value / format.divisor * scale) / scale;
    return (rounded == 0.0 ? 0.0 : rounded) * format.divisor;   // no negative zero
}

QString formatNumber(double value, const DisplayFormat& format)
{
    if (!std::isfinite(value))
        return u"---"_s;
    const double scale = std::pow(10.0, format.decimals);
    double rounded = std::round(value / format.divisor * scale) / scale;
    if (rounded == 0.0)
        rounded = 0.0;   // "-0.0" -> "0.0"
    return QString::number(rounded, 'f', format.decimals);
}

QString overloadText(mydaq::DmmFunction function, double value)
{
    return isBipolar(function) && value < 0 ? u"-Over"_s : u"+Over"_s;
}

bool isBipolar(mydaq::DmmFunction function)
{
    return function == mydaq::DmmFunction::DcVoltage || function == mydaq::DmmFunction::DcCurrent;
}

void Statistics::add(double value)
{
    if (!std::isfinite(value))
        return;
    ++m_count;
    if (m_count == 1) {
        m_min = m_max = m_mean = value;
        m_m2 = 0;
        return;
    }
    m_min = std::min(m_min, value);
    m_max = std::max(m_max, value);
    const double delta = value - m_mean;
    m_mean += delta / double(m_count);
    m_m2 += delta * (value - m_mean);
}

double Statistics::standardDeviation() const
{
    if (m_count == 0)
        return qNaN();
    if (m_count == 1)
        return 0.0;
    return std::sqrt(std::max(0.0, m_m2 / double(m_count - 1)));
}

QString logHeader(QChar separator)
{
    return QStringList{u"Time (ISO 8601)"_s, u"Elapsed (s)"_s, u"Function"_s, u"Range"_s, u"Value"_s,
                       u"Unit"_s, u"Overload"_s, u"Relative"_s, u"Null value"_s}
        .join(separator);
}

QString logLine(const LogRow& row, QChar separator)
{
    // Text cells never contain the separators we use (tab, comma); keep them safe anyway.
    auto text = [separator](QString cell) { return cell.replace(separator, u' '); };
    return QStringList{text(row.time),
                       QString::number(row.elapsed, 'f', 3),
                       text(row.function),
                       text(row.range),
                       row.overload ? QString() : numberText(row.value),
                       text(row.unit),
                       row.overload ? u"1"_s : u"0"_s,
                       row.relative ? u"1"_s : u"0"_s,
                       row.relative ? numberText(row.nullValue) : QString()}
        .join(separator);
}

}  // namespace mybench::instruments::dmm
