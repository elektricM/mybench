#include "scopecontroller.h"

#include <mybench/dataexport.h>
#include <mybench/devicehub.h>
#include <mybench/notifications.h>

#include <QElapsedTimer>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <span>

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

constexpr double kRollMinimumTimePerDiv = 50e-3;   // roll mode from 50 ms/div (0.5 s screen)
constexpr int kMinimumCycleMs = 33;                // at most 30 records per second

QString rangeText(double range)
{
    return u"±%1 V"_s.arg(range, 0, 'g', 3);
}

}  // namespace

// Everything the worker hands to the UI thread for one displayed record.
struct ScopeController::Published {
    scope::Record record;
    QDateTime startTime;
    double x0 = 0;
    scope::Measurements measurements[2];
    std::vector<double> math;
    double mathRms = scope::kNaN;
    double mathPeakToPeak = scope::kNaN;
    double fftPeakFrequency = scope::kNaN;
    double fftPeakLevel = scope::kNaN;
    double fftBinWidth = 0;
};

ScopeController::ScopeController() : InstrumentController(u"scope"_s)
{
    setRequiredResources(ResourceArbiter::AnalogInput);
    for (QObject* object : std::initializer_list<QObject*>{&m_channel0, &m_channel1, &m_math, &m_cursor1, &m_cursor2,
                                                           m_channel0.style(), m_channel1.style(), m_math.style()})
        watch(object);
    // Settings that change the acquisition plan (rate, record length, roll availability).
    for (auto signal : {&ScopeController::timePerDivChanged, &ScopeController::samplingModeChanged,
                        &ScopeController::rollModeChanged, &ScopeController::triggerTypeChanged})
        connect(this, signal, this, &ScopeController::planChanged);
    for (ScopeChannel* c : {&m_channel0, &m_channel1}) {
        connect(c, &ScopeChannel::sourceChanged, this, &ScopeController::planChanged);
        connect(c, &ScopeChannel::enabledChanged, this, &ScopeController::planChanged);
    }
    connect(this, &InstrumentController::settingsChanged, this, [this] {
        if (isRunning())
            m_live->post(snapshot());
    });
    connect(this, &ScopeController::timePerDivChanged, this, &ScopeController::clampCursors);
    setSummary({});
}

ScopeController::~ScopeController()
{
    shutdown();   // the worker lambda captures `this`
}

void ScopeController::watch(QObject* object)
{
    const QMetaMethod slot = staticMetaObject.method(staticMetaObject.indexOfSlot("onSettingChanged()"));
    const QMetaObject* meta = object->metaObject();
    QList<int> connected;
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
        const QMetaProperty p = meta->property(i);
        if (p.isWritable() && p.isStored() && p.hasNotifySignal() && !connected.contains(p.notifySignalIndex())) {
            connected.append(p.notifySignalIndex());
            connect(object, p.notifySignal(), this, slot);
        }
    }
}

void ScopeController::onSettingChanged()
{
    markSettingsChanged();   // also posts the live snapshot (settingsChanged)
}

// --- Settings ----------------------------------------------------------------------------------

void ScopeController::setTimePerDiv(double seconds)
{
    if (!(seconds > 0))
        return;
    const auto& values = scope::timePerDivValues();
    if (assign(m_timePerDiv, values[size_t(scope::nearestIndex(values, seconds))]))
        emit timePerDivChanged();
}

void ScopeController::setSamplingMode(SamplingMode mode)
{
    if (assign(m_samplingMode, mode))
        emit samplingModeChanged();
}

void ScopeController::setRollMode(bool on)
{
    if (assign(m_rollMode, on))
        emit rollModeChanged();
}

void ScopeController::setTriggerType(TriggerType type)
{
    if (assign(m_triggerType, type)) {
        fixTriggerSource();
        emit triggerTypeChanged();
    }
}

void ScopeController::setTriggerSource(TriggerSource source)
{
    if (assign(m_triggerSource, source))
        emit triggerSourceChanged();
}

void ScopeController::setTriggerSlope(Slope slope)
{
    if (assign(m_triggerSlope, slope))
        emit triggerSlopeChanged();
}

void ScopeController::setTriggerLevel(double volts)
{
    if (!std::isfinite(volts))
        return;
    // ELVISmx: -100 … +100 V, increment 0.1 (values in between are kept, rounded to 0.1 mV).
    if (assign(m_triggerLevel, std::round(std::clamp(volts, -100.0, 100.0) * 1e4) / 1e4))
        emit triggerLevelChanged();
}

