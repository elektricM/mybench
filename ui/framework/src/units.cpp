#include "mybench/units.h"

#include <mydaq/dsp/units.h>

#include <QJSEngine>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace mybench {

Units::Units(QObject* parent) : QObject(parent) {}

Units* Units::instance()
{
    static Units* const units = new Units;
    return units;
}

Units* Units::create(QQmlEngine*, QJSEngine*)
{
    Units* units = instance();
    QJSEngine::setObjectOwnership(units, QJSEngine::CppOwnership);
    return units;
}

QString Units::format(double value, const QString& unit, int digits, bool trimZeros)
{
    return mydaq::dsp::formatEng(value, unit, std::clamp(digits, 1, 15), trimZeros);
}

QString Units::fixed(double value, const QString& unit, int decimals)
{
    const QString number = std::isfinite(value) ? QString::number(value, 'f', std::clamp(decimals, 0, 12)) : u"---"_s;
    return unit.isEmpty() ? number : number + u' ' + unit;
}

double Units::parse(const QString& text, const QString& unit)
{
    const std::optional<double> value = mydaq::dsp::parseEng(text, unit);
    return value ? *value : std::numeric_limits<double>::quiet_NaN();
}

bool Units::isValid(const QString& text, const QString& unit)
{
    return mydaq::dsp::parseEng(text, unit).has_value();
}

double Units::prefixMultiplier(const QString& prefixAndUnit, const QString& unit)
{
    const QString trimmed = prefixAndUnit.trimmed();
    if (trimmed.isEmpty() || trimmed == unit)
        return 1.0;
    return parse(u"1"_s + trimmed, unit);
}

QString Units::decibels(double value, int decimals)
{
    if (std::isnan(value))
        return u"--- dB"_s;
    if (std::isinf(value))
        return value > 0 ? u"Inf dB"_s : u"-Inf dB"_s;
    return QString::number(value, 'f', std::clamp(decimals, 0, 6)) + u" dB"_s;
}

double Units::clamp(double value, double from, double to)
{
    if (!std::isnan(from) && value < from)
        value = from;
    if (!std::isnan(to) && value > to)
        value = to;
    return value;
}

}  // namespace mybench
