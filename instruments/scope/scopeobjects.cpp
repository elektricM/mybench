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

// --- Trace styles ------------------------------------------------------------------------------

void initTraceStyle(plot::TraceStyle& style, plot::TraceStyle::LineStyle lineStyle, double width)
{
    style.setLineWidth(width);
    style.setLineStyle(lineStyle);
    style.setAsDefault();
}

QVariantMap traceStyleJson(const QJsonObject& json)
{
    QVariantMap map = json.toVariantMap();
    // Older versions stored enum key names ("Dash", "FilledCircle", "NoPoints", "NoFill"); the
    // shared TraceStyle uses lower camel case and "none".
    for (const QString& key : {u"lineStyle"_s, u"pointStyle"_s, u"interpolation"_s, u"fillTo"_s}) {
        QString value = map.value(key).toString();
        if (value.isEmpty() || !value.front().isUpper())
            continue;
        if (value == u"NoPoints"_s || value == u"NoFill"_s)
            value = u"none"_s;
        else
            value.front() = value.front().toLower();
        map.insert(key, value);
    }
    map.remove(u"points"_s);
    return map;
}

// --- ScopeChannel ------------------------------------------------------------------------------

ScopeChannel::ScopeChannel(int index, Source source, bool enabled, QObject* parent)
    : QObject(parent), m_index(index), m_source(source), m_enabled(enabled)
{
    initTraceStyle(m_style, plot::TraceStyle::Solid, 1.0);   // ELVISmx: 1 px solid
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

void ScopeChannel::setMeasurementMask(int mask)
{
    if (assign(m_measurementMask, mask & kAllMeasurements))
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

void ScopeChannel::setFrequencyByEdges(bool on)
{
    if (assign(m_frequencyByEdges, on))
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
    // Row titles of the measurement strip (sentence case, short).
    switch (m) {
    case Rms: return tr("RMS");
    case Frequency: return tr("Frequency");
    case PeakToPeak: return tr("Peak-to-peak");
    case Mean: return tr("Mean");
    case Minimum: return tr("Minimum");
    case Maximum: return tr("Maximum");
    case Amplitude: return tr("Amplitude");
    case Period: return tr("Period");
    case CounterFrequency: return tr("Frequency (edges)");
    case DutyCycle: return tr("Duty cycle");
    case RiseTime: return tr("Rise time");
    case FallTime: return tr("Fall time");
    case PositiveWidth: return tr("Positive width");
    case NegativeWidth: return tr("Negative width");
    case AcRms: return tr("AC RMS");
    case CrestFactor: return tr("Crest factor");
    case Overshoot: return tr("Overshoot");
    case Preshoot: return tr("Preshoot");
    case SlewRate: return tr("Slew rate");
    case Phase: return tr("Phase");
    case Peak: return tr("Peak");
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
    case DutyCycle:
    case Overshoot:
    case Preshoot: return u"%"_s;
    case CrestFactor: return {};
    case SlewRate: return u"V/s"_s;
    case Phase: return u"°"_s;
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
    case AcRms: return r.acRms;
    case CrestFactor: return r.crestFactor;
    case Overshoot: return r.overshoot;
    case Preshoot: return r.preshoot;
    case SlewRate: return r.slewRate;
    case Phase: return r.phase;
    case Peak: return r.peak;
    }
    return scope::kNaN;
}

QString ScopeChannel::measurementName(Measurement m)
{
    // Entries of the measurement strip's "+" menu (Title Case).
    switch (m) {
    case Rms: return tr("RMS Voltage");
    case Frequency: return tr("Frequency");
    case PeakToPeak: return tr("Peak-to-Peak Voltage");
    case Mean: return tr("Mean");
    case Minimum: return tr("Minimum");
    case Maximum: return tr("Maximum");
    case Amplitude: return tr("Amplitude (High − Low)");
    case Period: return tr("Period");
    case CounterFrequency: return tr("Frequency by Edge Counting");
    case DutyCycle: return tr("Duty Cycle");
    case RiseTime: return tr("Rise Time (10–90 %)");
    case FallTime: return tr("Fall Time (90–10 %)");
    case PositiveWidth: return tr("Positive Pulse Width");
    case NegativeWidth: return tr("Negative Pulse Width");
    case AcRms: return tr("AC RMS (DC Removed)");
    case CrestFactor: return tr("Crest Factor (Peak / RMS)");
    case Overshoot: return tr("Overshoot");
    case Preshoot: return tr("Preshoot");
    case SlewRate: return tr("Slew Rate (Rising)");
    case Phase: return tr("Phase to the Other Channel");
    case Peak: return tr("Peak (Largest |V|)");
    }
    return {};
}

QVariantList ScopeChannel::measurementCatalog()
{
    QVariantList list;
    for (int bit = 0; bit < kMeasurementCount; ++bit) {
        const auto m = Measurement(1 << bit);
        list.append(QVariantMap{{u"flag"_s, int(m)}, {u"label"_s, measurementLabel(m)}, {u"name"_s, measurementName(m)},
                                {u"unit"_s, measurementUnit(m)}, {u"extension"_s, (int(m) & kElvisMeasurements) == 0}});
    }
    return list;
}

double ScopeChannel::measurement(int flag) const
{
    if (!m_hasResults)
        return scope::kNaN;
    const double v = measurementValue(m_results, Measurement(flag));
    return flag == DutyCycle ? v * 100.0 : v;
}

QVariantList ScopeChannel::readouts() const
{
    QVariantList list;
    for (int bit = 0; bit < kMeasurementCount; ++bit) {
        const auto m = Measurement(1 << bit);
        if (!(m_measurementMask & m))
            continue;
        QString text;
        if (!m_hasResults) {
            // ELVISmx before the first record: "?" except Vp-p "0.00 V".
            text = m == PeakToPeak ? scope::formatMeasurement(0.0, u"V"_s) : u"?"_s;
        } else if (m == Frequency) {
            text = scope::formatMeasurement(frequency(), u"Hz"_s);
        } else if (m == DutyCycle) {
            const double d = m_results.dutyCycle;
            text = std::isfinite(d) ? QString::number(d * 100.0, 'f', 1) + u" %"_s : u"?"_s;
        } else if (m == Overshoot || m == Preshoot || m == Phase) {
            const double d = measurementValue(m_results, m);
            text = std::isfinite(d) ? QString::number(d, 'f', 1) + (m == Phase ? u" °"_s : u" %"_s) : u"?"_s;
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
    json.insert(u"style"_s, QJsonObject::fromVariantMap(m_style.toJson()));
    return json;
}

void ScopeChannel::restore(const QJsonObject& json)
{
    // Source first: it bounds Volts/Div.
    if (json.contains(u"source"_s))
        restoreProperties(this, QJsonObject{{u"source"_s, json.value(u"source"_s)}});
    restoreProperties(this, json);   // "displayMeasurements" of older versions is ignored
    if (json.contains(u"style"_s))
        m_style.fromJson(traceStyleJson(json.value(u"style"_s).toObject()));
}

// --- ScopeMath ---------------------------------------------------------------------------------

ScopeMath::ScopeMath(QObject* parent) : QObject(parent)
{
    initTraceStyle(m_style, plot::TraceStyle::Solid, 1.0);
}

void ScopeMath::setEnabled(bool on)
{
    if (assign(m_enabled, on))
        emit enabledChanged();
}

void ScopeMath::setOperation(Operation operation)
{
    if (operation == FftA || operation == FftB)
        return;   // the spectrum is the FFT section's job (ScopeController::fftSource)
    if (assign(m_operation, operation))
        emit operationChanged();
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
    return m_operation == Product ? u"V²"_s : u"V"_s;
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
    const QString u = m_operation == Product ? u"V²"_s : u"V"_s;
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
    json.insert(u"style"_s, QJsonObject::fromVariantMap(m_style.toJson()));
    return json;
}

void ScopeMath::restore(const QJsonObject& json)
{
    restoreProperties(this, json);   // an FFT operation is refused (ScopeController migrates it)
    if (json.contains(u"style"_s))
        m_style.fromJson(traceStyleJson(json.value(u"style"_s).toObject()));
}

// --- ScopeCursor -------------------------------------------------------------------------------

ScopeCursor::ScopeCursor(const QString& label, int channel, QObject* parent)
    : QObject(parent), m_label(label), m_channel(channel), m_defaultChannel(channel)
{
    initTraceStyle(m_style, plot::TraceStyle::Dash, 1.0);   // ELVISmx: dashed, 1 px
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

void ScopeCursor::setCursorStyle(CursorStyle style)
{
    if (assign(m_cursorStyle, style))
        emit cursorStyleChanged();
}

void ScopeCursor::setShowValue(bool on)
{
    if (assign(m_showValue, on))
        emit showValueChanged();
}

void ScopeCursor::resetStyle()
{
    m_style.reset();
    setCursorStyle(VerticalLine);
    setShowValue(true);
    setChannel(m_defaultChannel);
}

QJsonObject ScopeCursor::save() const
{
    QJsonObject json = saveProperties(this);
    json.insert(u"style"_s, QJsonObject::fromVariantMap(m_style.toJson()));
    return json;
}

void ScopeCursor::restore(const QJsonObject& json)
{
    if (!json.contains(u"position"_s))
        setPosition(scope::kNaN);
    restoreProperties(this, json);
    if (json.contains(u"style"_s)) {
        m_style.fromJson(traceStyleJson(json.value(u"style"_s).toObject()));
    } else if (json.contains(u"lineStyle"_s) || json.contains(u"color"_s)) {
        // Older versions kept the cursor look in the cursor itself.
        QJsonObject style{{u"lineColor"_s, json.value(u"color"_s)},
                          {u"lineStyle"_s, json.value(u"lineStyle"_s)},
                          {u"pointStyle"_s, json.value(u"pointStyle"_s)}};
        if (json.contains(u"lineWidth"_s))
            style.insert(u"lineWidth"_s, json.value(u"lineWidth"_s));
        m_style.fromJson(traceStyleJson(style));
    }
}

}  // namespace mybench::instruments