void ScopeController::setHorizontalPosition(double percent)
{
    if (!std::isfinite(percent))
        return;
    if (assign(m_horizontalPosition, std::round(std::clamp(percent, 0.0, 100.0) * 10.0) / 10.0))
        emit horizontalPositionChanged();
}

void ScopeController::setTriggerHysteresis(double divisions)
{
    if (!std::isfinite(divisions))
        return;
    if (assign(m_triggerHysteresis, std::round(std::clamp(divisions, 0.0, 2.0) * 100.0) / 100.0))
        emit triggerHysteresisChanged();
}

void ScopeController::setNoTriggerMode(NoTriggerMode mode)
{
    if (assign(m_noTriggerMode, mode))
        emit noTriggerModeChanged();
}

void ScopeController::setFrequencyMethod(FrequencyMethod method)
{
    if (!assign(m_frequencyMethod, method))
        return;
    m_channel0.setFrequencyByEdges(method == EdgeFrequency);
    m_channel1.setFrequencyByEdges(method == EdgeFrequency);
    emit frequencyMethodChanged();
}

void ScopeController::setAutoscaleSetsLevel(bool on)
{
    if (assign(m_autoscaleSetsLevel, on))
        emit autoscaleSetsLevelChanged();
}

void ScopeController::setCursorsOn(bool on)
{
    if (!assign(m_cursorsOn, on))
        return;
    if (on && (std::isnan(m_cursor1.position()) || std::isnan(m_cursor2.position())))
        resetCursors();
    emit cursorsOnChanged();
}

void ScopeController::setShowDeltaTime(bool on)
{
    if (assign(m_showDeltaTime, on))
        emit showDeltaTimeChanged();
}

void ScopeController::setDisplayMode(DisplayMode mode)
{
    if (assign(m_displayMode, mode))
        emit displayModeChanged();
}

void ScopeController::setPersistence(int ms)
{
    if (assign(m_persistence, std::clamp(ms, 0, 10000)))
        emit persistenceChanged();
}

void ScopeController::setBackground(Background background)
{
    if (assign(m_background, background))
        emit backgroundChanged();
}

void ScopeController::resetCursors()
{
    m_cursor1.setPosition(0.25 * screenTime());
    m_cursor2.setPosition(0.75 * screenTime());
}

void ScopeController::clampCursors()
{
    for (ScopeCursor* c : {&m_cursor1, &m_cursor2})
        if (std::isfinite(c->position()) && c->position() > screenTime())
            c->setPosition(c == &m_cursor1 ? 0.25 * screenTime() : 0.75 * screenTime());
}

void ScopeController::fixTriggerSource()
{
    // "Chan N Source" needs channel N enabled; otherwise use the other channel.
    const int n = m_triggerSource == Chan1Source ? 1 : 0;
    if (!channel(n)->enabled() && channel(1 - n)->enabled())
        setTriggerSource(n == 0 ? Chan1Source : Chan0Source);
}

// --- Derived values ----------------------------------------------------------------------------

QList<double> ScopeController::timeDivValues() const
{
    const auto& values = scope::timePerDivValues();
    return QList<double>(values.begin(), values.end());
}

QStringList ScopeController::timeDivLabels() const
{
    QStringList labels;
    for (double v : scope::timePerDivValues())
        labels.append(scope::divisionLabel(v, u"s"_s));
    return labels;
}

int ScopeController::timeDivIndex() const
{
    return scope::nearestIndex(scope::timePerDivValues(), m_timePerDiv);
}

void ScopeController::setTimeDivIndex(int index)
{
    const auto& values = scope::timePerDivValues();
    setTimePerDiv(values[size_t(std::clamp(index, 0, int(values.size()) - 1))]);
}

QString ScopeController::timeDivLabel() const
{
    return scope::divisionLabel(m_timePerDiv, u"s"_s);
}

double ScopeController::maxRate() const
{
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    return device ? device->maxAiRate() : 200000.0;
}

double ScopeController::plannedRate() const
{
    const double rate = scope::planRate(m_timePerDiv, maxRate(), scope::SamplingMode(int(m_samplingMode))).rate;
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    return device ? device->coerceAiRate(rate) : rate;
}

int ScopeController::plannedRecordLength() const
{
    return int(scope::planRate(m_timePerDiv, maxRate(), scope::SamplingMode(int(m_samplingMode))).samples);
}

bool ScopeController::rollAvailable() const
{
    return m_timePerDiv >= kRollMinimumTimePerDiv * 0.999;
}

int ScopeController::recordLength() const
{
    for (int ch = 0; ch < 2; ++ch)
        if (m_record.enabled(ch))
            return int(m_record.data[ch].size());
    return 0;
}

