#include "mybench/plot/tracestyle.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mybench::plot {

namespace {

constexpr std::array kLineStyles{"solid", "dash", "dot", "dashDot", "dashDotDot"};
constexpr std::array kPointStyles{"none",        "circle",        "filledCircle", "square", "filledSquare",
                                  "diamond",     "filledDiamond", "cross",        "plus",   "x"};
constexpr std::array kInterpolations{"pointsOnly", "linear",     "step",
                                     "stepVertical", "centerStep", "centerStepVertical"};
constexpr std::array kFills{"none", "zero", "minusInfinity", "plusInfinity"};

template <size_t N>
int indexOf(const std::array<const char *, N> &names, const QVariant &v, int fallback)
{
    if (!v.isValid())
        return fallback;
    const QString s = v.toString();
    for (size_t i = 0; i < N; ++i) {
        if (s == QLatin1StringView(names[i]))
            return int(i);
    }
    return fallback;
}

QString colorName(const QColor &c)
{
    if (!c.isValid())
        return QString();
    return c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
}

QColor parseColor(const QVariant &v, const QColor &fallback)
{
    if (!v.isValid())
        return fallback;
    const QString s = v.toString();
    if (s.isEmpty())
        return QColor();
    const QColor c = QColor::fromString(s);
    return c.isValid() ? c : QColor();
}

}  // namespace

TraceStyle::TraceStyle(QObject *parent) : QObject(parent) {}

void TraceStyle::assign(const Values &v)
{
    if (m_value == v)
        return;
    m_value = v;
    emit changed();
}

void TraceStyle::setLineColor(const QColor &c)
{
    Values v = m_value;
    v.lineColor = c;
    assign(v);
}

void TraceStyle::setFillColor(const QColor &c)
{
    Values v = m_value;
    v.fillColor = c;
    assign(v);
}

void TraceStyle::setLineWidth(double w)
{
    Values v = m_value;
    v.lineWidth = std::isfinite(w) ? std::clamp(w, 0.0, 10.0) : 1.5;
    assign(v);
}

void TraceStyle::setLineStyle(LineStyle s)
{
    Values v = m_value;
    v.lineStyle = LineStyle(std::clamp(int(s), 0, int(DashDotDot)));
    assign(v);
}

void TraceStyle::setPointStyle(PointStyle s)
{
    Values v = m_value;
    v.pointStyle = PointStyle(std::clamp(int(s), 0, int(X)));
    assign(v);
}

void TraceStyle::setInterpolation(Interpolation i)
{
    Values v = m_value;
    v.interpolation = Interpolation(std::clamp(int(i), 0, int(CenterStepVertical)));
    assign(v);
}

void TraceStyle::setFillTo(FillTo f)
{
    Values v = m_value;
    v.fillTo = FillTo(std::clamp(int(f), 0, int(PlusInfinity)));
    assign(v);
}

void TraceStyle::reset() { assign(m_default); }

void TraceStyle::setAsDefault()
{
    if (m_default == m_value)
        return;
    m_default = m_value;
    emit changed();  // isDefault
}

void TraceStyle::copyFrom(TraceStyle *other)
{
    if (other && other != this)
        assign(other->m_value);
}

QVariantMap TraceStyle::toJson() const
{
    return {
        {QStringLiteral("lineColor"), colorName(m_value.lineColor)},
        {QStringLiteral("fillColor"), colorName(m_value.fillColor)},
        {QStringLiteral("lineWidth"), m_value.lineWidth},
        {QStringLiteral("lineStyle"), QString::fromLatin1(kLineStyles[size_t(m_value.lineStyle)])},
        {QStringLiteral("pointStyle"), QString::fromLatin1(kPointStyles[size_t(m_value.pointStyle)])},
        {QStringLiteral("interpolation"), QString::fromLatin1(kInterpolations[size_t(m_value.interpolation)])},
        {QStringLiteral("fillTo"), QString::fromLatin1(kFills[size_t(m_value.fillTo)])},
    };
}

void TraceStyle::fromJson(const QVariantMap &json)
{
    Values v = m_default;
    v.lineColor = parseColor(json.value(QStringLiteral("lineColor")), m_default.lineColor);
    v.fillColor = parseColor(json.value(QStringLiteral("fillColor")), m_default.fillColor);
    const QVariant width = json.value(QStringLiteral("lineWidth"));
    bool ok = false;
    const double w = width.toDouble(&ok);
    if (width.isValid() && ok && std::isfinite(w))
        v.lineWidth = std::clamp(w, 0.0, 10.0);
    v.lineStyle = LineStyle(indexOf(kLineStyles, json.value(QStringLiteral("lineStyle")), m_default.lineStyle));
    v.pointStyle = PointStyle(indexOf(kPointStyles, json.value(QStringLiteral("pointStyle")), m_default.pointStyle));
    v.interpolation =
        Interpolation(indexOf(kInterpolations, json.value(QStringLiteral("interpolation")), m_default.interpolation));
    v.fillTo = FillTo(indexOf(kFills, json.value(QStringLiteral("fillTo")), m_default.fillTo));
    assign(v);
}

}  // namespace mybench::plot
