// InstrumentController: base class of every instrument's C++ controller.
//
// It owns the run state machine (Idle -> Starting -> Running -> Stopping -> Idle, plus Paused
// when the ResourceArbiter preempts it), runs the instrument's loop in a worker thread
// (createWorker(), see worker.h), turns mydaq::DeviceError into an error state + toast (never a
// crash), declares the device resources the instrument needs, and persists its settings.
//
// Settings persistence: every Q_PROPERTY declared by a subclass that is writable, has a NOTIFY
// signal and is STORED (the Qt default) is a setting. Settings are saved as JSON (enums by key
// name) in QSettings ("instruments/<id>/settings", debounced) and in session files, restored at
// startup, and reset by resetSettings(). Mark writable properties that are not settings (view
// state, transient values) `STORED false`. Complex state (tables, waveforms) goes through
// saveExtraSettings()/restoreExtraSettings().
//
// Threading: every method runs on the UI thread, except the worker loop and WorkerContext.
// See instruments/README.md for a complete example.
#pragma once

#include <mybench/dataexport.h>
#include <mybench/resourcearbiter.h>
#include <mybench/worker.h>

#include <mydaq/device.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QMetaProperty>
#include <QObject>
#include <QQmlParserStatus>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace mybench {

class InstrumentController : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    QML_UNCREATABLE("InstrumentController is the abstract base of the instrument controllers")

    // Identity (from the InstrumentRegistry descriptor; falls back to the id).
    Q_PROPERTY(QString instrumentId READ instrumentId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString shortTitle READ shortTitle CONSTANT)

    // Run state.
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)   // Starting, Running or Stopping
    Q_PROPERTY(bool idle READ isIdle NOTIFY stateChanged)         // Idle or Paused
    Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)         // Starting or Stopping
    Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY stateChanged)
    Q_PROPERTY(RunMode runMode READ runMode NOTIFY runModeChanged)   // of the current/last run
    Q_PROPERTY(bool canRun READ canRun NOTIFY canRunChanged)      // idle and a device is connected
    Q_PROPERTY(bool supportsSingle READ supportsSingle CONSTANT)  // Single (Run Once) exists
    Q_PROPERTY(QDateTime startedAt READ startedAt NOTIFY stateChanged)

    // Run-bar / status-bar indicators.
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool timedOut READ timedOut NOTIFY timedOutChanged)
    Q_PROPERTY(double sampleRate READ sampleRate NOTIFY sampleRateChanged)   // S/s, 0 = n/a
    Q_PROPERTY(double throughput READ throughput NOTIFY throughputChanged)   // samples/s moved
    Q_PROPERTY(int acquisitionCount READ acquisitionCount NOTIFY acquisitionCountChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY hasDataChanged)   // something to export
    // One-line acquisition summary for the status bar ("AI 200 kS/s × 2", "DMM 5 readings/s");
    // empty: the shell composes one from sampleRate/throughput/resources.
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)
    // The last run was stopped because the device was unplugged (reconnect banner + Resume).
    Q_PROPERTY(bool interruptedByDisconnect READ interruptedByDisconnect NOTIFY stateChanged)

    // Errors. errorText is the current (last) error until clearError() or the next run;
    // errorHistory keeps the last 50: [{ time: Date, message: string }], newest first.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)
    Q_PROPERTY(QVariantList errorHistory READ errorHistory NOTIFY errorChanged)

    // Declared device resources (ResourceArbiter::Resources flags) and DIO line mask.
    Q_PROPERTY(int resources READ resourcesInt NOTIFY resourcesChanged)
    Q_PROPERTY(int dioLines READ dioLines NOTIFY resourcesChanged)
    Q_PROPERTY(QString resourceSummary READ resourceSummary NOTIFY resourcesChanged)  // "AI · AO"