double ScopeController::triggerTime() const
{
    if (!m_record.triggered || !(m_record.sampleRate > 0))
        return scope::kNaN;
    return m_recordX0 + double(m_record.triggerIndex) / m_record.sampleRate;
}

scope::Snapshot ScopeController::snapshot() const
{
    scope::Snapshot s;
    s.channel[0] = m_channel0.settings();
    s.channel[1] = m_channel1.settings();
    s.timePerDiv = m_timePerDiv;
    s.sampling = scope::SamplingMode(int(m_samplingMode));
    s.roll = rolling();
    s.triggerType = m_triggerType == Edge ? scope::TriggerType::Edge : scope::TriggerType::Immediate;
    s.triggerSource = m_triggerSource == Chan1Source ? scope::TriggerSource::Chan1 : scope::TriggerSource::Chan0;
    s.slope = m_triggerSlope == Falling ? scope::Slope::Falling : scope::Slope::Rising;
    s.level = m_triggerLevel;
    s.horizontalPercent = m_horizontalPosition;
    s.hysteresisDiv = m_triggerHysteresis;
    s.noTrigger = m_noTriggerMode == ShowUntriggered ? scope::NoTrigger::Auto
                : m_noTriggerMode == KeepLastTrace   ? scope::NoTrigger::KeepLast
                                                     : scope::NoTrigger::Clear;
    s.mathEnabled = m_math.enabled();
    s.mathFunction = m_math.coreFunction();
    return s;
}

// --- UI actions --------------------------------------------------------------------------------

bool ScopeController::setChannelSource(int index, int source)
{
    if (index < 0 || index > 1 || source < 0 || source > 3)
        return false;
    ScopeChannel* c = channel(index);
    ScopeChannel* other = channel(1 - index);
    const bool audio = scope::isAudio(scope::Source(source));
    (audio ? m_lastAudioSource : m_lastAiSource)[index] = source;
    c->setSource(ScopeChannel::Source(source));
    if (other->audio() != audio) {
        // AI and audio inputs share the converters: the other channel follows to the same kind
        // (its last choice of that kind; audio default: the other side of the jack).
        int follow = (audio ? m_lastAudioSource : m_lastAiSource)[1 - index];
        if (follow < 0)
            follow = source == ScopeChannel::AudioLeft ? ScopeChannel::AudioRight : ScopeChannel::AudioLeft;
        other->setSource(ScopeChannel::Source(follow));
        Notifications::instance()->notify(Notifications::Info, title(),
            tr("%1 switched to %2 (AI and audio inputs cannot be mixed)").arg(other->name(), other->sourceName()));
    }
    fixTriggerSource();
    return true;
}

bool ScopeController::setChannelEnabled(int index, bool enabled)
{
    if (index < 0 || index > 1)
        return false;
    ScopeChannel* c = channel(index);
    const ScopeChannel* other = channel(1 - index);
    if (isRunning() && !enabled && !other->enabled()) {
        Notifications::instance()->notify(Notifications::Warning, title(), tr("Enable at least one channel."));
        return false;
    }
    c->setEnabled(enabled);
    if (!enabled)
        (index == 0 ? m_ch0Trace : m_ch1Trace).clear();
    fixTriggerSource();
    return true;
}

void ScopeController::autoscale()
{
    const QString problem = scope::validate(snapshot());
    if (!problem.isEmpty()) {
        Notifications::instance()->notify(Notifications::Warning, title(), problem);
        return;
    }
    setAutoscaling(true);
    if (isRunning()) {
        m_autoscaleRequest->store(true);
        return;
    }
    m_autoscaleOnStart = true;
    runOnce();
    if (!isRunning()) {   // refused (no device...)
        m_autoscaleOnStart = false;
        setAutoscaling(false);
    }
}

void ScopeController::applyAutoscale(const scope::AutoscaleResult& result)
{
    for (int ch = 0; ch < 2; ++ch) {
        ScopeChannel* c = channel(ch);
        if (c->enabled() && std::isfinite(result.channel[ch].voltsPerDiv))
            c->setVoltsPerDiv(result.channel[ch].voltsPerDiv);
    }
    if (std::isfinite(result.timePerDiv))
        setTimePerDiv(result.timePerDiv);
    if (m_autoscaleSetsLevel && m_triggerType == Edge) {
        const int tc = m_triggerSource == Chan1Source ? 1 : 0;
        if (std::isfinite(result.channel[tc].midLevel))
            setTriggerLevel(std::round(result.channel[tc].midLevel * 1000.0) / 1000.0);
    }
    m_live->post(snapshot());   // the worker waits for this before its next record
    setAutoscaling(false);
}

