// Application-shell behaviour of MyBench.Framework (docs/audit.md "Application shell" rows 23,
// 26, 27, 28): toast stack and pause toasts, the event log's warning counter and technical
// details, hot-plug polling and the simulator controls of DeviceHub. Simulated myDAQ only.
#include <mybench/framework.h>

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QElapsedTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace mybench;
using namespace Qt::StringLiterals;

namespace {

// Continuous AI 0 reads at 1 kS/s: enough to hold the AI resource.
class AiInstrument : public InstrumentController
{
    Q_OBJECT

public:
    explicit AiInstrument(const QString& id) : InstrumentController(id)
    {
        setRequiredResources(ResourceArbiter::AnalogInput);
    }
    ~AiInstrument() override { shutdown(); }

protected:
    WorkerFunction createWorker() override
    {
        mydaq::AiConfig config;
        config.channels = {{mydaq::AiChannel::AI0, 10.0}};
        config.sampleRate = 1000;
        config.mode = mydaq::SampleMode::Continuous;
        config.samplesPerChannel = 1000;
        return [config](WorkerContext& ctx) {
            auto task = ctx.device().createAiTask(config);
            task->start();
            while (!ctx.stopRequested()) {
                task->read(50, 1000);
                ctx.acquisitionDone();
                if (ctx.once())
                    break;
            }
        };
    }
};

int toastsFrom(const QString& source)
{
    Notifications* n = Notifications::instance();
    int count = 0;
    for (int row = 0; row < n->rowCount(); ++row) {
        if (n->data(n->index(row), Notifications::SourceRole).toString() == source)
            ++count;
    }
    return count;
}

}  // namespace