public:
    enum State { Idle, Starting, Running, Stopping, Paused };
    Q_ENUM(State)
    enum RunMode { Continuous, Once };
    Q_ENUM(RunMode)

    using WorkerFunction = std::function<void(WorkerContext&)>;

    explicit InstrumentController(const QString& instrumentId, QObject* parent = nullptr);
    ~InstrumentController() override;   // stops and joins the worker (see shutdown())

    QString instrumentId() const { return m_id; }
    QString title() const;
    QString shortTitle() const;

    State state() const { return m_state; }
    bool isRunning() const { return m_state == Starting || m_state == Running || m_state == Stopping; }
    bool isIdle() const { return m_state == Idle || m_state == Paused; }
    bool isPaused() const { return m_state == Paused; }
    bool isBusy() const { return m_state == Starting || m_state == Stopping; }
    QString pauseReason() const { return m_pauseReason; }
    RunMode runMode() const { return m_runMode; }
    bool canRun() const;
    bool supportsSingle() const { return m_supportsSingle; }
    QDateTime startedAt() const { return m_startedAt; }

    QString statusText() const { return m_statusText; }
    bool timedOut() const { return m_timedOut; }
    double sampleRate() const { return m_sampleRate; }
    double throughput() const { return m_throughput; }
    int acquisitionCount() const { return m_acquisitionCount; }
    bool hasData() const { return m_hasData; }
    QString summary() const { return m_summary; }
    bool interruptedByDisconnect() const { return m_interruptedByDisconnect; }

    QString errorText() const { return m_errorText; }
    bool hasError() const { return !m_errorText.isEmpty(); }
    QVariantList errorHistory() const { return m_errorHistory; }

    ResourceArbiter::Resources resources() const { return m_resources; }
    int resourcesInt() const { return int(m_resources); }
    int dioLines() const { return m_dioLines; }
    QString resourceSummary() const;

    // --- Settings ----------------------------------------------------------------------------
    // Called once after construction (by InstrumentRegistry, or componentComplete() for
    // controllers created in QML): records the defaults, restores the saved settings and starts
    // auto-saving. Safe to call twice.
    void initialize();
    QJsonObject saveSettings() const;
    void restoreSettings(const QJsonObject& settings);
    // The properties treated as settings (see the header comment).
    QList<QMetaProperty> persistentProperties() const;

    // Stops the worker and waits for it (bounded). Call from a subclass destructor when the
    // worker lambda captured `this`; the registry calls it before deleting controllers.
    void shutdown();

    // User-facing message for a device error (the table of docs/instruments/app.md).
    QString describeError(const mydaq::DeviceError& error) const;

    // Global switch (before controllers are created): restore the settings saved in QSettings
    // at initialize() (default true; the shells turn it off for --fresh and the preference).
    static void setRestoreSavedSettings(bool restore);
    static bool restoreSavedSettings();

    // Called by DeviceHub when the selected device disappears: stops a running instrument with
    // the error "The device was disconnected." and sets interruptedByDisconnect.
    void deviceDisconnected();

    // QQmlParserStatus
    void classBegin() override {}
    void componentComplete() override { initialize(); }

public slots:
    void run();          // Run: continuous. While a Single runs: continue continuously.
    void runOnce();      // Single. While running continuously: finish after this acquisition.
    void stop();
    void toggle();       // run() when idle/paused, stop() when running
    void resume();       // after a pause: run again in the mode of the paused run
    void clearError();
    void resetSettings();   // back to the defaults captured by initialize()
    // Pauses a running instrument (used by ResourceArbiter; also callable from QML/tests).
    void pause(const QString& reason);
    // Stops a running instrument and reports `message` as its error once it stopped (e.g. "The
    // device was disconnected.").
    void abort(const QString& message);
    // Exports exportTable() to `file` (format by extension). Toast on success or failure.
    bool exportData(const QUrl& file);
    // The settings lines of exportTable() ({ key: value }), e.g. for Export PDF.
    QVariantMap exportMetadata() const;

signals:
    void stateChanged();
    void runModeChanged();
    void canRunChanged();
    void statusTextChanged();
    void timedOutChanged();
    void sampleRateChanged();
    void throughputChanged();
    void acquisitionCountChanged();
    void hasDataChanged();
    void summaryChanged();
    void errorChanged();
    void resourcesChanged();
    void settingsChanged();   // any setting changed (after initialize())
    void started();           // the worker is running
    void finished();          // the worker ended (stop, Single done, error) and resources are free
    void errorOccurred(const QString& message);