void ScopeController::setAutoscaling(bool on)
{
    if (assign(m_autoscaling, on))
        emit autoscalingChanged();
}

void ScopeController::setWaitingForTrigger(bool waiting)
{
    if (assign(m_waitingForTrigger, waiting))
        emit waitingForTriggerChanged();
}

QString ScopeController::formatMeasurement(double value, const QString& unit) const
{
    return scope::formatMeasurement(value, unit);
}

QString ScopeController::formatRate(double samplesPerSecond) const
{
    return samplesPerSecond > 0 ? scope::formatFixed(samplesPerSecond, u"S/s"_s, 2) : QString();
}

double ScopeController::valueAt(int index, double seconds) const
{
    const std::vector<double>* data = nullptr;
    double skew = 0;
    if (index == 2) {
        data = &m_mathRecord;
    } else if (index == 0 || index == 1) {
        data = &m_record.data[index];
        skew = m_record.skew[index];
    }
    if (!data || data->empty() || !(m_record.sampleRate > 0) || !std::isfinite(seconds))
        return scope::kNaN;
    const double i = std::round((seconds - m_recordX0 - skew) * m_record.sampleRate);
    if (i < 0 || i >= double(data->size()))
        return scope::kNaN;
    return (*data)[size_t(i)];
}

// --- References --------------------------------------------------------------------------------

void ScopeController::setRefAVisible(bool on)
{
    if (assign(m_ref[0].visible, on))
        emit referencesChanged();
}

void ScopeController::setRefBVisible(bool on)
{
    if (assign(m_ref[1].visible, on))
        emit referencesChanged();
}

bool ScopeController::saveReference(int slot, int source)
{
    if (slot < 0 || slot > 1 || !(m_record.sampleRate > 0))
        return false;
    Reference& ref = m_ref[slot];
    double x0 = m_recordX0;
    if (source == 2) {
        if (m_mathRecord.empty() || m_math.fft())
            return false;
        ref.data = m_mathRecord;
        ref.label = m_math.functionName();
        ref.scale = m_math.voltsPerDiv();
        ref.position = m_math.position();
    } else if (source == 0 || source == 1) {
        if (!m_record.enabled(source))
            return false;
        ref.data = m_record.data[source];
        x0 += m_record.skew[source];
        ref.label = channel(source)->name() + u" · "_s + channel(source)->sourceName();
        ref.scale = channel(source)->voltsPerDiv();
        ref.position = channel(source)->position();
    } else {
        return false;
    }
    ref.dt = 1.0 / m_record.sampleRate;
    ref.buffer.setUniform(x0, ref.dt, ref.data);
    ref.available = true;
    ref.visible = true;
    emit referencesChanged();
    return true;
}

void ScopeController::clearReference(int slot)
{
    if (slot < 0 || slot > 1)
        return;
    Reference& ref = m_ref[slot];
    ref.buffer.clear();
    ref.data.clear();
    ref.available = false;
    ref.label.clear();
    emit referencesChanged();
}

bool ScopeController::exportReference(int slot, const QUrl& file)
{
    if (slot < 0 || slot > 1 || !m_ref[slot].available)
        return false;
    const Reference& ref = m_ref[slot];
    DataTable table;
    table.title = title();
    table.instrumentId = instrumentId();
    table.addMetadata(tr("Reference"), (slot == 0 ? u"REF A: "_s : u"REF B: "_s) + ref.label);
    table.addMetadata(tr("Volts/Div"), scope::divisionLabel(ref.scale, u"V"_s));
    table.addMetadata(tr("Vertical position"), QString::number(ref.position) + u" div"_s);
    std::vector<double> time(ref.data.size());
    for (size_t i = 0; i < time.size(); ++i)
        time[i] = double(i) * ref.dt;
    table.addColumn(tr("Time"), u"s"_s, std::move(time));
    table.addColumn(ref.label, u"V"_s, ref.data);
    QString error;
    const QString path = DataExport::localPath(file);
    if (!DataExport::instance()->write(path, table, &error)) {
        Notifications::instance()->notify(Notifications::Error, tr("Reference not saved"), error);
        return false;
    }
    Notifications::instance()->notify(Notifications::Success, tr("Reference saved"), path);
    return true;
}

