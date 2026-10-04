// Tests of MyBench.Framework: run state machine, worker errors, resource arbitration,
// settings persistence, sessions and export, against the simulated myDAQ.
#include <mybench/framework.h>

#include <mydaq/device_manager.h>

#include <QFile>
#include <QJsonArray>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace mybench;
using namespace Qt::StringLiterals;

// An AI instrument: continuous 1 kS/s reads of 50 samples.
class AiInstrument : public InstrumentController
{
    Q_OBJECT
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(Shape shape READ shape WRITE setShape NOTIFY shapeChanged)
    Q_PROPERTY(QList<double> points READ points WRITE setPoints NOTIFY pointsChanged)
    Q_PROPERTY(int viewState READ viewState WRITE setViewState NOTIFY viewStateChanged STORED false)
    Q_PROPERTY(double lastMean READ lastMean NOTIFY lastMeanChanged)

public:
    enum Shape { Sine, Square, Triangle };
    Q_ENUM(Shape)

    explicit AiInstrument(const QString& id, bool fail = false) : InstrumentController(id), m_fail(fail)
    {
        setRequiredResources(ResourceArbiter::AnalogInput);
    }
    ~AiInstrument() override { shutdown(); }

    double rate() const { return m_rate; }
    void setRate(double rate)
    {
        if (rate != m_rate) {
            m_rate = rate;
            emit rateChanged();
        }
    }
    Shape shape() const { return m_shape; }
    void setShape(Shape shape)
    {
        if (shape != m_shape) {
            m_shape = shape;
            emit shapeChanged();
        }
    }
    QList<double> points() const { return m_points; }
    void setPoints(const QList<double>& points)
    {
        if (points != m_points) {
            m_points = points;
            emit pointsChanged();
        }
    }
    int viewState() const { return m_viewState; }
    void setViewState(int v)
    {
        if (v != m_viewState) {
            m_viewState = v;
            emit viewStateChanged();
        }
    }
    double lastMean() const { return m_lastMean; }

signals:
    void rateChanged();
    void shapeChanged();
    void pointsChanged();
    void viewStateChanged();
    void lastMeanChanged();

protected:
    WorkerFunction createWorker() override
    {
        mydaq::AiConfig config;
        config.channels = {{mydaq::AiChannel::AI0, 10.0}};
        config.sampleRate = m_rate;
        config.mode = mydaq::SampleMode::Continuous;
        config.samplesPerChannel = 1000;
        const bool fail = m_fail;
        return [this, config, fail](WorkerContext& ctx) {
            if (fail)
                throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig, u"bad channel"_s);
            auto task = ctx.device().createAiTask(config);
            task->start();
            ctx.setSampleRate(task->sampleRate());
            while (!ctx.stopRequested()) {
                const auto data = task->read(50, 1000);
                ctx.addSamples(50);
                double sum = 0;
                for (double v : data[0])
                    sum += v;
                const double mean = sum / double(data[0].size());
                ctx.publish([this, mean] {
                    m_lastMean = mean;
                    emit lastMeanChanged();
                });
                ctx.acquisitionDone();
                if (ctx.once())
                    break;
            }
        };
    }
    DataTable exportTable() const override
    {
        DataTable table;
        table.addColumn(u"Index"_s, {}, {0, 1, 2});
        table.addColumn(u"AI 0"_s, u"V"_s, {0.5, std::nan(""), -1.25});
        table.addMetadata(u"Rate"_s, u"1 kS/s"_s);
        return table;
    }

private:
    bool m_fail = false;
    double m_rate = 1000;
    Shape m_shape = Sine;
    QList<double> m_points{1, 2};
    int m_viewState = 0;
    double m_lastMean = 0;
};

// A DIO instrument on a line mask (no worker device access besides DIO).
class DioInstrument : public InstrumentController
{
    Q_OBJECT
public:
    DioInstrument(const QString& id, quint8 mask) : InstrumentController(id)
    {
        setRequiredResources(ResourceArbiter::DigitalLines, mask);
    }
    ~DioInstrument() override { shutdown(); }

protected:
    WorkerFunction createWorker() override
    {
        return [](WorkerContext& ctx) {
            while (!ctx.stopRequested()) {
                ctx.device().dioRead();
                if (!ctx.sleepMs(10))
                    break;
            }
        };
    }
};

