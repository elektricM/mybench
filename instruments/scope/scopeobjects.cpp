#include "scopeobjects.h"

#include <mybench/settingsjson.h>

#include <QMetaProperty>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

template <typename T, typename V>
bool assign(T& member, V value)
{
    if (member == value)
        return false;
    member = value;
    return true;
}

bool sameDouble(double a, double b)
{
    return a == b || (std::isnan(a) && std::isnan(b));
}

QList<QMetaProperty> settingProperties(const QObject* object)
{
    QList<QMetaProperty> list;
    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
        const QMetaProperty p = meta->property(i);
        if (p.isWritable() && p.isStored() && !p.isConstant() && p.hasNotifySignal())
            list.append(p);
    }
    return list;
}

}  // namespace

QJsonObject saveProperties(const QObject* object)
{
    return json::readProperties(object, settingProperties(object));
}

void restoreProperties(QObject* object, const QJsonObject& json)
{
    json::writeProperties(object, settingProperties(object), json);
}

// --- ScopeTraceStyle ---------------------------------------------------------------------------

void ScopeTraceStyle::setLineColor(const QString& color)
{
    if (assign(m_lineColor, color))
        emit changed();
}

void ScopeTraceStyle::setLineWidth(double width)
{
    if (assign(m_lineWidth, std::clamp(width, 0.5, 5.0)))
        emit changed();
}

void ScopeTraceStyle::setLineStyle(LineStyle style)
{
    if (assign(m_lineStyle, style))
        emit changed();
}

void ScopeTraceStyle::setPoints(bool on)
{
    if (assign(m_points, on))
        emit changed();
}

void ScopeTraceStyle::setInterpolation(Interpolation interpolation)
{
    if (assign(m_interpolation, interpolation))
        emit changed();
}

void ScopeTraceStyle::setFillTo(FillTo fill)
{
    if (assign(m_fillTo, fill))
        emit changed();
}

void ScopeTraceStyle::setFillColor(const QString& color)
{
    if (assign(m_fillColor, color))
        emit changed();
}

void ScopeTraceStyle::reset()
{
    setLineColor(QString());
    setLineWidth(1.5);
    setLineStyle(Solid);
    setPoints(false);
    setInterpolation(Linear);
    setFillTo(NoFill);
    setFillColor(QString());
}

// --- ScopeChannel ------------------------------------------------------------------------------

ScopeChannel::ScopeChannel(int index, Source source, bool enabled, QObject* parent)
    : QObject(parent), m_index(index), m_source(source), m_enabled(enabled)
{
    connect(this, &ScopeChannel::measurementMaskChanged, this, &ScopeChannel::readoutsChanged);
    connect(this, &ScopeChannel::resultsChanged, this, &ScopeChannel::readoutsChanged);
    for (auto signal : {&ScopeChannel::sourceChanged, &ScopeChannel::voltsPerDivChanged, &ScopeChannel::inputRangeChanged})
        connect(this, signal, this, &ScopeChannel::rangeChanged);
}

void ScopeChannel::setSource(Source source)
{
    if (!assign(m_source, source))
        return;
    // Audio sources only go up to 1 V/div (ELVISmx trims the list).
    const auto& values = scope::voltsPerDivValues(audio());
    if (m_voltsPerDiv > values.back() * 1.0001) {
        m_voltsPerDiv = values.back();
        emit voltsPerDivChanged();
    }
    emit sourceChanged();
}

void ScopeChannel::setEnabled(bool enabled)
{
    if (assign(m_enabled, enabled)) {
        if (!enabled)
            clearResults();
        emit enabledChanged();
    }
}

void ScopeChannel::setVoltsPerDiv(double voltsPerDiv)
{
    if (!(voltsPerDiv > 0))
        return;
    const auto& values = scope::voltsPerDivValues(audio());
    const double snapped = values[size_t(scope::nearestIndex(values, voltsPerDiv))];
    if (assign(m_voltsPerDiv, snapped))
        emit voltsPerDivChanged();
}

void ScopeChannel::setPosition(double divisions)
{
    if (!std::isfinite(divisions))
        return;
    const double v = std::round(std::clamp(divisions, -5.0, 5.0) * 100.0) / 100.0;
    if (assign(m_position, v))
        emit positionChanged();
}

void ScopeChannel::setInputRange(InputRange range)
{
    if (assign(m_inputRange, range))
        emit inputRangeChanged();
}

void ScopeChannel::setDisplayMeasurements(bool on)
{
    if (assign(m_displayMeasurements, on))
        emit displayMeasurementsChanged();
}

void ScopeChannel::setMeasurementMask(int mask)
{
    if (assign(m_measurementMask, mask & 0x3FFF))
        emit measurementMaskChanged();
}

void ScopeChannel::setShown(int measurement, bool on)
{
    setMeasurementMask(on ? (m_measurementMask | measurement) : (m_measurementMask & ~measurement));
}