bool ScopeController::importReference(int slot, const QUrl& file)
{
    if (slot < 0 || slot > 1)
        return false;
    const QString path = DataExport::localPath(file);
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Notifications::instance()->notify(Notifications::Error, tr("Reference not loaded"), in.errorString());
        return false;
    }
    // Delimited text (our CSV/TSV exports or any two-column table): the first column is time,
    // the second the voltage; comment lines (#) and non-numeric rows (headers) are skipped.
    std::vector<double> time;
    std::vector<double> value;
    QTextStream stream(&in);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QStringList cells = line.split(QRegularExpression(u"[,;\\t]"_s));
        if (cells.size() < 2)
            continue;
        bool okT = false, okV = false;
        const double t = cells[0].trimmed().toDouble(&okT);
        const double v = cells[1].trimmed().toDouble(&okV);
        if (okT && okV) {
            time.push_back(t);
            value.push_back(v);
        }
    }
    if (value.size() < 2 || !(time[1] > time[0])) {
        Notifications::instance()->notify(Notifications::Error, tr("Reference not loaded"),
                                          tr("The file has no time/voltage columns."));
        return false;
    }
    Reference& ref = m_ref[slot];
    ref.dt = (time.back() - time.front()) / double(time.size() - 1);
    ref.data = std::move(value);
    ref.label = QFileInfo(path).completeBaseName();
    const auto [mn, mx] = std::minmax_element(ref.data.begin(), ref.data.end());
    const auto& scales = scope::voltsPerDivValues(false);
    ref.scale = scales.back();
    for (double s : scales) {
        if ((*mx - *mn) / s <= 8.0) {
            ref.scale = s;
            break;
        }
    }
    ref.position = std::clamp(-std::round((*mx + *mn) / 2.0 / ref.scale * 10.0) / 10.0, -5.0, 5.0);
    ref.buffer.setUniform(0.0, ref.dt, ref.data);
    ref.available = true;
    ref.visible = true;
    emit referencesChanged();
    return true;
}

// --- Worker ------------------------------------------------------------------------------------

