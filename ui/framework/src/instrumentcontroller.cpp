#include "mybench/instrumentcontroller.h"

#include "mybench/devicehub.h"
#include "mybench/eventlog.h"
#include "mybench/instrumentregistry.h"
#include "mybench/notifications.h"
#include "mybench/settingsjson.h"
#include "runstate_p.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QSettings>

#include <exception>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

constexpr int kSaveDelayMs = 400;
constexpr int kMaxErrorHistory = 50;
constexpr auto kPredecessorTimeout = std::chrono::seconds(5);
constexpr auto kJoinTimeout = std::chrono::seconds(3);

bool g_restoreSavedSettings = true;

QString settingsKey(const QString& id)
{
    return u"instruments/%1/settings"_s.arg(id);
}

// "message" -> "message." unless it already ends with punctuation.
QString sentence(QString text)
{
    text = text.trimmed();
    if (!text.isEmpty() && !text.endsWith(u'.') && !text.endsWith(u'!') && !text.endsWith(u'?'))
        text += u'.';
    return text;
}

}  // namespace

struct InstrumentController::Private {
    std::shared_ptr<detail::RunState> run;                  // current run, if any
    std::vector<std::shared_ptr<detail::RunState>> retired;  // stopped runs not joined yet
    quint64 generation = 0;
    QList<QMetaProperty> persistent;
};

InstrumentController::InstrumentController(const QString& instrumentId, QObject* parent)
    : QObject(parent), m_id(instrumentId), d(std::make_unique<Private>())
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &InstrumentController::saveToStore);

    m_throughputTimer.setInterval(1000);
    connect(&m_throughputTimer, &QTimer::timeout, this, &InstrumentController::updateThroughput);

    DeviceHub* hub = DeviceHub::instance();
    connect(hub, &DeviceHub::connectionChanged, this, &InstrumentController::updateCanRun);
    connect(hub, &DeviceHub::currentChanged, this, &InstrumentController::updateCanRun);
    connect(this, &InstrumentController::stateChanged, this, &InstrumentController::updateCanRun);
    m_canRun = canRun();
}

InstrumentController::~InstrumentController()
{
    shutdownImpl(true);
}

QString InstrumentController::title() const
{
    if (const InstrumentDescriptor* descriptor = InstrumentRegistry::instance()->descriptor(m_id))
        return descriptor->title;
    return m_id;
}

QString InstrumentController::shortTitle() const
{
    if (const InstrumentDescriptor* descriptor = InstrumentRegistry::instance()->descriptor(m_id))
        return descriptor->shortTitle;
    return m_id;
}

bool InstrumentController::canRun() const
{
    return isIdle() && DeviceHub::instance()->isConnected();
}

void InstrumentController::updateCanRun()
{
    const bool value = canRun();
    if (value != m_canRun) {
        m_canRun = value;
        emit canRunChanged();
    }
}

QString InstrumentController::resourceSummary() const
{
    QStringList parts;
    if (m_resources.testFlag(ResourceArbiter::AnalogInput))
        parts << ResourceArbiter::label(ResourceArbiter::AnalogInput);
    if (m_resources.testFlag(ResourceArbiter::AnalogOutput))
        parts << ResourceArbiter::label(ResourceArbiter::AnalogOutput);
    if (m_resources.testFlag(ResourceArbiter::Multimeter))
        parts << ResourceArbiter::label(ResourceArbiter::Multimeter);
    if (m_resources.testFlag(ResourceArbiter::Counter))
        parts << ResourceArbiter::label(ResourceArbiter::Counter);
    if (m_resources.testFlag(ResourceArbiter::FrequencyOutput))
        parts << ResourceArbiter::label(ResourceArbiter::FrequencyOutput);
    if (m_resources.testFlag(ResourceArbiter::DigitalLines))
        parts << ResourceArbiter::dioLinesLabel(m_dioLines);
    return parts.join(u" · "_s);
}

// --- Run state machine ---------------------------------------------------------------------

void InstrumentController::run()
{
    startRun(Continuous);
}

void InstrumentController::runOnce()
{
    if (!m_supportsSingle) {
        startRun(Continuous);
        return;
    }
    startRun(Once);
}

void InstrumentController::toggle()
{
    if (isRunning())
        stop();
    else if (isPaused())
        resume();
    else
        run();
}

void InstrumentController::resume()
{
    if (m_state == Paused)
        startRun(m_pausedMode);
}