QList<double> ScopeChannel::scaleValues() const
{
    const auto& values = scope::voltsPerDivValues(audio());
    return QList<double>(values.begin(), values.end());
}

QStringList ScopeChannel::scaleLabels() const
{
    QStringList labels;
    for (double v : scope::voltsPerDivValues(audio()))
        labels.append(scope::divisionLabel(v, u"V"_s));
    return labels;
}

int ScopeChannel::scaleIndex() const
{
    return scope::nearestIndex(scope::voltsPerDivValues(audio()), m_voltsPerDiv);
}

void ScopeChannel::setScaleIndex(int index)
{
    const auto& values = scope::voltsPerDivValues(audio());
    setVoltsPerDiv(values[size_t(std::clamp(index, 0, int(values.size()) - 1))]);
}

QString ScopeChannel::scaleLabel() const
{
    return scope::divisionLabel(m_voltsPerDiv, u"V"_s);
}

void ScopeChannel::setResults(const scope::Measurements& results)
{
    m_results = results;
    m_hasResults = true;
    emit resultsChanged();
}

void ScopeChannel::clearResults()
{
    m_results = {};
    m_hasResults = false;
    emit resultsChanged();
}

QString ScopeChannel::measurementLabel(Measurement m)
{
    switch (m) {
    case Rms: return tr("RMS");
    case Frequency: return tr("Freq");
    case PeakToPeak: return tr("Vp-p");
    case Mean: return tr("Mean");
    case Minimum: return tr("Min");
    case Maximum: return tr("Max");
    case Amplitude: return tr("Ampl");
    case Period: return tr("Period");
    case CounterFrequency: return tr("Freq (count)");
    case DutyCycle: return tr("Duty");
    case RiseTime: return tr("Rise");
    case FallTime: return tr("Fall");
    case PositiveWidth: return tr("+Width");
    case NegativeWidth: return tr("−Width");
    }
    return {};
}

QString ScopeChannel::measurementUnit(Measurement m)
{
    switch (m) {
    case Frequency:
    case CounterFrequency: return u"Hz"_s;
    case Period:
    case RiseTime:
    case FallTime:
    case PositiveWidth:
    case NegativeWidth: return u"s"_s;
    case DutyCycle: return u"%"_s;
    default: return u"V"_s;
    }
}

double ScopeChannel::measurementValue(const scope::Measurements& r, Measurement m)
{
    switch (m) {
    case Rms: return r.rms;
    case Frequency: return r.frequency;
    case PeakToPeak: return r.peakToPeak;
    case Mean: return r.mean;
    case Minimum: return r.minimum;
    case Maximum: return r.maximum;
    case Amplitude: return r.amplitude;
    case Period: return r.period;
    case CounterFrequency: return r.counterFrequency;
    case DutyCycle: return r.dutyCycle;
    case RiseTime: return r.riseTime;
    case FallTime: return r.fallTime;
    case PositiveWidth: return r.positiveWidth;
    case NegativeWidth: return r.negativeWidth;
    }
    return scope::kNaN;
}

QVariantList ScopeChannel::measurementCatalog()
{
    QVariantList list;
    for (int bit = 0; bit < 14; ++bit) {
        const auto m = Measurement(1 << bit);
        list.append(QVariantMap{{u"flag"_s, int(m)}, {u"label"_s, measurementLabel(m)},
                                {u"extension"_s, (int(m) & kElvisMeasurements) == 0}});
    }
    return list;
}

QVariantList ScopeChannel::readouts() const
{
    QVariantList list;
    for (int bit = 0; bit < 14; ++bit) {
        const auto m = Measurement(1 << bit);
        if (!(m_measurementMask & m))
            continue;
        QString text;
        if (!m_hasResults) {
            // ELVISmx before the first record: "?" except Vp-p "0.00 V".
            text = m == PeakToPeak ? scope::formatMeasurement(0.0, u"V"_s) : u"?"_s;
        } else if (m == DutyCycle) {
            const double d = m_results.dutyCycle;
            text = std::isfinite(d) ? QString::number(d * 100.0, 'f', 1) + u" %"_s : u"?"_s;
        } else {
            text = scope::formatMeasurement(measurementValue(m_results, m), measurementUnit(m));
        }
        list.append(QVariantMap{{u"key"_s, int(m)}, {u"label"_s, measurementLabel(m)}, {u"text"_s, text}});
    }
    return list;
}

QJsonObject ScopeChannel::save() const
{
    QJsonObject json = saveProperties(this);
    json.insert(u"style"_s, saveProperties(&m_style));
    return json;
}