InstrumentController::WorkerFunction ScopeController::createWorker()
{
    const scope::Snapshot initial = snapshot();
    const QString problem = scope::validate(initial);
    const bool autoscaleFirst = std::exchange(m_autoscaleOnStart, false);
    if (!problem.isEmpty()) {
        reportError(problem);
        setAutoscaling(false);
        return {};
    }
    m_live->clear();
    m_autoscaleRequest->store(false);
    setWaitingForTrigger(false);
    for (int ch = 0; ch < 2; ++ch)
        if (!initial.channel[ch].enabled)
            (ch == 0 ? m_ch0Trace : m_ch1Trace).clear();

    return [this, initial, autoscaleFirst, live = m_live, request = m_autoscaleRequest](WorkerContext& ctx) {
        scope::Snapshot s = initial;
        scope::Acquirer acquirer;
        scope::Roller roller;
        const scope::Acquirer::StopFn stop = [&ctx] { return ctx.stopRequested(); };
        bool autoscaleNow = autoscaleFirst;
        bool waiting = false;
        bool cleared = false;

        // Measurements, math and traces of a record, then hand it to the UI thread.
        auto publish = [&](scope::Record&& record, double x0) {
            auto p = std::make_shared<Published>();
            const double fs = record.sampleRate;
            const double dt = fs > 0 ? 1.0 / fs : 1.0;
            const size_t length = std::max(record.data[0].size(), record.data[1].size());
            p->startTime = QDateTime::currentDateTime().addMSecs(-qint64(double(length) * dt * 1000.0));
            plot::TraceBuffer* traces[2] = {&m_ch0Trace, &m_ch1Trace};
            for (int ch = 0; ch < 2; ++ch) {
                if (record.enabled(ch)) {
                    p->measurements[ch] = scope::measure(record.data[ch], fs, s.channel[ch].voltsPerDiv);
                    traces[ch]->setUniform(x0 + record.skew[ch], dt, record.data[ch]);
                } else {
                    traces[ch]->clear();
                }
            }
            if (record.enabled(0) && record.enabled(1)) {
                const size_t n = std::min(record.data[0].size(), record.data[1].size());
                m_xyTrace.setXY(std::span<const double>(record.data[0].data(), n),
                                std::span<const double>(record.data[1].data(), n));
            } else {
                m_xyTrace.clear();
            }
            if (s.mathEnabled && scope::isFft(s.mathFunction)) {
                const int src = s.mathFunction == scope::MathFunction::FftA ? 0 : 1;
                if (record.enabled(src)) {
                    const scope::FftResult fft = scope::fftAnalyse(record.data[src], fs);
                    p->fftBinWidth = fft.binWidth;
                    p->fftPeakFrequency = fft.peakFrequency;
                    p->fftPeakLevel = fft.peakLevel;
                    m_fftTrace.setUniform(0.0, fft.binWidth, fft.db);
                } else {
                    m_fftTrace.clear();
                }
                m_mathTrace.clear();
            } else if (s.mathEnabled) {
                const bool available = scope::needsBothChannels(s.mathFunction)
                                           ? record.enabled(0) && record.enabled(1)
                                           : record.enabled(s.mathFunction == scope::MathFunction::InvertA ? 0 : 1);
                if (available) {
                    p->math = scope::computeMath(s.mathFunction, record.data[0], record.data[1]);
                    const scope::Measurements m = scope::measure(p->math, fs, 1.0);
                    p->mathRms = m.rms;
                    p->mathPeakToPeak = m.peakToPeak;
                    m_mathTrace.setUniform(x0, dt, p->math);
                } else {
                    m_mathTrace.clear();
                }
                m_fftTrace.clear();
            } else {
                m_mathTrace.clear();
                m_fftTrace.clear();
            }
            p->record = std::move(record);
            p->x0 = x0;
            ctx.publish([this, p] { applyRecord(p); });
        };

        while (!ctx.stopRequested()) {
            if (auto next = live->take(); next && scope::validate(*next).isEmpty())
                s = *next;
            if (request->exchange(false))
                autoscaleNow = true;

            if (autoscaleNow) {
                autoscaleNow = false;
                acquirer.reset();   // one AI task at a time
                roller.stop();
                ctx.setStatus(tr("Autoscaling…"));
                const auto result = scope::runAutoscale(ctx.device(), s, stop);
                ctx.setStatus({});
                if (!result)
                    break;   // stopped
                live->clear();
                ctx.post([this, r = *result] { applyAutoscale(r); });
                // Continue with the settings the UI derives from the result.
                for (int i = 0; i < 100 && !live->hasValue() && !ctx.stopRequested(); ++i)
                    ctx.sleepMs(10);
                // Autoscale pressed while idle: ELVISmx only probes and applies the settings.
                if (autoscaleFirst && ctx.once())
                    break;
                continue;
            }

            QElapsedTimer cycle;
            cycle.start();

            if (s.roll) {
                // Roll mode: a continuous task, the newest sample on the right edge.
                const scope::AcquisitionPlan plan = [&] {
                    scope::Snapshot r = s;
                    r.roll = true;
                    return scope::planAcquisition(r, ctx.device());
                }();
                if (!roller.matches(plan)) {
                    acquirer.reset();
                    roller.stop();
                    roller.start(ctx.device(), s);
                    ctx.setTimedOut(false);
                    if (waiting) {
                        waiting = false;
                        ctx.post([this] { setWaitingForTrigger(false); });
                    }
                }
                if (!ctx.sleepMs(40))
                    break;
                const int64_t moved = roller.poll();
                ctx.addSamples(moved);
                ctx.setSampleRate(roller.record().sampleRate);
                if (roller.filled() > 1) {
                    scope::Record record = roller.record();
                    const double x0 = 10.0 * s.timePerDiv - double(roller.filled() - 1) / record.sampleRate;
                    publish(std::move(record), x0);
                    ctx.acquisitionDone();
                }
                if (ctx.once() && roller.filled() >= roller.windowLength())
                    break;
                continue;
            }
            roller.stop();

            scope::CycleResult result = acquirer.acquire(ctx.device(), s, stop);
            if (result.stopped)
                break;
            ctx.setSampleRate(acquirer.sampleRate());
            ctx.addSamples(result.samplesMoved);
            ctx.setTimedOut(result.timedOut);
            const bool nowWaiting = result.timedOut;
            if (nowWaiting != waiting) {
                waiting = nowWaiting;
                ctx.setStatus(waiting ? tr("Waiting for trigger") : QString());
                ctx.post([this, waiting] { setWaitingForTrigger(waiting); });
            }
            const bool displayed = result.record.has_value();
            if (displayed) {
                publish(std::move(*result.record), 0.0);
                ctx.acquisitionDone();
                cleared = false;
            } else if (result.timedOut && s.noTrigger == scope::NoTrigger::Clear && !cleared) {
                // ELVISmx: a record without trigger clears the graph and the measurements.
                publish(scope::Record{}, 0.0);
                cleared = true;
            }
            // Single: one displayed record; with Edge it keeps acquiring until a trigger is
            // found or Stop is pressed (a single-shot "arm").
            if (ctx.once() && displayed)
                break;
            const int remaining = kMinimumCycleMs - int(cycle.elapsed());
            if (remaining > 0 && !ctx.sleepMs(remaining))
                break;
        }
    };
}