void InstrumentController::stop()
{
    m_restartPending = false;
    switch (m_state) {
    case Starting:
    case Running:
        if (d->run)
            d->run->stopSource.request_stop();
        setState(Stopping);
        break;
    case Paused:
        m_pauseReason.clear();
        Notifications::instance()->dismissSource(u"pause:"_s + m_id);
        setState(Idle);
        break;
    case Idle:
    case Stopping:
        break;
    }
}

void InstrumentController::pause(const QString& reason)
{
    if (m_state != Starting && m_state != Running)
        return;
    m_pausePending = true;
    m_pauseReason = reason;
    m_pausedMode = m_runMode;
    stop();
    emit stateChanged();   // pauseReason
}

void InstrumentController::setRestoreSavedSettings(bool restore)
{
    g_restoreSavedSettings = restore;
}

bool InstrumentController::restoreSavedSettings()
{
    return g_restoreSavedSettings;
}

void InstrumentController::deviceDisconnected()
{
    if (m_state != Starting && m_state != Running)
        return;
    m_interruptedByDisconnect = true;
    abort(tr("The device was disconnected."));
}

void InstrumentController::abort(const QString& message)
{
    if (isPaused()) {
        stop();
        recordError(message, true);
        return;
    }
    if (m_state != Starting && m_state != Running)
        return;
    m_abortMessage = message;
    stop();
}

void InstrumentController::restart()
{
    if (m_state != Starting && m_state != Running)
        return;
    stop();
    m_restartPending = true;
}

void InstrumentController::startRun(RunMode mode)
{
    if (!m_initialized)
        initialize();

    switch (m_state) {
    case Starting:
    case Running:
        // Live mode change: Single while continuous ends after the current acquisition.
        setRunMode(mode);
        if (d->run)
            d->run->continuous.store(mode == Continuous);
        return;
    case Stopping:
        return;
    case Idle:
    case Paused:
        break;
    }

    const bool wasPaused = m_state == Paused;
    auto refuse = [&](const QString& message) {
        if (!message.isEmpty())
            recordError(message, true);
        if (wasPaused) {
            m_pauseReason.clear();
            setState(Idle);
        }
    };

    m_pauseReason.clear();
    m_interruptedByDisconnect = false;
    Notifications::instance()->dismissSource(u"pause:"_s + m_id);
    clearError();

    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    if (!device || !device->isConnected()) {
        refuse(describeError(mydaq::DeviceError(mydaq::DeviceError::NotConnected, {})));
        return;
    }

    WorkerFunction function;
    try {
        function = createWorker();
    } catch (const mydaq::DeviceError& error) {
        refuse(describeError(error));
        return;
    } catch (const std::exception& error) {
        refuse(QString::fromUtf8(error.what()));
        return;
    }
    if (!function) {
        refuse({});
        return;
    }

    const QList<InstrumentController*> preempted = ResourceArbiter::instance()->acquire(this);

    auto state = std::make_shared<detail::RunState>();
    state->owner = this;
    state->generation = ++d->generation;
    state->device = device;
    state->continuous.store(mode == Continuous);
    for (InstrumentController* other : preempted) {
        if (other->d->run)
            state->predecessors.push_back(other->d->run);
        for (const auto& retired : other->d->retired)
            state->predecessors.push_back(retired);
    }
    for (const auto& retired : d->retired)
        state->predecessors.push_back(retired);

    d->run = state;
    m_abortMessage.clear();
    m_startedAt = QDateTime::currentDateTime();
    setRunMode(mode);
    setStatusText({});
    setTimedOut(false);
    state->thread = std::thread(&InstrumentController::threadMain, state, std::move(function));
    setState(Starting);
}