void ScopeChannel::restore(const QJsonObject& json)
{
    // Source first: it bounds Volts/Div.
    if (json.contains(u"source"_s))
        restoreProperties(this, QJsonObject{{u"source"_s, json.value(u"source"_s)}});
    restoreProperties(this, json);
    if (json.contains(u"style"_s))
        restoreProperties(&m_style, json.value(u"style"_s).toObject());
}

// --- ScopeMath ---------------------------------------------------------------------------------

ScopeMath::ScopeMath(QObject* parent) : QObject(parent) {}

void ScopeMath::setEnabled(bool on)
{
    if (assign(m_enabled, on))
        emit enabledChanged();
}

void ScopeMath::setFunction(Function f)
{
    if (assign(m_function, f))
        emit functionChanged();
}

void ScopeMath::setVoltsPerDiv(double v)
{
    if (!(v > 0))
        return;
    const auto values = scaleValues();
    const std::vector<double> list(values.begin(), values.end());
    if (assign(m_voltsPerDiv, list[size_t(scope::nearestIndex(list, v))]))
        emit voltsPerDivChanged();
}

void ScopeMath::setPosition(double divisions)
{
    if (!std::isfinite(divisions))
        return;
    if (assign(m_position, std::round(std::clamp(divisions, -5.0, 5.0) * 100.0) / 100.0))
        emit positionChanged();
}

QString ScopeMath::unit() const
{
    if (fft())
        return u"dBV"_s;
    return m_function == Product ? u"V²"_s : u"V"_s;
}

QList<double> ScopeMath::scaleValues() const
{
    QList<double> values;
    for (double decade = 1e-3; decade < 100; decade *= 10)
        for (double m : {1.0, 2.0, 5.0})
            values.append(m * decade);
    return values;   // 1 m … 50 per division
}

QStringList ScopeMath::scaleLabels() const
{
    QStringList labels;
    const QString u = m_function == Product ? u"V²"_s : u"V"_s;
    for (double v : scaleValues())
        labels.append(scope::divisionLabel(v, u));
    return labels;
}

int ScopeMath::scaleIndex() const
{
    const auto values = scaleValues();
    return scope::nearestIndex(std::vector<double>(values.begin(), values.end()), m_voltsPerDiv);
}

void ScopeMath::setScaleIndex(int index)
{
    const auto values = scaleValues();
    setVoltsPerDiv(values.value(std::clamp(index, 0, int(values.size()) - 1)));
}

void ScopeMath::setResults(double rms, double vpp, double peakFrequency, double peakLevel)
{
    if (sameDouble(rms, m_rms) && sameDouble(vpp, m_peakToPeak) && sameDouble(peakFrequency, m_peakFrequency)
        && sameDouble(peakLevel, m_peakLevel))
        return;
    m_rms = rms;
    m_peakToPeak = vpp;
    m_peakFrequency = peakFrequency;
    m_peakLevel = peakLevel;
    emit resultsChanged();
}

QJsonObject ScopeMath::save() const
{
    QJsonObject json = saveProperties(this);
    json.insert(u"style"_s, saveProperties(&m_style));
    return json;
}

void ScopeMath::restore(const QJsonObject& json)
{
    restoreProperties(this, json);
    if (json.contains(u"style"_s))
        restoreProperties(&m_style, json.value(u"style"_s).toObject());
}

// --- ScopeCursor -------------------------------------------------------------------------------

ScopeCursor::ScopeCursor(const QString& label, int channel, QObject* parent)
    : QObject(parent), m_label(label), m_channel(channel), m_defaultChannel(channel)
{
}

void ScopeCursor::setChannel(int channel)
{
    if (assign(m_channel, std::clamp(channel, 0, 2)))
        emit channelChanged();
}

void ScopeCursor::setPosition(double seconds)
{
    if (!sameDouble(m_position, seconds)) {
        m_position = seconds;
        emit positionChanged();
    }
}

void ScopeCursor::setColor(const QString& color)
{
    if (assign(m_color, color))
        emit styleChanged();
}

void ScopeCursor::setLineStyle(ScopeTraceStyle::LineStyle style)
{
    if (assign(m_lineStyle, style))
        emit styleChanged();
}

void ScopeCursor::setCursorStyle(CursorStyle style)
{
    if (assign(m_cursorStyle, style))
        emit styleChanged();
}

void ScopeCursor::setShowValue(bool on)
{
    if (assign(m_showValue, on))
        emit styleChanged();
}

void ScopeCursor::resetStyle()
{
    setColor(QString());
    setLineStyle(ScopeTraceStyle::Dash);
    setCursorStyle(VerticalLine);
    setShowValue(true);
    setChannel(m_defaultChannel);
}

QJsonObject ScopeCursor::save() const
{
    return saveProperties(this);
}

void ScopeCursor::restore(const QJsonObject& json)
{
    if (!json.contains(u"position"_s))
        setPosition(scope::kNaN);
    restoreProperties(this, json);
}

}  // namespace mybench::instruments