void ScopeController::applyRecord(const std::shared_ptr<const Published>& p)
{
    m_record = p->record;
    m_recordX0 = p->x0;
    m_recordTime = p->startTime;
    m_mathRecord = p->math;
    for (int ch = 0; ch < 2; ++ch) {
        if (m_record.enabled(ch))
            channel(ch)->setResults(p->measurements[ch]);
        else
            channel(ch)->clearResults();
    }
    m_math.setResults(p->mathRms, p->mathPeakToPeak, p->fftPeakFrequency, p->fftPeakLevel);
    m_fftBinWidth = p->fftBinWidth;
    emit recordChanged();
}

void ScopeController::onFinished()
{
    setWaitingForTrigger(false);
    setAutoscaling(false);
    m_autoscaleRequest->store(false);
}

// --- Export ------------------------------------------------------------------------------------

DataTable ScopeController::exportTable() const
{
    DataTable table;
    const double fs = m_record.sampleRate;
    table.addMetadata(tr("Sample rate"), fs > 0 ? QString::number(fs, 'g', 10) + u" S/s"_s : u"-"_s);
    table.addMetadata(tr("Time/Div"), timeDivLabel());
    for (int ch = 0; ch < 2; ++ch) {
        const ScopeChannel* c = channel(ch);
        table.addMetadata(c->name(),
                          c->enabled() ? u"%1, %2/div, position %3 div, %4 coupling, range %5, probe 1x"_s.arg(
                                             c->sourceName(), c->scaleLabel(), QString::number(c->position()),
                                             c->coupling(), rangeText(c->effectiveRange()))
                                       : tr("disabled"));
    }
    if (m_triggerType == Edge && !rolling()) {
        table.addMetadata(tr("Trigger"),
                          u"Edge, %1, %2, level %3 V, hysteresis %4 V, horizontal position %5 %"_s.arg(
                              m_triggerSource == Chan1Source ? u"Chan 1 Source"_s : u"Chan 0 Source"_s,
                              m_triggerSlope == Falling ? u"falling"_s : u"rising"_s, QString::number(m_triggerLevel),
                              QString::number(m_triggerHysteresis * channel(m_triggerSource == Chan1Source ? 1 : 0)->voltsPerDiv()),
                              QString::number(m_horizontalPosition)));
        const double t = triggerTime();
        table.addMetadata(tr("Trigger time"), std::isfinite(t) ? QString::number(t, 'g', 10) + u" s from the first sample"_s
                                                                : tr("not triggered"));
    } else {
        table.addMetadata(tr("Trigger"), rolling() ? tr("Immediate (roll mode)") : tr("Immediate"));
    }
    table.addMetadata(tr("Record length"), m_samplingMode == HighResolution ? tr("high resolution") : tr("standard (2500 points)"));
    if (m_cursorsOn) {
        auto cursorText = [this](const ScopeCursor& c) {
            return u"%1 s, linked to %2, %3"_s.arg(QString::number(c.position(), 'g', 6),
                                                   c.channel() == 2 ? u"Math"_s : u"CH %1"_s.arg(c.channel()),
                                                   formatMeasurement(valueAt(c.channel(), c.position()), u"V"_s));
        };
        table.addMetadata(u"C1"_s, cursorText(m_cursor1));
        table.addMetadata(u"C2"_s, cursorText(m_cursor2));
        table.addMetadata(u"dT"_s, formatMeasurement(m_cursor2.position() - m_cursor1.position(), u"s"_s));
    }
    for (int ch = 0; ch < 2; ++ch) {
        const ScopeChannel* c = channel(ch);
        if (c->enabled() && c->hasResults())
            table.addMetadata(c->name() + u" measurements"_s,
                              u"RMS %1, Freq %2, Vp-p %3"_s.arg(formatMeasurement(c->rms(), u"V"_s),
                                                                formatMeasurement(c->frequency(), u"Hz"_s),
                                                                formatMeasurement(c->peakToPeak(), u"V"_s)));
    }
    if (!hasRecord() || !(fs > 0))
        return table;

    const size_t n = size_t(recordLength());
    std::vector<double> time(n);
    for (size_t i = 0; i < n; ++i)
        time[i] = double(i) / fs;
    table.addColumn(tr("Time"), u"s"_s, std::move(time));
    for (int ch = 0; ch < 2; ++ch)
        if (m_record.enabled(ch))
            table.addColumn(channel(ch)->name(), u"V"_s, m_record.data[ch]);
    if (m_math.enabled() && !m_math.fft() && !m_mathRecord.empty()) {
        table.addMetadata(tr("Math"), m_math.functionName());
        table.addColumn(tr("Math"), m_math.unit(), m_mathRecord);
    }
    return table;
}

