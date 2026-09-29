#include "mybench/plot/plotaxis.h"

#include <cmath>

#include <algorithm>

namespace mybench::plot {

PlotAxis::PlotAxis(QObject *parent) : QObject(parent) {}

void PlotAxis::applyRange(double min, double max, bool home)
{
    if (!std::isfinite(min) || !std::isfinite(max))
        return;
    const bool changed = min != m_min || max != m_max;
    m_min = min;
    m_max = max;
    if (home) {
        m_homeMin = min;
        m_homeMax = max;
    }
    const bool zoomed = m_min != m_homeMin || m_max != m_homeMax;
    if (changed || zoomed != m_zoomed) {
        m_zoomed = zoomed;
        emit rangeChanged();
    }
}

void PlotAxis::setMin(double v)
{
    if (!std::isfinite(v) || (v == m_homeMin && !m_zoomed))
        return;
    m_homeMin = v;
    applyRange(v, m_homeMax, true);
}

void PlotAxis::setMax(double v)
{
    if (!std::isfinite(v) || (v == m_homeMax && !m_zoomed))
        return;
    m_homeMax = v;
    applyRange(m_homeMin, v, true);
}

void PlotAxis::setRange(double min, double max)
{
    if (min > max)
        std::swap(min, max);
    applyRange(min, max, true);
}

void PlotAxis::clampToLimits(double &min, double &max) const
{
    double span = max - min;
    const double minSpan = std::max(m_minimumSpan, std::abs(max) * 1e-12 + 1e-300);
    if (isLog()) {
        min = std::max(min, 1e-300);
        max = std::max(max, min * (1 + 1e-9));
        if (std::isfinite(m_maximumSpan) && m_maximumSpan > 0 && std::log10(max / min) > m_maximumSpan) {
            const double c = 0.5 * (std::log10(min) + std::log10(max));
            min = std::pow(10.0, c - 0.5 * m_maximumSpan);
            max = std::pow(10.0, c + 0.5 * m_maximumSpan);
        }
    } else {
        if (span < minSpan) {
            const double c = 0.5 * (min + max);
            min = c - 0.5 * minSpan;
            max = c + 0.5 * minSpan;
        }
        if (std::isfinite(m_maximumSpan) && m_maximumSpan > 0 && max - min > m_maximumSpan) {
            const double c = 0.5 * (min + max);
            min = c - 0.5 * m_maximumSpan;
            max = c + 0.5 * m_maximumSpan;
        }
    }
    // Effective limits: the explicit ones, else (limitToData) the data extent plus the margin.
    double lower = m_lowerLimit, upper = m_upperLimit;
    if (m_limitToData && std::isfinite(m_dataLo) && std::isfinite(m_dataHi) && m_dataHi >= m_dataLo) {
        if (isLog() && m_dataLo > 0) {
            const double pad = std::pow(m_dataHi / m_dataLo, m_limitMargin);
            if (!std::isfinite(lower))
                lower = m_dataLo / pad;
            if (!std::isfinite(upper))
                upper = m_dataHi * pad;
        } else if (!isLog()) {
            const double pad = std::max(m_dataHi - m_dataLo, minSpan) * m_limitMargin;
            if (!std::isfinite(lower))
                lower = m_dataLo - pad;
            if (!std::isfinite(upper))
                upper = m_dataHi + pad;
        }
    }
    const bool lo = std::isfinite(lower), hi = std::isfinite(upper);
    if (lo && hi && (upper - lower) <= (max - min)) {
        min = lower;
        max = upper;
        return;
    }
    if (isLog()) {
        if (lo && min < lower) {
            const double f = lower / min;
            min *= f;
            max *= f;
        }
        if (hi && max > upper) {
            const double f = upper / max;
            min *= f;
            max *= f;
        }
        return;
    }
    if (lo && min < lower) {
        max += lower - min;
        min = lower;
    }
    if (hi && max > upper) {
        min -= max - upper;
        max = upper;
    }
}

void PlotAxis::setDataExtent(double lo, double hi)
{
    if (m_dataLo == lo && m_dataHi == hi)
        return;
    m_dataLo = lo;
    m_dataHi = hi;
}

void PlotAxis::setViewRange(double min, double max)
{
    if (!std::isfinite(min) || !std::isfinite(max))
        return;
    if (min > max)
        std::swap(min, max);
    clampToLimits(min, max);
    applyRange(min, max, false);
}

void PlotAxis::reset()
{
    applyRange(m_homeMin, m_homeMax, false);
}

void PlotAxis::zoom(double factor, double anchor)
{
    if (!(factor > 0) || !std::isfinite(factor))
        return;
    if (isLog()) {
        if (!(anchor > 0) || !(m_min > 0))
            return;
        const double la = std::log10(anchor);
        const double lmin = la + (std::log10(m_min) - la) / factor;
        const double lmax = la + (std::log10(m_max) - la) / factor;
        setViewRange(std::pow(10.0, lmin), std::pow(10.0, lmax));
    } else {
        setViewRange(anchor + (m_min - anchor) / factor, anchor + (m_max - anchor) / factor);
    }
    emit viewChangedByUser();
}

void PlotAxis::pan(double fraction)
{
    if (!std::isfinite(fraction))
        return;
    if (isLog()) {
        if (!(m_min > 0))
            return;
        const double d = (std::log10(m_max) - std::log10(m_min)) * fraction;
        const double f = std::pow(10.0, d);
        setViewRange(m_min * f, m_max * f);
    } else {
        const double d = (m_max - m_min) * fraction;
        setViewRange(m_min + d, m_max + d);
    }
    emit viewChangedByUser();
}

void PlotAxis::setScale(Scale s)
{
    if (m_scale == s)
        return;
    m_scale = s;
    emit scaleChanged();
    emit appearanceChanged();
}

#define MYBENCH_AXIS_SETTER(Name, member, signal)                                                  \
    if (member == Name)                                                                            \
        return;                                                                                    \
    member = Name;                                                                                 \
    emit signal();

void PlotAxis::setDivisions(int d) { d = std::max(0, d); MYBENCH_AXIS_SETTER(d, m_divisions, appearanceChanged) }
void PlotAxis::setMinorTicks(int n) { MYBENCH_AXIS_SETTER(n, m_minorTicks, appearanceChanged) }
void PlotAxis::setTickSpacing(double px) { px = std::max(20.0, px); MYBENCH_AXIS_SETTER(px, m_tickSpacing, appearanceChanged) }
void PlotAxis::setUnit(const QString &u) { MYBENCH_AXIS_SETTER(u, m_unit, appearanceChanged) }
void PlotAxis::setTitle(const QString &t) { MYBENCH_AXIS_SETTER(t, m_title, appearanceChanged) }
void PlotAxis::setUnitInLabels(bool on) { MYBENCH_AXIS_SETTER(on, m_unitInLabels, appearanceChanged) }
void PlotAxis::setLabelsVisible(bool on) { MYBENCH_AXIS_SETTER(on, m_labelsVisible, appearanceChanged) }
void PlotAxis::setGridVisible(bool on) { MYBENCH_AXIS_SETTER(on, m_gridVisible, appearanceChanged) }
void PlotAxis::setMinorGridVisible(bool on) { MYBENCH_AXIS_SETTER(on, m_minorGridVisible, appearanceChanged) }
void PlotAxis::setInverted(bool on) { MYBENCH_AXIS_SETTER(on, m_inverted, appearanceChanged) }
void PlotAxis::setLogLabels(LogLabels labels) { MYBENCH_AXIS_SETTER(labels, m_logLabels, appearanceChanged) }
void PlotAxis::setLabelFormat(LabelFormat f) { MYBENCH_AXIS_SETTER(f, m_labelFormat, appearanceChanged) }
void PlotAxis::setDecimals(int d) { MYBENCH_AXIS_SETTER(d, m_decimals, appearanceChanged) }
void PlotAxis::setInteractive(bool on) { MYBENCH_AXIS_SETTER(on, m_interactive, interactiveChanged) }
void PlotAxis::setLowerLimit(double v) { MYBENCH_AXIS_SETTER(v, m_lowerLimit, limitsChanged) }
void PlotAxis::setUpperLimit(double v) { MYBENCH_AXIS_SETTER(v, m_upperLimit, limitsChanged) }
void PlotAxis::setMinimumSpan(double v) { v = std::max(0.0, v); MYBENCH_AXIS_SETTER(v, m_minimumSpan, limitsChanged) }
void PlotAxis::setMaximumSpan(double v) { MYBENCH_AXIS_SETTER(v, m_maximumSpan, limitsChanged) }
void PlotAxis::setLimitToData(bool on) { MYBENCH_AXIS_SETTER(on, m_limitToData, limitsChanged) }
void PlotAxis::setLimitMargin(double f) { f = std::clamp(f, 0.0, 10.0); MYBENCH_AXIS_SETTER(f, m_limitMargin, limitsChanged) }
void PlotAxis::setAutoScale(bool on) { MYBENCH_AXIS_SETTER(on, m_autoScale, autoScaleChanged) }
void PlotAxis::setFollow(bool on) { MYBENCH_AXIS_SETTER(on, m_follow, followChanged) }
void PlotAxis::setAutoScaleMargin(double f) { f = std::clamp(f, 0.0, 1.0); MYBENCH_AXIS_SETTER(f, m_autoScaleMargin, autoScaleChanged) }

#undef MYBENCH_AXIS_SETTER

QString PlotAxis::formatValue(double value, int digits) const
{
    if (m_labelFormat == Time) {
        TickSet t;
        t.step = 0.5;
        return formatTick(value, t, ScaleType::Linear, LabelStyle::Time);
    }
    if (m_labelFormat == Fixed) {
        const int d = m_decimals >= 0 ? m_decimals : 3;
        QString s = QString::number(value, 'f', d);
        if (!m_unit.isEmpty())
            s += u' ' + m_unit;
        return typographicMinus(s);
    }
    return formatEngineering(value, m_unit, digits);
}

double PlotAxis::normalized(double value) const
{
    return map(0, 1).normalized(value);
}

double PlotAxis::fromNormalized(double t) const
{
    return map(0, 1).fromNormalized(t);
}

ScaleMap PlotAxis::map(double start, double length) const
{
    ScaleMap m;
    m.type = isLog() ? ScaleType::Log : ScaleType::Linear;
    m.min = m_min;
    m.max = m_max;
    m.pixelStart = start;
    m.pixelLength = length;
    m.inverted = m_inverted;
    return m;
}

TickSet PlotAxis::ticks(double pixelLength) const
{
    const double len = std::abs(pixelLength);
    if (!(m_max > m_min) || len < 1)
        return {};
    if (m_divisions > 0 && !isLog())
        return divisionTicks(m_min, m_max, m_divisions, m_minorTicks < 0 ? 5 : m_minorTicks);
    if (isLog()) {
        if (!(m_min > 0))
            return {};
        TickSet t = logTicks(m_min, m_max, len, m_tickSpacing * 0.75);
        if (m_logLabels == Decades && t.logDecades) {
            // Only decades are labeled; 2..9 of every decade become minor lines.
            std::vector<double> major, minor;
            const int d0 = int(std::floor(std::log10(m_min) + 1e-9));
            const int d1 = int(std::ceil(std::log10(m_max) - 1e-9));
            for (int d = d0; d <= d1; ++d) {
                const double base = std::pow(10.0, d);
                for (int m = 1; m <= 9; ++m) {
                    const double v = m * base;
                    if (v < m_min * (1 - 1e-9) || v > m_max * (1 + 1e-9))
                        continue;
                    (m == 1 ? major : minor).push_back(v);
                }
            }
            t.major = std::move(major);
            t.minor = std::move(minor);
            t.step = 1;
        }
        return t;
    }
    if (m_labelFormat == Time)
        return timeTicks(m_min, m_max, len / m_tickSpacing);
    return linearTicks(m_min, m_max, len / m_tickSpacing, m_minorTicks);
}

QString PlotAxis::tickLabel(double value, const TickSet &ticks) const
{
    const LabelStyle style = m_labelFormat == Time    ? LabelStyle::Time
                             : m_labelFormat == Fixed ? LabelStyle::Fixed
                                                      : LabelStyle::Engineering;
    return formatTick(value, ticks, isLog() ? ScaleType::Log : ScaleType::Linear, style,
                      m_unitInLabels ? m_unit : QString(), m_decimals);
}

QString PlotAxis::decoratedTitle() const
{
    if (m_title.isEmpty())
        return {};
    if (m_unit.isEmpty() || m_unitInLabels || m_labelFormat == Time)
        return m_title;
    return m_title + QStringLiteral(" (") + m_unit + u')';
}

}  // namespace mybench::plot