protected:
    // Builds the run, on the UI thread, when Run/Single is pressed. Validate the settings and
    // capture everything the loop needs *by value* (a snapshot). Throw mydaq::DeviceError
    // (InvalidConfig) to refuse with a message; return {} to refuse silently.
    virtual WorkerFunction createWorker() = 0;
    // During createWorker(): the mode being started (Run = Continuous, Single = Once, Resume = the
    // paused run's mode). runMode() still reports the previous run at that point.
    RunMode startingRunMode() const { return m_startingMode; }
    // UI thread: the worker started / ended (after errors too) and every task is released.
    virtual void onStarted() {}
    virtual void onFinished() {}
    // Complex settings that are not plain properties.
    virtual void saveExtraSettings(QJsonObject& settings) const { Q_UNUSED(settings) }
    virtual void restoreExtraSettings(const QJsonObject& settings) { Q_UNUSED(settings) }
    // Data of the last acquisition for exportData(). Default: empty (nothing to export).
    virtual DataTable exportTable() const { return {}; }

    // Declares the device resources (call in the constructor, or while idle when a setting such
    // as the DIO line selection changes them).
    void setRequiredResources(ResourceArbiter::Resources resources, quint8 dioLines = 0);
    void setSupportsSingle(bool supported) { m_supportsSingle = supported; }   // constructor only
    void setStatusText(const QString& text);
    void setTimedOut(bool timedOut);
    void setSampleRate(double samplesPerSecond);
    void setHasData(bool hasData);
    void setSummary(const QString& summary);
    void incrementAcquisitionCount();
    // Error (logged, toast, sets errorText). Does not stop a run: throw from the worker for that.
    void reportError(const QString& message);
    // Non-fatal warning (logged, toast).
    void reportWarning(const QString& message);
    // Stops and starts again with a fresh createWorker() (for settings that need new tasks).
    // Does nothing when idle.
    void restart();
    // Emits settingsChanged and schedules the auto-save. Called automatically when a persistent
    // property changes; call it yourself after changing extra settings.
    void markSettingsChanged();

private slots:
    void onPersistentPropertyChanged();

private:
    friend class WorkerContext;
    struct Private;

    void startRun(RunMode mode);
    static void threadMain(std::shared_ptr<detail::RunState> state, WorkerFunction function);
    void shutdownImpl(bool destroying);
    void setState(State state);
    void setRunMode(RunMode mode);
    void recordError(const QString& message, bool notify);
    void onWorkerStarted(quint64 generation);
    void onWorkerFinished(quint64 generation, const QString& error, bool disconnected);
    void updateThroughput();
    void saveToStore();
    void updateCanRun();

    QString m_id;
    State m_state = Idle;
    RunMode m_runMode = Continuous;
    RunMode m_pausedMode = Continuous;
    RunMode m_startingMode = Continuous;
    bool m_pausePending = false;
    bool m_restartPending = false;
    bool m_supportsSingle = true;
    bool m_initialized = false;
    bool m_restoring = false;
    bool m_canRun = false;
    QString m_pauseReason;
    QString m_abortMessage;
    QDateTime m_startedAt;
    QString m_statusText;
    bool m_timedOut = false;
    double m_sampleRate = 0;
    double m_throughput = 0;
    int m_acquisitionCount = 0;
    bool m_hasData = false;
    QString m_summary;
    bool m_interruptedByDisconnect = false;
    QString m_errorText;
    QVariantList m_errorHistory;
    ResourceArbiter::Resources m_resources;
    quint8 m_dioLines = 0;
    QJsonObject m_defaults;
    QTimer m_saveTimer;
    QTimer m_throughputTimer;
    QElapsedTimer m_throughputClock;
    std::unique_ptr<Private> d;
};

}  // namespace mybench