bool ScopeController::exportElvisText(const QUrl& file)
{
    const QString path = DataExport::localPath(file);
    if (!hasRecord() || !(m_record.sampleRate > 0)) {
        Notifications::instance()->notify(Notifications::Info, tr("Nothing to log yet"), tr("Run the instrument first."));
        return false;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        Notifications::instance()->notify(Notifications::Error, tr("Log not saved"), out.errorString());
        return false;
    }
    // Layout of the ELVISmx Log TXT file (LabVIEW waveform export): a header block with t0 and
    // delta t per channel, then time/value column pairs with absolute timestamps, %E values.
    const double dt = 1.0 / m_record.sampleRate;
    const QDateTime t0 = m_recordTime.isValid() ? m_recordTime : QDateTime::currentDateTime();
    auto stamp = [&](double offset) {
        const qint64 us = qint64(std::llround(offset * 1e6));
        const QDateTime t = t0.addMSecs(us / 1000);
        const int micro = int((t0.time().msec() * 1000 + us) % 1000000);
        return t.toString(u"MM/dd/yyyy hh:mm:ss"_s) + u'.' + QString::number(micro).rightJustified(6, u'0')
               + t.toString(u" AP"_s);
    };
    auto number = [](double v) { return QString::asprintf("%.6E", v); };
    QList<int> channels;
    for (int ch = 0; ch < 2; ++ch)
        if (m_record.enabled(ch))
            channels.append(ch);
    QStringList rows[4];
    rows[0] << u"waveform"_s;
    rows[1] << u"t0"_s;
    rows[2] << u"delta t"_s;
    rows[3].clear();
    QStringList header;
    for (int i = 0; i < channels.size(); ++i) {
        const int ch = channels[i];
        rows[0] << u"Ch%1"_s.arg(ch) << QString();
        rows[1] << stamp(m_record.skew[ch]) << QString();
        rows[2] << number(dt) << QString();
        header << u"time[%1]"_s.arg(i) << u"Ch%1"_s.arg(ch);
    }
    QTextStream stream(&out);
    for (int r = 0; r < 3; ++r)
        stream << rows[r].join(u'\t') << '\n';
    stream << '\n' << header.join(u'\t') << '\n';
    const size_t n = size_t(recordLength());
    for (size_t i = 0; i < n; ++i) {
        QStringList cells;
        for (int ch : channels) {
            cells << stamp(double(i) * dt + m_record.skew[ch]);
            cells << (i < m_record.data[ch].size() ? number(m_record.data[ch][i]) : QString());
        }
        stream << cells.join(u'\t') << '\n';
    }
    stream.flush();
    if (out.error() != QFile::NoError) {
        Notifications::instance()->notify(Notifications::Error, tr("Log not saved"), out.errorString());
        return false;
    }
    Notifications::instance()->notify(Notifications::Success, tr("Log saved"), path);
    return true;
}

// --- Persistence of the sub-objects ------------------------------------------------------------

void ScopeController::saveExtraSettings(QJsonObject& settings) const
{
    settings.insert(u"channel0"_s, m_channel0.save());
    settings.insert(u"channel1"_s, m_channel1.save());
    settings.insert(u"mathTrace"_s, m_math.save());
    settings.insert(u"cursor1"_s, m_cursor1.save());
    settings.insert(u"cursor2"_s, m_cursor2.save());
}

void ScopeController::restoreExtraSettings(const QJsonObject& settings)
{
    m_channel0.restore(settings.value(u"channel0"_s).toObject());
    m_channel1.restore(settings.value(u"channel1"_s).toObject());
    m_math.restore(settings.value(u"mathTrace"_s).toObject());
    m_cursor1.restore(settings.value(u"cursor1"_s).toObject());
    m_cursor2.restore(settings.value(u"cursor2"_s).toObject());
    if (m_cursorsOn && (std::isnan(m_cursor1.position()) || std::isnan(m_cursor2.position())))
        resetCursors();
    fixTriggerSource();
}

// -----------------------------------------------------------------------------------------------

void registerScope(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"scope"_s,
        .title = QObject::tr("Oscilloscope"),
        .shortTitle = QObject::tr("Scope"),
        .description = QObject::tr("Two channels, edge trigger, measurements, cursors, math, XY"),
        .module = u"MyBench.Instruments.Scope"_s,
        .icon = u"instrument-scope"_s,
        .order = 20,
        .createController = [] { return new ScopeController; },
    });
}

}  // namespace mybench::instruments
