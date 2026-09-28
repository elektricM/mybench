#include "dsaformat.h"

#include <mydaq/dsp/units.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace mybench::instruments::dsa {

namespace dsp = mydaq::dsp;

namespace {

constexpr double kFloorDb = -400.0;
constexpr double kDbStep = 10.0;
constexpr double kMaxDbSpan = 160.0;
constexpr double kMinDbSpan = 20.0;

double toDb(double ratio)
{
    return ratio > 0.0 ? std::max(10.0 * std::log10(ratio), kFloorDb) : kFloorDb;
}

// Smallest 1-2-5 × 10^k value >= v (v > 0).
double niceCeil(double v)
{
    const double decade = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 5.0, 10.0}) {
        if (m * decade >= v * (1.0 - 1e-12))
            return m * decade;
    }
    return 10.0 * decade;
}

}  // namespace

bool isLogarithmic(dsp::SpectrumUnit unit)
{
    return unit != dsp::SpectrumUnit::Linear;
}

QString magnitudeUnit(const dsp::SpectrumFormat& format)
{
    const bool peak = format.mode == dsp::SpectrumMode::Peak;
    switch (format.unit) {
    case dsp::SpectrumUnit::Db:
        return (peak ? u"dBVpk"_s : u"dBVrms"_s) + (format.density ? u"/√Hz"_s : QString());
    case dsp::SpectrumUnit::Dbm:
        return format.density ? u"dBm/Hz"_s : u"dBm"_s;
    case dsp::SpectrumUnit::Linear:
        return (peak ? u"Vpk"_s : u"Vrms"_s) + (format.density ? u"/√Hz"_s : QString());
    }
    return {};
}

QString powerUnit(dsp::SpectrumUnit unit, dsp::SpectrumMode mode)
{
    const bool peak = mode == dsp::SpectrumMode::Peak;
    switch (unit) {
    case dsp::SpectrumUnit::Db: return peak ? u"dBVpk"_s : u"dBVrms"_s;
    case dsp::SpectrumUnit::Dbm: return u"dBm"_s;
    case dsp::SpectrumUnit::Linear: return peak ? u"Vpk²"_s : u"Vrms²"_s;
    }
    return {};
}

double powerValue(double powerVrms2, dsp::SpectrumUnit unit, dsp::SpectrumMode mode)
{
    if (!std::isfinite(powerVrms2) || powerVrms2 < 0.0)
        powerVrms2 = 0.0;
    const double p = mode == dsp::SpectrumMode::Peak ? 2.0 * powerVrms2 : powerVrms2;
    switch (unit) {
    case dsp::SpectrumUnit::Db: return toDb(p);
    case dsp::SpectrumUnit::Dbm: return toDb(p / 50.0 / 1e-3);
    case dsp::SpectrumUnit::Linear: return p;
    }
    return p;
}

double levelValue(double powerVrms2, dsp::SpectrumUnit unit, dsp::SpectrumMode mode)
{
    const double p = powerValue(powerVrms2, unit, mode);
    return unit == dsp::SpectrumUnit::Linear ? std::sqrt(std::max(p, 0.0)) : p;
}

double relativeDb(double power, double reference)
{
    if (!(power > 0.0) || !(reference > 0.0) || !std::isfinite(power) || !std::isfinite(reference))
        return 0.0;
    return std::max(10.0 * std::log10(power / reference), kFloorDb);
}

QString fixedText(double value, int decimals)
{
    if (!std::isfinite(value))
        value = 0.0;
    QString text = QString::number(value, 'f', decimals);
    // "-0.00" reads as a sign glitch.
    if (text.startsWith(u'-') && text.mid(1).toDouble() == 0.0)
        text.remove(0, 1);
    if (text.startsWith(u'-'))
        text.replace(0, 1, QChar(0x2212));
    return text;
}

QString levelText(double value, const QString& unit, bool logarithmic)
{
    if (!std::isfinite(value))
        value = 0.0;
    if (logarithmic)
        return fixedText(value, 2) + u' ' + unit;
    if (unit.endsWith(u'²')) {
        // A power in V²: SI prefixes would be ambiguous (mV² ≠ 1e-3 V²).
        if (value == 0.0 || std::abs(value) >= 0.01)
            return fixedText(value, 3) + u' ' + unit;
        return QString::number(value, 'e', 2).replace(u'-', QChar(0x2212)) + u' ' + unit;
    }
    return dsp::formatEng(value, unit, 4, false).replace(u'-', QChar(0x2212));
}

ValueRange defaultManualRange(dsp::SpectrumUnit unit)
{
    return unit == dsp::SpectrumUnit::Linear ? ValueRange{0.0, 1.0} : ValueRange{-120.0, 10.0};
}

ValueRange autoRange(std::span<const double> values, dsp::SpectrumUnit unit, const std::optional<ValueRange>& previous)
{
    const bool logarithmic = isLogarithmic(unit);
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double v : values) {
        if (!std::isfinite(v) || (logarithmic && v <= kFloorDb + 1.0))
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (!(hi >= lo))
        return previous.value_or(defaultManualRange(unit));

    ValueRange r;
    if (logarithmic) {
        r.maximum = std::ceil((hi + 2.0) / kDbStep) * kDbStep;
        r.minimum = std::floor((lo - 2.0) / kDbStep) * kDbStep;
        if (previous) {
            if (r.maximum < previous->maximum && previous->maximum - r.maximum <= kDbStep)
                r.maximum = previous->maximum;
            if (r.minimum > previous->minimum && r.minimum - previous->minimum <= kDbStep)
                r.minimum = previous->minimum;
        }
        r.minimum = std::max(r.minimum, r.maximum - kMaxDbSpan);
        r.minimum = std::min(r.minimum, r.maximum - kMinDbSpan);
        return r;
    }

    const double top = hi > 0.0 ? niceCeil(hi * 1.05) : 1.0;
    const double bottom = lo < 0.0 ? -niceCeil(-lo * 1.05) : 0.0;
    r = {bottom, top};
    if (previous && previous->maximum > 0.0) {
        // Shrink only when the data dropped well below the previous top.
        if (r.maximum < previous->maximum && hi * 1.05 > 0.4 * previous->maximum)
            r.maximum = previous->maximum;
        if (r.minimum > previous->minimum && previous->minimum < 0.0 && -lo * 1.05 > -0.4 * previous->minimum)
            r.minimum = previous->minimum;
    }
    return r;
}

}  // namespace mybench::instruments::dsa