void InstrumentController::threadMain(std::shared_ptr<detail::RunState> state, WorkerFunction function)
{
    InstrumentController* owner = state->owner;
    const quint64 generation = state->generation;
    const std::stop_token token = state->stopSource.get_token();
    int errorCode = -2;   // -2: none, -1: not a DeviceError
    QString errorMessage;
    bool ranLoop = false;

    for (const auto& previous : state->predecessors) {
        if (!previous->waitFinished(kPredecessorTimeout, token) && !token.stop_requested()) {
            errorCode = mydaq::DeviceError::ResourceBusy;
            errorMessage = u"another instrument did not release the device in time"_s;
            break;
        }
    }
    state->predecessors.clear();

    if (errorCode == -2 && !token.stop_requested()) {
        QMetaObject::invokeMethod(owner, [owner, generation] { owner->onWorkerStarted(generation); },
                                  Qt::QueuedConnection);
        ranLoop = true;
        try {
            WorkerContext context(state);
            function(context);
        } catch (const mydaq::DeviceError& error) {
            errorCode = error.code();
            errorMessage = error.message();
        } catch (const std::exception& error) {
            errorCode = -1;
            errorMessage = QString::fromUtf8(error.what());
        } catch (...) {
            errorCode = -1;
            errorMessage = u"Unexpected error in the instrument worker."_s;
        }
        // Errors while stopping are side effects of the stop (e.g. a read interrupted by an
        // unplug); they are logged, not shown.
        if (errorCode != -2 && token.stop_requested()) {
            EventLog::log(EventLog::Debug, QString(), u"ignored error while stopping: "_s + errorMessage);
            errorCode = -2;
        }
    }

    function = nullptr;   // destroys the captures (tasks) before the resources are released
    state->markFinished();
    QMetaObject::invokeMethod(owner, [owner, generation, errorCode, errorMessage, ranLoop] {
        QString text;
        // The device went away during the run (unplugged): reported as a disconnection, with the
        // reconnect banner, rather than as "not connected".
        const bool disconnected = ranLoop && errorCode == mydaq::DeviceError::NotConnected;
        if (disconnected)
            text = tr("The device was disconnected.");
        else if (errorCode >= 0)
            text = owner->describeError(mydaq::DeviceError(mydaq::DeviceError::Code(errorCode), errorMessage));
        else if (errorCode == -1)
            text = errorMessage;
        owner->onWorkerFinished(generation, text, disconnected);
    }, Qt::QueuedConnection);
}

void InstrumentController::onWorkerStarted(quint64 generation)
{
    if (!d->run || d->run->generation != generation)
        return;
    if (m_state == Starting) {
        m_throughputClock.start();
        m_throughputTimer.start();
        setState(Running);
        onStarted();
        emit started();
    }
}

void InstrumentController::onWorkerFinished(quint64 generation, const QString& error, bool disconnected)
{
    std::shared_ptr<detail::RunState> state;
    const bool current = d->run && d->run->generation == generation;
    if (current) {
        state = std::move(d->run);
        d->run.reset();
    } else {
        for (auto it = d->retired.begin(); it != d->retired.end(); ++it) {
            if ((*it)->generation == generation) {
                state = *it;
                d->retired.erase(it);
                break;
            }
        }
    }
    if (state && state->thread.joinable())
        state->thread.join();   // the thread's last statement posted this event: returns at once
    if (!current)
        return;

    ResourceArbiter::instance()->release(this);
    m_throughputTimer.stop();
    if (m_throughput != 0) {
        m_throughput = 0;
        emit throughputChanged();
    }
    setStatusText({});

    if (disconnected)
        m_interruptedByDisconnect = true;
    QString message = error;
    if (message.isEmpty() && !m_abortMessage.isEmpty())
        message = m_abortMessage;
    m_abortMessage.clear();
    if (!message.isEmpty()) {
        m_pausePending = false;
        m_restartPending = false;
        recordError(message, true);
    }

    const bool paused = m_pausePending;
    m_pausePending = false;
    if (!paused)
        m_pauseReason.clear();
    setState(paused ? Paused : Idle);
    onFinished();
    emit finished();

    if (m_restartPending) {
        m_restartPending = false;
        startRun(m_runMode);
    }
}

void InstrumentController::shutdown()
{
    shutdownImpl(false);
}

void InstrumentController::shutdownImpl(bool destroying)
{
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveToStore();
    }
    if (d->run) {
        d->run->stopSource.request_stop();
        d->retired.push_back(std::move(d->run));
        d->run.reset();
    }
    for (const auto& state : d->retired) {
        if (state->waitFinished(std::chrono::duration_cast<std::chrono::milliseconds>(kJoinTimeout))) {
            if (state->thread.joinable())
                state->thread.join();
        } else if (state->thread.joinable()) {
            qWarning("MYBENCH: the %s worker did not stop in time; detaching it", qPrintable(m_id));
            state->thread.detach();
        }
    }
    d->retired.clear();
    ResourceArbiter::instance()->release(this);
    m_throughputTimer.stop();
    m_pausePending = false;
    m_restartPending = false;
    if (m_state != Idle) {
        m_state = Idle;
        if (!destroying)
            emit stateChanged();
    }
}