class TestShellRows : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_shellrows"_s);
        QVERIFY(DeviceHub::instance()->isConnected());
        QVERIFY(DeviceHub::instance()->isSimulated());

        InstrumentRegistry* registry = InstrumentRegistry::instance();
        registry->registerInstrument({.id = u"scope"_s, .title = u"Oscilloscope"_s, .shortTitle = u"Scope"_s,
                                      .module = u"Test.Scope"_s, .order = 1,
                                      .createController = [] { return new AiInstrument(u"scope"_s); }});
        registry->registerInstrument({.id = u"bode"_s, .title = u"Bode Analyzer"_s, .shortTitle = u"Bode"_s,
                                      .module = u"Test.Bode"_s, .order = 2,
                                      .createController = [] { return new AiInstrument(u"bode"_s); }});
        registry->createAll();
    }

    void cleanupTestCase() { InstrumentRegistry::instance()->shutdown(); }

    void init() { Notifications::instance()->clear(); }

    // Row 23: at most three toasts stacked (app.md 9.1), the newest kept.
    void toastStackIsThree()
    {
        Notifications* n = Notifications::instance();
        for (int i = 0; i < 5; ++i)
            n->show(Notifications::Info, u"Toast %1"_s.arg(i));
        QCOMPARE(n->count(), 3);
        QCOMPARE(n->data(n->index(0), Notifications::TitleRole).toString(), u"Toast 2"_s);
        QCOMPARE(n->data(n->index(2), Notifications::TitleRole).toString(), u"Toast 4"_s);
    }

    // Row 23: an error toast without an action gets `Details` (opens the event log).
    void errorToastHasDetails()
    {
        Notifications* n = Notifications::instance();
        QSignalSpy details(n, &Notifications::detailsRequested);
        const int id = n->show(Notifications::Error, u"Oscilloscope"_s, u"The device did not answer."_s, {}, {}, u"scope"_s);
        QCOMPARE(n->data(n->index(0), Notifications::ActionTextRole).toString(), u"Details"_s);
        n->trigger(id);
        QCOMPARE(details.count(), 1);
        QCOMPARE(details.at(0).at(0).toString(), u"scope"_s);
        QCOMPARE(n->count(), 0);

        // An explicit action is kept; other levels get none.
        bool ran = false;
        const int other = n->show(Notifications::Error, u"x"_s, {}, u"Retry"_s, [&ran] { ran = true; });
        QCOMPARE(n->data(n->index(0), Notifications::ActionTextRole).toString(), u"Retry"_s);
        n->trigger(other);
        QVERIFY(ran);
        QCOMPARE(details.count(), 1);
        n->show(Notifications::Warning, u"w"_s);
        QCOMPARE(n->data(n->index(0), Notifications::ActionTextRole).toString(), QString());
    }

    // Row 23: full titles in the pause text; no toast when the paused panel is on screen.
    void pauseToastOnlyWhenHidden()
    {
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        InstrumentController* scope = registry->controller(u"scope"_s);
        InstrumentController* bode = registry->controller(u"bode"_s);
        ResourceArbiter* arbiter = ResourceArbiter::instance();
        QSignalSpy visibleChanged(arbiter, &ResourceArbiter::visibleInstrumentsChanged);

        // Scope's panel is visible: the banner says it, no toast.
        arbiter->setVisibleInstruments({u"scope"_s, u"bode"_s});
        QCOMPARE(visibleChanged.count(), 1);
        QCOMPARE(arbiter->visibleInstruments(), (QStringList{u"scope"_s, u"bode"_s}));
        scope->run();
        QTRY_COMPARE(scope->state(), InstrumentController::Running);
        bode->run();
        QTRY_COMPARE(scope->state(), InstrumentController::Paused);
        QCOMPARE(scope->pauseReason(), u"Oscilloscope paused — AI in use by Bode Analyzer"_s);
        QCOMPARE(toastsFrom(u"pause:scope"_s), 0);
        QTRY_COMPARE(bode->state(), InstrumentController::Running);

        // Bode's panel is not visible when Scope takes AI back: toast with Resume.
        arbiter->setVisibleInstruments({u"scope"_s});
        scope->resume();
        QTRY_COMPARE(bode->state(), InstrumentController::Paused);
        QCOMPARE(toastsFrom(u"pause:bode"_s), 1);
        QCOMPARE(Notifications::instance()->data(Notifications::instance()->index(Notifications::instance()->count() - 1),
                                                 Notifications::TitleRole).toString(),
                 u"Bode Analyzer paused — AI in use by Oscilloscope"_s);
        QTRY_COMPARE(scope->state(), InstrumentController::Running);

        scope->stop();
        bode->stop();
        QTRY_COMPARE(scope->state(), InstrumentController::Idle);
        QTRY_COMPARE(bode->state(), InstrumentController::Idle);
        arbiter->setVisibleInstruments({});
    }

    // Row 26: warnings count with errors; entries carry technical details.
    void eventLogWarningsAndDetails()
    {
        EventLog* log = EventLog::instance();
        log->clear();
        QSignalSpy changed(log, &EventLog::countChanged);
        EventLog::log(EventLog::Warning, u"scope"_s, u"Input over range"_s);
        EventLog::log(EventLog::Error, u"scope"_s, u"The analog input is busy."_s,
                      EventLog::errorDetails(mydaq::DeviceError::ResourceBusy, u"AI subsystem reserved"_s));
        EventLog::log(EventLog::Info, u"device"_s, u"Connected"_s);
        QCOMPARE(log->count(), 3);
        QCOMPARE(log->warningCount(), 1);
        QCOMPARE(log->errorCount(), 1);
        QCOMPARE(log->unreadIssueCount(), 2);
        QCOMPARE(log->data(log->index(1), EventLog::DetailsRole).toString(), u"ResourceBusy: AI subsystem reserved"_s);
        QCOMPARE(log->data(log->index(0), EventLog::DetailsRole).toString(), QString());
        QVERIFY(log->roleNames().values().contains("details"));
        QVERIFY(log->toText().contains(u"[ResourceBusy: AI subsystem reserved]"_s));
        log->markRead();
        QCOMPARE(log->unreadIssueCount(), 0);
        QCOMPARE(log->warningCount(), 1);

        QCOMPARE(EventLog::errorDetails(mydaq::DeviceError::Timeout, {}), u"Timeout"_s);
        QCOMPARE(EventLog::errorDetails(-1, u"plain"_s), u"plain"_s);
        QCOMPARE(EventLog::errorCodeName(mydaq::DeviceError::UsbError), u"UsbError"_s);
        log->clear();
        QCOMPARE(log->warningCount(), 0);
    }

    // Row 27: hot-plug polling twice a second (ELVISmx rate): an unplug/replug of the simulator
    // is noticed within one polling period, without an explicit rescan.
    void hotPlugPollingIsFast()
    {
        auto sim = DeviceHub::instance()->simulator();
        QVERIFY(sim);
        mydaq::DeviceManager& manager = mydaq::DeviceManager::instance();
        QSignalSpy changed(&manager, &mydaq::DeviceManager::devicesChanged);

        sim->setConnected(false);
        QVERIFY(changed.wait(3000));   // aligns with the polling timer
        QVERIFY(!manager.device(u"sim:0"_s));
        QElapsedTimer elapsed;
        elapsed.start();
        sim->setConnected(true);
        QVERIFY(changed.wait(3000));
        QVERIFY(manager.device(u"sim:0"_s));
        QVERIFY2(elapsed.elapsed() < 1500, qPrintable(u"replug seen after %1 ms"_s.arg(elapsed.elapsed())));
        QTRY_VERIFY(DeviceHub::instance()->isConnected());
    }

    // Row 28: test signals and DMM values of the simulator through DeviceHub.
    void simulatorControls()
    {
        DeviceHub* hub = DeviceHub::instance();
        QSignalSpy changed(hub, &DeviceHub::simulatorChanged);

        QVariantList signalList = hub->simulatorTestSignals();
        QCOMPARE(signalList.size(), 4);
        QCOMPARE(signalList[0].toMap().value(u"frequency"_s).toDouble(), 1000.0);   // AI 0 default
        QCOMPARE(signalList[1].toMap().value(u"waveform"_s).toInt(), int(mydaq::dsp::Waveform::Square));

        hub->setSimulatorTestSignal(0, {{u"waveform"_s, int(mydaq::dsp::Waveform::Triangle)},
                                        {u"frequency"_s, 250.0}, {u"amplitude"_s, 0.5}});
        QCOMPARE(changed.count(), 1);
        const mydaq::SimTestSignal ai0 = hub->simulator()->testSignal(mydaq::AiChannel::AI0);
        QCOMPARE(ai0.waveform, mydaq::dsp::Waveform::Triangle);
        QCOMPARE(ai0.frequency, 250.0);
        QCOMPARE(ai0.amplitude, 0.5);
        QVERIFY(!ai0.harmonics.empty());   // untouched keys are kept
        hub->setSimulatorTestSignal(0, {{u"dutyCycle"_s, 5.0}});   // clamped
        QCOMPARE(hub->simulator()->testSignal(mydaq::AiChannel::AI0).dutyCycle, 0.99);
        hub->setSimulatorTestSignal(9, {{u"frequency"_s, 1.0}});   // ignored
        QCOMPARE(changed.count(), 2);

        QVariantList dmm = hub->simulatorDmmValues();
        QCOMPARE(dmm.size(), 7);
        QCOMPARE(dmm[0].toDouble(), 1.2345);   // DC V default
        hub->setSimulatorDmmValue(int(mydaq::DmmFunction::Resistance), 1000.0);
        QCOMPARE(hub->simulator()->dmmValue(mydaq::DmmFunction::Resistance), 1000.0);
        QCOMPARE(hub->simulatorDmmValues()[4].toDouble(), 1000.0);
        QCOMPARE(changed.count(), 3);

        hub->setSimulatorNoiseRms(2e-3);
        QCOMPARE(hub->simulator()->noiseRms(), 2e-3);

        // Back to the defaults for the other tests.
        mydaq::SimTestSignal def;
        def.harmonics = {0, 0.01};
        hub->simulator()->setTestSignal(mydaq::AiChannel::AI0, def);
        hub->setSimulatorDmmValue(int(mydaq::DmmFunction::Resistance), 4700.0);
        hub->setSimulatorNoiseRms(0.5e-3);
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestShellRows)
#include "tst_shellrows.moc"
