#include "mybench/plot/scale.h"

#include <QDateTime>

#include <algorithm>
#include <array>

namespace mybench::plot {

namespace {

constexpr double kEps = 1e-9;

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

int engineeringExponent(double absValue)
{
    if (!(absValue > 0) || !std::isfinite(absValue))
        return 0;
    const int e = int(std::floor(std::log10(absValue) + kEps));
    int e3 = int(std::floor(double(e) / 3.0)) * 3;
    return std::clamp(e3, -15, 12);
}

double cleanZero(double v, double step)
{
    return std::abs(v) < std::abs(step) * 1e-9 ? 0.0 : v;
}

int decimalsForStep(double scaledStep)
{
    if (!(scaledStep > 0))
        return 0;
    // Smallest number of decimals that represents the step exactly (1-2-5 steps: one digit).
    for (int d = 0; d <= 6; ++d) {
        const double s = scaledStep * std::pow(10.0, d);
        if (std::abs(s - std::round(s)) < 1e-6 * std::max(1.0, s))
            return d;
    }
    return 3;
}

QString appendUnit(QString number, const QString &prefix, const QString &unit)
{
    // Angles are written without a space ("−90°"), everything else with one ("10 kHz").
    if (prefix.isEmpty() && (unit == u"°" || unit == u"′" || unit == u"″"))
        return number + unit;
    if (!prefix.isEmpty() || !unit.isEmpty())
        number += u' ' + prefix + unit;
    return number;
}

void chooseLabelFormat(TickSet &t, double scaleStep)
{
    double maxAbs = 0;
    for (double v : t.major)
        maxAbs = std::max(maxAbs, std::abs(v));
    int e3 = engineeringExponent(maxAbs);
    // Many significant digits (e.g. 1000.0 .. 1000.4): keep plain numbers instead of "1.0002 k".
    if (e3 > 0 && maxAbs / scaleStep > 5e3)
        e3 = 0;
    t.exponent = e3;
    t.decimals = decimalsForStep(scaleStep / std::pow(10.0, e3));
}

}  // namespace

QString typographicMinus(QString text)
{
    return text.replace(u'-', QChar(0x2212));
}

double niceStep(double raw)
{
    if (!(raw > 0) || !std::isfinite(raw))
        return 1.0;
    const double e = std::floor(std::log10(raw));
    const double base = std::pow(10.0, e);
    const double f = raw / base;
    double nice;
    if (f <= 1.0 + kEps)
        nice = 1;
    else if (f <= 2.0 + kEps)
        nice = 2;
    else if (f <= 5.0 + kEps)
        nice = 5;
    else
        nice = 10;
    return nice * base;
}

double niceStepBelow(double raw)
{
    if (!(raw > 0) || !std::isfinite(raw))
        return 1.0;
    const double e = std::floor(std::log10(raw) + kEps);
    const double base = std::pow(10.0, e);
    const double f = raw / base;
    double nice;
    if (f >= 5.0 - kEps)
        nice = 5;
    else if (f >= 2.0 - kEps)
        nice = 2;
    else
        nice = 1;
    return nice * base;
}

TickSet linearTicks(double min, double max, double targetCount, int minorPerMajor)
{
    TickSet t;
    if (!(max > min) || !std::isfinite(min) || !std::isfinite(max))
        return t;
    targetCount = std::max(1.0, targetCount);
    const double step = niceStep((max - min) / targetCount);
    t.step = step;
    const double k0 = std::ceil(min / step - kEps);
    const double k1 = std::floor(max / step + kEps);
    for (double k = k0; k <= k1 && t.major.size() < 10000; k += 1)
        t.major.push_back(cleanZero(k * step, step));

    int m = minorPerMajor;
    if (m < 0) {
        const double mantissa = step / std::pow(10.0, std::floor(std::log10(step) + kEps));
        m = (std::abs(mantissa - 2.0) < 1e-6) ? 4 : 5;
    }
    if (m > 1) {
        const double minorStep = step / m;
        const double j0 = std::ceil(min / minorStep - kEps);
        const double j1 = std::floor(max / minorStep + kEps);
        for (double j = j0; j <= j1 && t.minor.size() < 50000; j += 1) {
            const long long ji = (long long)j;
            if (((ji % m) + m) % m == 0)
                continue;
            t.minor.push_back(cleanZero(j * minorStep, minorStep));
        }
    }
    chooseLabelFormat(t, step);
    return t;
}

TickSet divisionTicks(double min, double max, int divisions, int minorPerDivision)
{
    TickSet t;
    if (!(max > min) || divisions < 1)
        return t;
    const double step = (max - min) / divisions;
    t.step = step;
    for (int i = 0; i <= divisions; ++i)
        t.major.push_back(cleanZero(min + step * i, step));
    if (minorPerDivision > 1) {
        const double minorStep = step / minorPerDivision;
        for (int i = 0; i < divisions; ++i)
            for (int j = 1; j < minorPerDivision; ++j)
                t.minor.push_back(cleanZero(min + step * i + minorStep * j, minorStep));
    }
    chooseLabelFormat(t, step);
    t.decimals = std::min(t.decimals, 3);
    return t;
}

TickSet logTicks(double min, double max, double pixelLength, double minMajorSpacingPx)
{
    TickSet t;
    if (!(min > 0) || !(max > min) || !std::isfinite(max))
        return t;
    pixelLength = std::max(1.0, std::abs(pixelLength));
    const double lmin = std::log10(min), lmax = std::log10(max);
    const double decades = lmax - lmin;
    if (decades < 0.7) {
        // Less than a decade visible: nice linear steps (positions stay logarithmic).
        t = linearTicks(min, max, std::max(2.0, pixelLength / minMajorSpacingPx));
        return t;
    }
    t.logDecades = true;
    const double ppd = pixelLength / decades;  // pixels per decade
    const int d0 = int(std::floor(lmin + kEps));
    const int d1 = int(std::ceil(lmax - kEps));
    auto inRange = [&](double v) { return v >= min * (1 - 1e-9) && v <= max * (1 + 1e-9); };

    if (ppd >= 3.2 * minMajorSpacingPx) {
        // Plenty of room: label 1, 2, 5 of every decade.
        t.step = 1;
        for (int d = d0; d <= d1; ++d) {
            const double base = std::pow(10.0, d);
            for (int m = 1; m <= 9; ++m) {
                const double v = m * base;
                if (!inRange(v))
                    continue;
                (m == 1 || m == 2 || m == 5 ? t.major : t.minor).push_back(v);
            }
        }
    } else {
        int every = 1;
        if (ppd < minMajorSpacingPx) {
            static constexpr std::array<int, 6> choices{1, 2, 3, 5, 10, 20};
            every = choices.back();
            for (int c : choices) {
                if (ppd * c >= minMajorSpacingPx) {
                    every = c;
                    break;
                }
            }
        }
        t.step = every;
        const bool subDecadeMinors = ppd >= 18;
        for (int d = d0; d <= d1; ++d) {
            const double base = std::pow(10.0, d);
            if (inRange(base))
                (((d % every) + every) % every == 0 ? t.major : t.minor).push_back(base);
            if (subDecadeMinors) {
                for (int m = 2; m <= 9; ++m) {
                    const double v = m * base;
                    if (inRange(v))
                        t.minor.push_back(v);
                }
            }
        }
        std::sort(t.minor.begin(), t.minor.end());
    }
    return t;
}

TickSet timeTicks(double min, double max, double targetCount)
{
    TickSet t;
    if (!(max > min))
        return t;
    static constexpr std::array<double, 31> steps{
        0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
        1, 2, 5, 10, 15, 30,
        60, 120, 300, 600, 900, 1800,
        3600, 7200, 10800, 21600, 43200,
        86400, 172800, 604800, 2592000, 31536000};
    const double raw = (max - min) / std::max(1.0, targetCount);
    double step = steps.back();
    for (double s : steps) {
        if (s >= raw * (1 - 1e-9)) {
            step = s;
            break;
        }
    }
    t.step = step;
    // Align to local wall-clock multiples of the step.
    const double offset = QDateTime::fromMSecsSinceEpoch(qint64(min * 1000)).offsetFromUtc();
    const double k0 = std::ceil((min + offset) / step - kEps);
    const double k1 = std::floor((max + offset) / step + kEps);
    for (double k = k0; k <= k1 && t.major.size() < 10000; k += 1)
        t.major.push_back(k * step - offset);
    int m = 5;
    if (step == 2 || step == 120 || step == 7200 || step == 0.002 || step == 0.02 || step == 0.2)
        m = 4;
    else if (step == 15 || step == 30 || step == 900 || step == 1800 || step == 10800)
        m = 3;
    else if (step == 21600 || step == 43200)
        m = 6;
    else if (step == 604800)
        m = 7;
    const double minorStep = step / m;
    const double j0 = std::ceil((min + offset) / minorStep - kEps);
    const double j1 = std::floor((max + offset) / minorStep + kEps);
    for (double j = j0; j <= j1 && t.minor.size() < 50000; j += 1) {
        const long long ji = (long long)j;
        if (((ji % m) + m) % m == 0)
            continue;
        t.minor.push_back(j * minorStep - offset);
    }
    t.decimals = step < 1 ? decimalsForStep(step) : 0;
    return t;
}

QString formatTick(double value, const TickSet &ticks, ScaleType scale, LabelStyle style,
                   const QString &unit, int fixedDecimals)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");
    if (style == LabelStyle::Time) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(qint64(std::llround(value * 1000.0)));
        const double step = ticks.step;
        QString format;
        if (step >= 86400)
            format = QStringLiteral("d MMM");
        else if (step >= 60)
            format = QStringLiteral("HH:mm");
        else if (step >= 1)
            format = QStringLiteral("HH:mm:ss");
        else
            format = QStringLiteral("HH:mm:ss.zzz");
        return dt.toString(format);
    }
    if (style == LabelStyle::Fixed) {
        const int d = fixedDecimals >= 0 ? fixedDecimals : decimalsForStep(ticks.step);
        return typographicMinus(appendUnit(QString::number(value, 'f', d), {}, unit));
    }
    if (value == 0.0)
        return appendUnit(QStringLiteral("0"), {}, unit);

    if (scale == ScaleType::Log) {
        const int e3 = engineeringExponent(std::abs(value));
        const double mantissa = value / std::pow(10.0, e3);
        QString number;
        if (ticks.logDecades) {
            number = QString::number(mantissa, 'g', 3);
        } else {
            const double scaledStep = ticks.step / std::pow(10.0, e3);
            number = QString::number(mantissa, 'f', decimalsForStep(scaledStep));
        }
        return typographicMinus(appendUnit(number, prefixFor(e3), unit));
    }
    const double scaled = value / std::pow(10.0, ticks.exponent);
    QString number = QString::number(scaled, 'f', ticks.decimals);
    if (number == u"-0" || (number.startsWith(u"-0.") && number.count(u'0') == number.size() - 2))
        number.remove(0, 1);
    return typographicMinus(appendUnit(number, prefixFor(ticks.exponent), unit));
}

QString formatEngineering(double value, const QString &unit, int digits)
{
    if (!std::isfinite(value))
        return unit.isEmpty() ? QStringLiteral("—") : QStringLiteral("— ") + unit;
    digits = std::clamp(digits, 1, 12);
    if (value == 0.0)
        return appendUnit(QString::number(0.0, 'f', digits - 1), {}, unit);
    int e3 = engineeringExponent(std::abs(value));
    double mantissa = value / std::pow(10.0, e3);
    int intDigits = int(std::floor(std::log10(std::abs(mantissa)) + kEps)) + 1;
    int decimals = std::max(0, digits - intDigits);
    // Rounding can carry into the next prefix (999.96 -> 1000.0).
    const double rounded = std::round(std::abs(mantissa) * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
    if (rounded >= 1000.0 && e3 < 12) {
        e3 += 3;
        mantissa = value / std::pow(10.0, e3);
        intDigits = 1;
        decimals = std::max(0, digits - 1);
    }
    return typographicMinus(appendUnit(QString::number(mantissa, 'f', decimals), prefixFor(e3), unit));
}

}  // namespace mybench::plot