void InstrumentController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void InstrumentController::setRunMode(RunMode mode)
{
    if (m_runMode == mode)
        return;
    m_runMode = mode;
    emit runModeChanged();
}

// --- Indicators ----------------------------------------------------------------------------

void InstrumentController::setStatusText(const QString& text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void InstrumentController::setTimedOut(bool timedOut)
{
    if (m_timedOut == timedOut)
        return;
    m_timedOut = timedOut;
    emit timedOutChanged();
}

void InstrumentController::setSampleRate(double samplesPerSecond)
{
    if (m_sampleRate == samplesPerSecond)
        return;
    m_sampleRate = samplesPerSecond;
    emit sampleRateChanged();
}

void InstrumentController::setHasData(bool hasData)
{
    if (m_hasData == hasData)
        return;
    m_hasData = hasData;
    emit hasDataChanged();
}

void InstrumentController::setSummary(const QString& summary)
{
    if (m_summary == summary)
        return;
    m_summary = summary;
    emit summaryChanged();
}

void InstrumentController::incrementAcquisitionCount()
{
    ++m_acquisitionCount;
    emit acquisitionCountChanged();
}

void InstrumentController::updateThroughput()
{
    if (!d->run)
        return;
    const qint64 samples = d->run->samples.exchange(0);
    const qint64 elapsed = std::max<qint64>(1, m_throughputClock.restart());
    const double value = double(samples) * 1000.0 / double(elapsed);
    if (value != m_throughput) {
        m_throughput = value;
        emit throughputChanged();
    }
}

void InstrumentController::setRequiredResources(ResourceArbiter::Resources resources, quint8 dioLines)
{
    if (!resources.testFlag(ResourceArbiter::DigitalLines))
        dioLines = 0;
    if (m_resources == resources && m_dioLines == dioLines)
        return;
    if (isRunning())
        qWarning("MYBENCH: %s changed its resources while running; effective at the next run", qPrintable(m_id));
    m_resources = resources;
    m_dioLines = dioLines;
    emit resourcesChanged();
}

// --- Errors --------------------------------------------------------------------------------

QString InstrumentController::describeError(const mydaq::DeviceError& error) const
{
    using E = mydaq::DeviceError;
    const QString message = error.message().trimmed();
    switch (error.code()) {
    case E::NotConnected:
        return tr("The device is not connected. Connect the myDAQ (or select another device) and press Run again.");
    case E::ResourceBusy: {
        QString what = tr("device");
        if (m_resources.testFlag(ResourceArbiter::AnalogInput))
            what = ResourceArbiter::description(ResourceArbiter::AnalogInput);
        else if (m_resources.testFlag(ResourceArbiter::AnalogOutput))
            what = ResourceArbiter::description(ResourceArbiter::AnalogOutput);
        else if (m_resources.testFlag(ResourceArbiter::Multimeter))
            what = ResourceArbiter::description(ResourceArbiter::Multimeter);
        else if (m_resources.testFlag(ResourceArbiter::Counter))
            what = ResourceArbiter::description(ResourceArbiter::Counter);
        else if (m_resources.testFlag(ResourceArbiter::FrequencyOutput))
            what = ResourceArbiter::description(ResourceArbiter::FrequencyOutput);
        else if (m_resources.testFlag(ResourceArbiter::DigitalLines))
            what = ResourceArbiter::description(ResourceArbiter::DigitalLines);
        QString device = DeviceHub::instance()->name();
        if (device.isEmpty())
            device = tr("the device");
        return tr("The %1 of %2 is busy (used by another program?).").arg(what, device);
    }
    case E::InvalidConfig:
        return sentence(tr("The device rejected the settings: %1").arg(message));
    case E::Timeout:
        return sentence(tr("The device did not answer in time. %1").arg(message));
    case E::Overflow:
        return tr("Data were lost because the computer could not read fast enough. Try a slower timebase / lower sample rate.");
    case E::Underflow:
        return tr("The output ran out of data because the computer could not keep up. The generation was stopped.");
    case E::UsbError:
        return tr("USB communication error: %1. Check the cable and reconnect the device.").arg(message);
    case E::NotSupported:
        return sentence(tr("This operation is not supported by the selected device: %1").arg(message));
    case E::Generic:
        break;
    }
    return sentence(message.isEmpty() ? tr("Unknown device error") : message);
}

void InstrumentController::recordError(const QString& message, bool notify)
{
    m_errorText = message;
    QVariantMap entry;
    entry.insert(u"time"_s, QDateTime::currentDateTime());
    entry.insert(u"message"_s, message);
    m_errorHistory.prepend(entry);
    while (m_errorHistory.size() > kMaxErrorHistory)
        m_errorHistory.removeLast();
    emit errorChanged();
    emit errorOccurred(message);
    EventLog::log(EventLog::Error, m_id, message);
    if (notify)
        Notifications::instance()->show(Notifications::Error, title(), message, {}, {}, m_id);
}

void InstrumentController::reportError(const QString& message)
{
    recordError(message, true);
}

void InstrumentController::reportWarning(const QString& message)
{
    EventLog::log(EventLog::Warning, m_id, message);
    Notifications::instance()->show(Notifications::Warning, title(), message, {}, {}, m_id);
}

void InstrumentController::clearError()
{
    if (m_errorText.isEmpty())
        return;
    m_errorText.clear();
    emit errorChanged();
}

// --- Settings ------------------------------------------------------------------------------

QList<QMetaProperty> InstrumentController::persistentProperties() const
{
    QList<QMetaProperty> list;
    const QMetaObject* meta = metaObject();
    for (int i = InstrumentController::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (property.isWritable() && property.isStored() && !property.isConstant() && property.hasNotifySignal())
            list.append(property);
    }
    return list;
}

void InstrumentController::initialize()
{
    if (m_initialized)
        return;
    m_initialized = true;
    d->persistent = persistentProperties();
    m_defaults = saveSettings();

    const QByteArray stored = g_restoreSavedSettings ? QSettings().value(settingsKey(m_id)).toByteArray() : QByteArray();
    if (!stored.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(stored, &parseError);
        if (document.isObject()) {
            m_restoring = true;
            json::writeProperties(this, d->persistent, document.object());
            restoreExtraSettings(document.object());
            m_restoring = false;
        }
    }

    const QMetaMethod slot = staticMetaObject.method(staticMetaObject.indexOfSlot("onPersistentPropertyChanged()"));
    for (const QMetaProperty& property : std::as_const(d->persistent))
        connect(this, property.notifySignal(), this, slot);
}

QJsonObject InstrumentController::saveSettings() const
{
    const QList<QMetaProperty> properties = d->persistent.isEmpty() ? persistentProperties() : d->persistent;
    QJsonObject settings = json::readProperties(this, properties);
    saveExtraSettings(settings);
    return settings;
}

void InstrumentController::restoreSettings(const QJsonObject& settings)
{
    if (!m_initialized)
        initialize();
    m_restoring = true;
    json::writeProperties(this, d->persistent, settings);
    restoreExtraSettings(settings);
    m_restoring = false;
    markSettingsChanged();
}

void InstrumentController::resetSettings()
{
    restoreSettings(m_defaults);
}

void InstrumentController::onPersistentPropertyChanged()
{
    if (!m_restoring)
        markSettingsChanged();
}

void InstrumentController::markSettingsChanged()
{
    if (!m_initialized || m_restoring)
        return;
    emit settingsChanged();
    m_saveTimer.start();
}

void InstrumentController::saveToStore()
{
    QSettings().setValue(settingsKey(m_id), QJsonDocument(saveSettings()).toJson(QJsonDocument::Compact));
}

// --- Export --------------------------------------------------------------------------------

QVariantMap InstrumentController::exportMetadata() const
{
    QVariantMap map;
    for (const auto& [key, value] : exportTable().metadata)
        map.insert(key, value);
    return map;
}

bool InstrumentController::exportData(const QUrl& file)
{
    DataTable table = exportTable();
    if (table.isEmpty()) {
        reportError(tr("There is no data to export yet."));
        return false;
    }
    if (table.title.isEmpty())
        table.title = title();
    if (table.instrumentId.isEmpty())
        table.instrumentId = m_id;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString error;
    if (!DataExport::instance()->write(path, table, &error)) {
        reportError(tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    const QString folder = QFileInfo(path).absolutePath();
    Notifications::instance()->show(Notifications::Success, tr("Data exported"), QFileInfo(path).fileName(),
                                    tr("Show folder"),
                                    [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); }, m_id);
    return true;
}

}  // namespace mybench