class TestFramework : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_framework"_s);
        QVERIFY(DeviceHub::instance()->isConnected());
        QVERIFY(DeviceHub::instance()->isSimulated());

        InstrumentRegistry* registry = InstrumentRegistry::instance();
        registry->registerInstrument({.id = u"alpha"_s, .title = u"Alpha Scope"_s, .shortTitle = u"Alpha"_s,
                                      .module = u"Test.Alpha"_s, .order = 1,
                                      .createController = [] { return new AiInstrument(u"alpha"_s); }});
        registry->registerInstrument({.id = u"beta"_s, .title = u"Beta Analyzer"_s, .shortTitle = u"Beta"_s,
                                      .module = u"Test.Beta"_s, .order = 2,
                                      .createController = [] { return new AiInstrument(u"beta"_s); }});
        registry->createAll();
    }

    void cleanupTestCase() { InstrumentRegistry::instance()->shutdown(); }

    void registry()
    {
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        QCOMPARE(registry->count(), 2);
        QCOMPARE(registry->ids(), (QStringList{u"alpha"_s, u"beta"_s}));
        const InstrumentDescriptor* alpha = registry->descriptor(u"alpha"_s);
        QVERIFY(alpha);
        QCOMPARE(alpha->panelUrl(), QUrl(u"qrc:/qt/qml/Test/Alpha/AlphaPanel.qml"_s));
        QCOMPARE(alpha->icon, u"instrument-alpha"_s);
        QCOMPARE(registry->controller(u"alpha"_s)->title(), u"Alpha Scope"_s);
    }

    void runAndStop()
    {
        auto* alpha = qobject_cast<AiInstrument*>(InstrumentRegistry::instance()->controller(u"alpha"_s));
        QVERIFY(alpha->canRun());
        alpha->run();
        QCOMPARE(alpha->state(), InstrumentController::Starting);
        QTRY_COMPARE(alpha->state(), InstrumentController::Running);
        QCOMPARE(ResourceArbiter::instance()->holder(ResourceArbiter::AnalogInput), alpha);
        QTRY_VERIFY(alpha->acquisitionCount() >= 3);
        QVERIFY(alpha->hasData());
        QVERIFY(alpha->sampleRate() > 999 && alpha->sampleRate() < 1001);
        QTRY_VERIFY_WITH_TIMEOUT(alpha->throughput() > 500, 3000);
        alpha->stop();
        QCOMPARE(alpha->state(), InstrumentController::Stopping);
        QTRY_COMPARE(alpha->state(), InstrumentController::Idle);
        QCOMPARE(ResourceArbiter::instance()->holder(ResourceArbiter::AnalogInput), nullptr);
        QVERIFY(!alpha->hasError());
    }

    void single()
    {
        auto* alpha = InstrumentRegistry::instance()->controller(u"alpha"_s);
        const int before = alpha->acquisitionCount();
        QSignalSpy finished(alpha, &InstrumentController::finished);
        alpha->runOnce();
        QCOMPARE(alpha->runMode(), InstrumentController::Once);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(alpha->state(), InstrumentController::Idle);
        QCOMPARE(alpha->acquisitionCount(), before + 1);
    }

    void singleWhileRunningEndsTheRun()
    {
        auto* alpha = InstrumentRegistry::instance()->controller(u"alpha"_s);
        alpha->run();
        QTRY_COMPARE(alpha->state(), InstrumentController::Running);
        alpha->runOnce();   // continuous -> once: the run ends after the current acquisition
        QTRY_COMPARE(alpha->state(), InstrumentController::Idle);
    }

    void preemption()
    {
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        InstrumentController* alpha = registry->controller(u"alpha"_s);
        InstrumentController* beta = registry->controller(u"beta"_s);
        QSignalSpy preempted(ResourceArbiter::instance(), &ResourceArbiter::preempted);

        alpha->run();
        QTRY_COMPARE(alpha->state(), InstrumentController::Running);
        beta->run();   // needs AI too: alpha is paused, beta waits for alpha's task to go away
        QCOMPARE(preempted.count(), 1);
        QTRY_COMPARE(alpha->state(), InstrumentController::Paused);
        QCOMPARE(alpha->pauseReason(), u"Alpha Scope paused — AI in use by Beta Analyzer"_s);   // full titles (app.md 5.2)
        QTRY_COMPARE(beta->state(), InstrumentController::Running);
        QVERIFY(!beta->hasError());
        QCOMPARE(ResourceArbiter::instance()->holder(ResourceArbiter::AnalogInput), beta);
        const QVariantList holders = ResourceArbiter::instance()->holders();
        QCOMPARE(holders.size(), 1);
        QCOMPARE(holders[0].toMap().value(u"shortTitle"_s).toString(), u"Beta"_s);

        alpha->resume();   // takes it back: beta is paused in turn
        QTRY_COMPARE(beta->state(), InstrumentController::Paused);
        QTRY_COMPARE(alpha->state(), InstrumentController::Running);
        QVERIFY(!alpha->hasError());

        alpha->stop();
        beta->stop();   // stop while paused -> idle
        QTRY_COMPARE(alpha->state(), InstrumentController::Idle);
        QCOMPARE(beta->state(), InstrumentController::Idle);
        QVERIFY(!ResourceArbiter::instance()->anyHeld());
    }

    void dioLines()
    {
        DioInstrument low(u"low"_s, 0x0F);
        DioInstrument all(u"all"_s, 0xFF);
        DioInstrument high(u"high"_s, 0xF0);
        low.run();
        high.run();   // disjoint lines: both run
        QTRY_COMPARE(low.state(), InstrumentController::Running);
        QTRY_COMPARE(high.state(), InstrumentController::Running);
        QCOMPARE(ResourceArbiter::dioLinesLabel(0x0F), u"DIO 0–3"_s);
        QCOMPARE(ResourceArbiter::dioLinesLabel(0x25), u"DIO 0, 2, 5"_s);
        all.run();    // overlaps both
        QTRY_COMPARE(low.state(), InstrumentController::Paused);
        QTRY_COMPARE(high.state(), InstrumentController::Paused);
        QTRY_COMPARE(all.state(), InstrumentController::Running);
        all.stop();
        QTRY_COMPARE(all.state(), InstrumentController::Idle);
    }

    void workerError()
    {
        AiInstrument broken(u"broken"_s, true);
        QSignalSpy errors(&broken, &InstrumentController::errorOccurred);
        broken.run();
        QTRY_COMPARE(broken.state(), InstrumentController::Idle);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(broken.errorText(), u"The device rejected the settings: bad channel."_s);
        QCOMPARE(broken.errorHistory().size(), 1);
        QVERIFY(!ResourceArbiter::instance()->anyHeld());
        broken.clearError();
        QVERIFY(!broken.hasError());
    }

    void settings()
    {
        AiInstrument instrument(u"persist"_s);
        instrument.initialize();
        QStringList names;
        for (const QMetaProperty& p : instrument.persistentProperties())
            names << QString::fromLatin1(p.name());
        QCOMPARE(names, (QStringList{u"rate"_s, u"shape"_s, u"points"_s}));

        instrument.setRate(2500);
        instrument.setShape(AiInstrument::Triangle);
        instrument.setPoints({3, 4, 5});
        const QJsonObject json = instrument.saveSettings();
        QCOMPARE(json.value(u"shape"_s).toString(), u"Triangle"_s);
        QCOMPARE(json.value(u"rate"_s).toDouble(), 2500.0);
        QCOMPARE(json.value(u"points"_s).toArray().size(), 3);
        QVERIFY(!json.contains(u"viewState"_s));

        instrument.resetSettings();
        QCOMPARE(instrument.rate(), 1000.0);
        QCOMPARE(instrument.shape(), AiInstrument::Sine);
        instrument.restoreSettings(json);
        QCOMPARE(instrument.rate(), 2500.0);
        QCOMPARE(instrument.shape(), AiInstrument::Triangle);
        QCOMPARE(instrument.points(), (QList<double>{3, 4, 5}));

        // Auto-saved (debounced) and restored by a new instance.
        QTRY_VERIFY(QSettings().contains(u"instruments/persist/settings"_s));
        AiInstrument again(u"persist"_s);
        again.initialize();
        QCOMPARE(again.rate(), 2500.0);
        QCOMPARE(again.shape(), AiInstrument::Triangle);
    }

    void session()
    {
        SessionManager* sessions = SessionManager::instance();
        auto* alpha = qobject_cast<AiInstrument*>(InstrumentRegistry::instance()->controller(u"alpha"_s));
        alpha->setRate(4000);
        sessions->setWorkspace({{u"tabs"_s, QStringList{u"alpha"_s}}});
        const QString file = m_settingsDir.filePath(u"test.mybench"_s);
        QVERIFY(sessions->save(QUrl::fromLocalFile(file)));
        QVERIFY(!sessions->isModified());
        QCOMPARE(sessions->currentName(), u"test"_s);
        QCOMPARE(sessions->recentSessions().size(), 1);

        alpha->setRate(8000);
        QVERIFY(sessions->isModified());
        QSignalSpy restored(sessions, &SessionManager::workspaceRestored);
        QVERIFY(sessions->load(QUrl::fromLocalFile(file)));
        QCOMPARE(alpha->rate(), 4000.0);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(sessions->workspace().value(u"tabs"_s).toStringList(), QStringList{u"alpha"_s});

        sessions->newSession();
        QCOMPARE(alpha->rate(), 1000.0);
        QVERIFY(sessions->workspace().isEmpty());
    }

    void exportCsv()
    {
        auto* alpha = InstrumentRegistry::instance()->controller(u"alpha"_s);
        const QString file = m_settingsDir.filePath(u"data.csv"_s);
        QVERIFY(alpha->exportData(QUrl::fromLocalFile(file)));
        QFile in(file);
        QVERIFY(in.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(in.readAll()).split(u'\n', Qt::SkipEmptyParts);
        QVERIFY(lines[0].startsWith(u"# "_s) && lines[0].endsWith(u" - Alpha Scope"_s));
        QVERIFY(lines[1].startsWith(u"# Date: "_s));
        QVERIFY(lines[2].startsWith(u"# Device: Simulated myDAQ"_s) && lines[2].endsWith(u", simulated"_s));
        QCOMPARE(lines[3], u"# Rate: 1 kS/s"_s);
        QCOMPARE(lines[4], u"Index,AI 0 (V)"_s);
        QCOMPARE(lines[5], u"0,0.5"_s);
        QCOMPARE(lines[6], u"1,"_s);
        QCOMPARE(lines[7], u"2,-1.25"_s);
        QCOMPARE(DataExport::instance()->writerFor(u"x.tsv"_s)->name(), u"Tab-separated"_s);
    }

    void hotPlug()
    {
        DeviceHub* hub = DeviceHub::instance();
        QVERIFY(hub->simulatorAvailable());
        InstrumentController* alpha = InstrumentRegistry::instance()->controller(u"alpha"_s);
        alpha->run();
        QTRY_COMPARE(alpha->state(), InstrumentController::Running);

        hub->setSimulatorPlugged(false);   // unplug: the selection stays, marked disconnected
        QTRY_COMPARE(hub->connectionState(), DeviceHub::Disconnected);
        QTRY_COMPARE(alpha->state(), InstrumentController::Idle);
        QCOMPARE(alpha->errorText(), u"The device was disconnected."_s);
        QVERIFY(alpha->interruptedByDisconnect());
        QVERIFY(!alpha->canRun());
        const int row = hub->devices()->indexOf(u"sim:0"_s);
        QVERIFY(row >= 0);
        QVERIFY(hub->devices()->data(hub->devices()->index(row), DeviceListModel::DisplayNameRole)
                    .toString().endsWith(u"(disconnected)"_s));

        hub->setSimulatorPlugged(true);    // same id comes back: usable again, no automatic restart
        QTRY_COMPARE(hub->connectionState(), DeviceHub::Connected);
        QVERIFY(alpha->canRun());
        QCOMPARE(alpha->state(), InstrumentController::Idle);
        alpha->run();
        QTRY_COMPARE(alpha->state(), InstrumentController::Running);
        QVERIFY(!alpha->interruptedByDisconnect());
        alpha->stop();
        QTRY_COMPARE(alpha->state(), InstrumentController::Idle);
    }

    void units()
    {
        QCOMPARE(Units::format(1234.6, u"Hz"_s), u"1.235 kHz"_s);
        QCOMPARE(Units::parse(u"1.5k"_s, u"Hz"_s), 1500.0);
        QVERIFY(std::isnan(Units::parse(u"abc"_s)));
        QCOMPARE(Units::prefixMultiplier(u"mV"_s, u"V"_s), 1e-3);
        QCOMPARE(Units::prefixMultiplier(u"V"_s, u"V"_s), 1.0);
        QCOMPARE(Units::fixed(3.14159, u"V"_s, 2), u"3.14 V"_s);
    }

    void appInfoOptions()
    {
        AppInfo* info = AppInfo::instance();
        info->setLaunch({u"--instrument"_s, u"scope"_s, u"--tile"_s, u"--run"_s, u"--show-help"_s, u"--sidebar"_s,
                         u"compact"_s, u"--inspector"_s, u"hidden"_s, u"x.mybench"_s},
                        true, u"light"_s);
        QCOMPARE(info->instrument(), u"scope"_s);
        QCOMPARE(info->value(u"sidebar"_s), u"compact"_s);     // shell layout options take a value
        QCOMPARE(info->value(u"inspector"_s), u"hidden"_s);
        QVERIFY(info->flag(u"tile"_s));
        QVERIFY(info->flag(u"run"_s));   // "--run" followed by an option is a flag
        QVERIFY(info->flag(u"show-help"_s));
        QCOMPARE(info->arguments(), QStringList{u"x.mybench"_s});
        QVERIFY(!info->animations());
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_GUILESS_MAIN(TestFramework)
#include "tst_framework.moc"
