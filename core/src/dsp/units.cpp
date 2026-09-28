#include "mydaq/dsp/units.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace mydaq::dsp {

namespace {

constexpr int kMinExponent = -15;
constexpr int kMaxExponent = 12;

QString prefixFor(int exponent)
{
    switch (exponent) {
    case -15: return QStringLiteral("f");
    case -12: return QStringLiteral("p");
    case -9: return QStringLiteral("n");
    case -6: return QStringLiteral("µ");
    case -3: return QStringLiteral("m");
    case 3: return QStringLiteral("k");
    case 6: return QStringLiteral("M");
    case 9: return QStringLiteral("G");
    case 12: return QStringLiteral("T");
    default: return {};
    }
}

// Multiplier of a one-character prefix, 0 if `c` is not a prefix.
double prefixFactor(QChar c)
{
    switch (c.unicode()) {
    case 'f': return 1e-15;
    case 'p': return 1e-12;
    case 'n': return 1e-9;
    case 'u':
    case 0x00B5:   // micro sign
    case 0x03BC:   // greek small mu
        return 1e-6;
    case 'm': return 1e-3;
    case 'k':
    case 'K': return 1e3;
    case 'M': return 1e6;
    case 'G': return 1e9;
    case 'T': return 1e12;
    default: return 0.0;
    }
}

QString joinUnit(const QString& number, const QString& prefix, const QString& unit)
{
    if (prefix.isEmpty() && unit.isEmpty())
        return number;
    return number + QLatin1Char(' ') + prefix + unit;
}

bool isUnitText(const QString& s)
{
    return std::none_of(s.begin(), s.end(), [](QChar c) { return c.isDigit() || c.isSpace(); });
}

}  // namespace

QString formatEng(double value, const QString& unit, int significantDigits, bool trimZeros)
{
    const int digits = std::clamp(significantDigits, 1, 15);
    if (!std::isfinite(value))
        return joinUnit(QStringLiteral("---"), {}, unit);
    if (value == 0.0)
        return joinUnit(trimZeros ? QStringLiteral("0") : QString::number(0.0, 'f', digits - 1), {}, unit);

    const double magnitude = std::abs(value);
    int exponent = int(std::floor(std::log10(magnitude) / 3.0)) * 3;
    exponent = std::clamp(exponent, kMinExponent, kMaxExponent);
    const double mantissa = value / std::pow(10.0, exponent);
    int integerDigits = int(std::floor(std::log10(std::abs(mantissa)))) + 1;
    if (exponent > kMinExponent)
        integerDigits = std::max(integerDigits, 1);   // log10 rounding just below a power of 1000
    QString number = QString::number(mantissa, 'f', std::max(0, digits - integerDigits));
    // Rounding can carry into the next prefix (999.96 -> 1000.0 -> 1.000 k).
    if (std::abs(number.toDouble()) >= 1000.0 && exponent < kMaxExponent) {
        exponent += 3;
        number = QString::number(value / std::pow(10.0, exponent), 'f', digits - 1);
    }
    if (trimZeros && number.contains(QLatin1Char('.'))) {
        while (number.endsWith(QLatin1Char('0')))
            number.chop(1);
        if (number.endsWith(QLatin1Char('.')))
            number.chop(1);
    }
    return joinUnit(number, prefixFor(exponent), unit);
}

std::optional<double> parseEng(const QString& text, const QString& unit)
{
    QString s = text.trimmed();
    if (!s.contains(QLatin1Char('.')))
        s.replace(QLatin1Char(','), QLatin1Char('.'));
    static const QRegularExpression pattern(
        QStringLiteral("^([+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s*(.*)$"));
    const QRegularExpressionMatch match = pattern.match(s);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double number = match.captured(1).toDouble(&ok);
    if (!ok)
        return std::nullopt;
    QString rest = match.captured(2).trimmed();

    if (!unit.isEmpty()) {
        QStringList names{unit};
        if (unit == QStringLiteral("Ω"))
            names << QStringLiteral("ohms") << QStringLiteral("ohm") << QStringLiteral("Ω");
        for (const QString& name : names) {
            if (rest.endsWith(name, Qt::CaseInsensitive)) {
                rest.chop(name.size());
                rest = rest.trimmed();
                break;
            }
        }
        if (rest.isEmpty())
            return number;
        if (rest.size() == 1 && prefixFactor(rest[0]) != 0.0)
            return number * prefixFactor(rest[0]);
        return std::nullopt;
    }

    if (rest.isEmpty())
        return number;
    if (!isUnitText(rest))
        return std::nullopt;
    const double factor = prefixFactor(rest[0]);
    // A lone letter or a letter followed by a unit: "m", "mV". Anything else is just a unit ("Hz").
    return factor != 0.0 ? number * factor : number;
}

}  // namespace mydaq::dsp
