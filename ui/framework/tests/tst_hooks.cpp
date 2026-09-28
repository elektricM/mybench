// Tests of the framework hooks for instruments (instruments/README.md "Framework hooks"):
// panel close / session reset / safe state, stale worker results, restart clearing, Single
// enable, user-input validation, device capabilities, presets and open requests, palette
// commands and instrument-specific export writers. Simulated myDAQ, no window.
#include <mybench/framework.h>

#include <mydaq/sim_device.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace mybench;
using namespace Qt::StringLiterals;

// Drives DIO outputs while running (like the Digital Writer) and keeps them driven when idle;
// its safe state puts every line back to input.
class OutputInstrument : public InstrumentController
{
    Q_OBJECT
    Q_PROPERTY(int tag READ tag WRITE setTag NOTIFY tagChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(double level READ level WRITE setLevel NOTIFY levelChanged)

public:
    enum Mode { Static, Pattern };
    Q_ENUM(Mode)

    explicit OutputInstrument(const QString& id) : InstrumentController(id)
    {
        setRequiredResources(ResourceArbiter::DigitalLines, 0x0F);
    }
    ~OutputInstrument() override { shutdown(); }

    int tag() const { return m_tag; }
    void setTag(int tag)
    {
        if (tag != m_tag) {
            m_tag = tag;
            emit tagChanged();
        }
    }
    Mode mode() const { return m_mode; }
    void setMode(Mode mode)
    {
        if (mode != m_mode) {
            m_mode = mode;
            emit modeChanged();
        }
    }
    double level() const { return m_level; }
    void setLevel(double level)
    {
        if (level != m_level) {
            m_level = level;
            emit levelChanged();
        }
    }

    // Test access to protected API.
    using InstrumentController::addCommand;
    using InstrumentController::discardPendingResults;
    using InstrumentController::removeCommand;
    using InstrumentController::restart;
    using InstrumentController::setHoldsOutputs;
    using InstrumentController::setSingleEnabled;
    using InstrumentController::setUnsavedDocument;

    QList<int> received;        // tags delivered by post() / publish()
    int safeStateCount = 0;
    int restartCount = 0;
    bool rejectInput = false;   // createWorker() throws InvalidInput
    bool rejectInLoop = false;  // the worker throws InvalidInput
    bool deviceFailure = false; // the worker throws a DeviceError
    std::atomic<bool> workerRunning{false};
    bool workerRunningAtSafeState = false;

signals:
    void tagChanged();
    void modeChanged();
    void levelChanged();

protected:
    WorkerFunction createWorker() override
    {
        if (rejectInput)
            throw InvalidInput(u"The pattern is empty"_s);
        const int tag = m_tag;
        const bool rejectLater = rejectInLoop;
        const bool fail = deviceFailure;
        return [this, tag, rejectLater, fail](WorkerContext& ctx) {
            workerRunning = true;
            struct Done {
                std::atomic<bool>& flag;
                ~Done() { flag = false; }
            } done{workerRunning};
            if (rejectLater)
                throw InvalidInput(u"Line 3 is used by the counter"_s);
            if (fail)
                throw mydaq::DeviceError(mydaq::DeviceError::ResourceBusy, u"endpoint 0x04 stalled"_s);
            ctx.device().dioSetDirection(0x0F, true);
            ctx.post([this] { setHoldsOutputs(true); });
            while (!ctx.stopRequested()) {
                ctx.post([this, tag] { received.append(tag); });
                ctx.publish([this, tag] { received.append(tag); });
                if (ctx.once())
                    break;
                ctx.sleepMs(1);
            }
        };
    }
    void onRestart() override { ++restartCount; }
    void applySafeState() override
    {
        ++safeStateCount;
        workerRunningAtSafeState = workerRunning;
        DeviceHub::instance()->device()->dioSetDirection(0xFF, false);
        setHoldsOutputs(false);
    }
    DataTable exportTable() const override
    {
        DataTable table;
        table.addColumn(u"Tag"_s, {}, {double(m_tag)});
        table.extra.insert(u"dt"_s, 0.5);
        return table;
    }

private:
    int m_tag = 1;
    Mode m_mode = Static;
    double m_level = 0;
};

// Writes "HOOK <dt>" for the instrument-specific writer test.
class HookWriter : public TableWriter
{
public:
    QString name() const override { return u"Hook text log"_s; }
    QStringList extensions() const override { return {u"txt"_s}; }
    bool write(const DataTable& table, const QStringList&, QIODevice& out, QString*) const override
    {
        out.write("HOOK " + QByteArray::number(table.extra.value(u"dt"_s).toDouble()));
        return true;
    }
};

class TestHooks : public QObject
{
    Q_OBJECT

    OutputInstrument* out() { return qobject_cast<OutputInstrument*>(InstrumentRegistry::instance()->controller(u"out"_s)); }
    std::shared_ptr<mydaq::SimDevice> sim() { return DeviceHub::instance()->simulator(); }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_hooks"_s);
        QVERIFY(DeviceHub::instance()->isConnected());
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        registry->registerInstrument({.id = u"out"_s, .title = u"Output Box"_s, .shortTitle = u"Out"_s,
                                      .module = u"Test.Out"_s, .order = 1,
                                      .createController = [] { return new OutputInstrument(u"out"_s); }});
        registry->registerInstrument({.id = u"lazy"_s, .title = u"Lazy"_s, .module = u"Test.Lazy"_s, .order = 2,
                                      .createController = [] { return new OutputInstrument(u"lazy"_s); }});
        QVERIFY(out());
    }
    void cleanupTestCase() { InstrumentRegistry::instance()->shutdown(); }

    void cleanup()
    {
        OutputInstrument* c = out();
        c->stop();
        QTRY_VERIFY(c->isIdle());
        c->returnToSafeState();
        c->rejectInput = c->rejectInLoop = c->deviceFailure = false;
        c->setSingleEnabled(true);
        c->clearInputWarning();
        c->clearError();
    }

    // --- Panel close / session reset / Stop of an idle output holder ------------------------

    void closePanelStopsAndAppliesSafeStateAfterTheWorker()
    {
        OutputInstrument* c = out();
        c->run();
        QTRY_VERIFY(c->holdsOutputs());
        QCOMPARE(sim()->dioDirection(), quint8(0x0F));
        const int before = c->safeStateCount;
        QSignalSpy closed(c, &InstrumentController::panelClosed);
        c->closePanel();
        QCOMPARE(closed.count(), 1);
        QTRY_COMPARE(c->state(), InstrumentController::Idle);
        QTRY_COMPARE(c->safeStateCount, before + 1);
        QVERIFY(!c->workerRunningAtSafeState);   // applied once the worker released the device
        QCOMPARE(sim()->dioDirection(), quint8(0));
        QVERIFY(!c->holdsOutputs());
    }

    void notifyPanelClosedNeverCreatesAController()
    {
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        const qsizetype before = registry->controllers().size();
        registry->notifyPanelClosed(u"lazy"_s);
        QCOMPARE(registry->controllers().size(), before);
        const int count = out()->safeStateCount;
        registry->notifyPanelClosed(u"out"_s);
        QCOMPARE(out()->safeStateCount, count + 1);
    }

    void stopOfAnIdleHolderReleasesTheOutputs()
    {
        OutputInstrument* c = out();
        c->runOnce();
        QTRY_COMPARE(c->state(), InstrumentController::Idle);
        QVERIFY(c->holdsOutputs());   // a Single leaves the lines driven (like ELVISmx)
        const int before = c->safeStateCount;
        c->stop();
        QCOMPARE(c->safeStateCount, before + 1);
        QVERIFY(!c->holdsOutputs());

        c->runOnce();
        QTRY_COMPARE(c->state(), InstrumentController::Idle);
        QVERIFY(c->holdsOutputs());
        QVERIFY(InstrumentRegistry::instance()->anyStoppable());   // Stop all enabled
        InstrumentRegistry::instance()->stopAll();
        QVERIFY(!InstrumentRegistry::instance()->anyStoppable());
        QVERIFY(!c->holdsOutputs());
        QCOMPARE(c->safeStateCount, before + 2);
    }

    void sessionResetReleasesOutputs()
    {
        OutputInstrument* c = out();
        c->run();
        QTRY_VERIFY(c->holdsOutputs());
        QSignalSpy reset(c, &InstrumentController::sessionReset);
        SessionManager::instance()->newSession();
        QCOMPARE(reset.count(), 1);
        QTRY_VERIFY(c->isIdle());
        QTRY_VERIFY(!c->holdsOutputs());
        QCOMPARE(sim()->dioDirection(), quint8(0));
    }

    // --- Stale results and restart ------------------------------------------------------------

    void restartDropsResultsOfTheOldRun()
    {
        OutputInstrument* c = out();
        c->setTag(1);
        c->run();
        QTRY_VERIFY(c->received.size() > 20);
        // Let results of tag 1 pile up in the event queue, then change the setting and restart
        // without processing events in between: none of those may be delivered afterwards.
        QTest::qSleep(20);
        c->setTag(2);
        QSignalSpy restarting(c, &InstrumentController::restarting);
        const int restarts = c->restartCount;
        c->received.clear();
        c->restart();
        QTRY_VERIFY(c->received.size() > 20);
        QVERIFY2(!c->received.contains(1), "a result of the previous run was delivered after restart()");
        QCOMPARE(restarting.count(), 1);
        QCOMPARE(c->restartCount, restarts + 1);
    }

    void discardPendingResultsDropsQueuedResults()
    {
        OutputInstrument* c = out();
        c->run();
        QTRY_VERIFY(c->received.size() > 5);
        QTest::qSleep(20);   // queue some
        c->received.clear();
        c->discardPendingResults();
        QTest::qWait(30);
        QVERIFY(c->received.isEmpty());
        QVERIFY(c->runGeneration() > 0);
    }

    // --- Single enable ------------------------------------------------------------------------

    void singleCanBeDisabled()
    {
        OutputInstrument* c = out();
        QSignalSpy changed(c, &InstrumentController::singleEnabledChanged);
        c->setSingleEnabled(false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!c->singleEnabled());
        c->runOnce();
        QCOMPARE(c->state(), InstrumentController::Idle);
        c->setSingleEnabled(true);
        QVERIFY(c->singleEnabled());
    }

    // --- User-input validation ----------------------------------------------------------------

    void invalidInputIsAWarningNotAnError()
    {
        OutputInstrument* c = out();
        c->rejectInput = true;
        c->run();
        QCOMPARE(c->state(), InstrumentController::Idle);
        QVERIFY(!c->hasError());
        QCOMPARE(c->inputWarning(), u"The pattern is empty."_s);
        c->rejectInput = false;
        c->run();
        QVERIFY(c->inputWarning().isEmpty());   // cleared by the next start
    }

    void invalidInputFromTheWorker()
    {
        OutputInstrument* c = out();
        c->rejectInLoop = true;
        c->run();
        QTRY_COMPARE(c->state(), InstrumentController::Idle);
        QVERIFY(!c->hasError());
        QCOMPARE(c->inputWarning(), u"Line 3 is used by the counter."_s);
    }

    void deviceErrorDetailsInTheEventLog()   // audit row 26: code name + backend text
    {
        OutputInstrument* c = out();
        c->deviceFailure = true;
        c->run();
        QTRY_VERIFY(c->hasError());
        const QString log = EventLog::instance()->toText();
        QVERIFY2(log.contains(u"ResourceBusy"_s) && log.contains(u"endpoint 0x04 stalled"_s), qPrintable(log.right(300)));
    }

    // --- Capabilities -------------------------------------------------------------------------

    void capabilities()
    {
        DeviceHub* hub = DeviceHub::instance();
        QVERIFY(hub->unsupportedFeatures().isEmpty());
        QVERIFY(hub->supports(QString::fromLatin1(mydaq::feature::CounterPulseWidth)));
        QSignalSpy changed(hub, &DeviceHub::capabilitiesChanged);
        hub->setSimulatorUnsupportedFeatures({QString::fromLatin1(mydaq::feature::CounterPulseWidth)});
        QCOMPARE(changed.count(), 1);
        QVERIFY(!hub->supports(u"counter.pulseWidth"_s));
        QVERIFY(hub->device()->supports(u"counter.semiPeriod"_s));
        QVERIFY(!hub->unsupportedReason(u"counter.pulseWidth"_s).isEmpty());
        // Unplugged: the list is kept (options stay greyed out).
        hub->setSimulatorPlugged(false);
        QTRY_VERIFY(!hub->isConnected());
        QVERIFY(!hub->supports(u"counter.pulseWidth"_s));
        hub->setSimulatorPlugged(true);
        QTRY_VERIFY(hub->isConnected());
        hub->setSimulatorUnsupportedFeatures({});
        QVERIFY(hub->supports(u"counter.pulseWidth"_s));
    }

    // --- Presets and open requests ------------------------------------------------------------

    void presetAndOpenRequest()
    {
        InstrumentRegistry* registry = InstrumentRegistry::instance();
        QSignalSpy open(registry, &InstrumentRegistry::openRequested);
        QVERIFY(registry->openInstrument(u"out"_s, {{u"mode"_s, u"Pattern"_s}, {u"level"_s, 0.25}, {u"bogus"_s, 1}}, true));
        QCOMPARE(open.count(), 1);
        QCOMPARE(open.at(0).at(0).toString(), u"out"_s);
        QCOMPARE(open.at(0).at(1).toBool(), true);
        QCOMPARE(out()->mode(), OutputInstrument::Pattern);
        QCOMPARE(out()->level(), 0.25);
        QVERIFY(!registry->openInstrument(u"nope"_s));
        out()->applyPreset({{u"state"_s, 3}});   // base-class properties are not presets
        QCOMPARE(out()->state(), InstrumentController::Idle);
    }

    // --- Palette commands ---------------------------------------------------------------------

    void commands()
    {
        OutputInstrument* c = out();
        QSignalSpy changed(c, &InstrumentController::commandsChanged);
        double level = -1;
        c->addCommand({.id = u"set"_s, .title = u"Set level"_s, .argumentLabel = u"Level (V)"_s,
                       .argumentUnit = u"V"_s, .run = [&](const QVariant& v) { level = v.toDouble(); }});
        c->addCommand({.id = u"qml"_s, .title = u"Handled in QML"_s});
        bool allowed = false;
        c->addCommand({.id = u"gated"_s, .title = u"Gated"_s, .run = [](const QVariant&) {},
                       .enabled = [&] { return allowed; }});
        QCOMPARE(changed.count(), 3);
        const QVariantList list = c->commands();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).toMap().value(u"argumentLabel"_s).toString(), u"Level (V)"_s);
        QCOMPARE(list.at(2).toMap().value(u"enabled"_s).toBool(), false);

        c->invokeCommand(u"set"_s, u"500m"_s);
        QCOMPARE(level, 0.5);
        c->invokeCommand(u"set"_s, u"abc"_s);
        QCOMPARE(level, 0.5);
        QVERIFY(!c->inputWarning().isEmpty());

        QSignalSpy triggered(c, &InstrumentController::commandTriggered);
        c->invokeCommand(u"qml"_s);
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.at(0).at(0).toString(), u"qml"_s);
        c->removeCommand(u"qml"_s);
        QCOMPARE(c->commands().size(), 2);
    }

    // --- Instrument-specific export writers ---------------------------------------------------

    void instrumentWriter()
    {
        DataExport* exporter = DataExport::instance();
        exporter->registerWriter(std::make_unique<HookWriter>(), u"out"_s);
        QCOMPARE(exporter->writerFor(u"a.txt"_s, u"out"_s)->name(), u"Hook text log"_s);
        QCOMPARE(exporter->writerFor(u"a.txt"_s)->name(), u"Tab-separated"_s);
        QCOMPARE(exporter->writerFor(u"a.txt"_s, u"other"_s)->name(), u"Tab-separated"_s);
        const QStringList filters = exporter->nameFiltersFor(u"out"_s);
        QCOMPARE(filters.first(), u"Hook text log (*.txt)"_s);
        QVERIFY(filters.contains(u"Tab-separated (*.tsv)"_s));
        QVERIFY(!exporter->nameFilters().join(u' ').contains(u"Hook"_s));

        OutputInstrument* c = out();
        c->runOnce();
        QTRY_COMPARE(c->state(), InstrumentController::Idle);
        const QString path = m_settingsDir.filePath(u"log.txt"_s);
        QVERIFY(c->exportData(QUrl::fromLocalFile(path)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("HOOK 0.5"));
    }

    void unsavedDocument()
    {
        OutputInstrument* c = out();
        QSignalSpy changed(c, &InstrumentController::unsavedDocumentChanged);
        c->setUnsavedDocument(u"Waveform ramp.wdt"_s);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(InstrumentRegistry::instance()->unsavedDocuments(), QStringList{u"Output Box: Waveform ramp.wdt"_s});
        c->setUnsavedDocument({});
        QVERIFY(InstrumentRegistry::instance()->unsavedDocuments().isEmpty());
    }

    // --- View state in sessions (audit row 18: collapsed inspector sections) -------------------

    void viewStateRoundTrip()
    {
        SessionManager* sessions = SessionManager::instance();
        sessions->newSession();
        QVERIFY(!sessions->viewValue(u"collapsed/out/Timebase"_s, false).toBool());
        sessions->setViewValue(u"collapsed/out/Timebase"_s, true);
        QVERIFY(sessions->viewValue(u"collapsed/out/Timebase"_s).toBool());
        QVERIFY(sessions->isModified());
        const QString path = m_settingsDir.filePath(u"view.mybench"_s);
        QVERIFY(sessions->save(QUrl::fromLocalFile(path)));
        sessions->newSession();
        QVERIFY(!sessions->viewValue(u"collapsed/out/Timebase"_s, false).toBool());
        QSignalSpy restored(sessions, &SessionManager::viewStateChanged);
        QVERIFY(sessions->load(QUrl::fromLocalFile(path)));
        QVERIFY(restored.count() >= 1);
        QVERIFY(sessions->viewValue(u"collapsed/out/Timebase"_s).toBool());
    }

    // --- Print options (audit row 11) ---------------------------------------------------------

    void lastFolderPerFileType()   // audit row 16: last directories per file type
    {
        DataExport* exporter = DataExport::instance();
        QDir(m_settingsDir.path()).mkpath(u"csvdir"_s);
        QDir(m_settingsDir.path()).mkpath(u"pngdir"_s);
        DataTable table;
        table.addColumn(u"A"_s, {}, {1, 2});
        QVERIFY(exporter->write(m_settingsDir.filePath(u"csvdir/a.csv"_s), table));
        QImage grab(10, 10, QImage::Format_ARGB32);
        grab.fill(Qt::black);
        const QString source = m_settingsDir.filePath(u"g.png"_s);
        QVERIFY(grab.save(source));
        QVERIFY(exporter->writeImage(QUrl::fromLocalFile(m_settingsDir.filePath(u"pngdir/b.png"_s)), source, u"X"_s));
        QCOMPARE(exporter->lastFolderFor(u"csv"_s).toLocalFile(), m_settingsDir.filePath(u"csvdir"_s));
        QCOMPARE(exporter->lastFolderFor(u"png"_s).toLocalFile(), m_settingsDir.filePath(u"pngdir"_s));
        QVERIFY(exporter->suggestedFileUrl(u"scope"_s, u"csv"_s).toLocalFile().startsWith(m_settingsDir.filePath(u"csvdir"_s)));
        QCOMPARE(exporter->lastFolderFor(u"pdf"_s), exporter->lastFolder());   // never used: the last folder
    }

    void inkSaver()
    {
        QImage image(4, 1, QImage::Format_ARGB32);
        image.setPixelColor(0, 0, QColor(6, 7, 9));          // display background
        image.setPixelColor(1, 0, QColor(229, 231, 235));    // light text
        image.setPixelColor(2, 0, QColor(74, 222, 128));     // CH0 green trace
        image.setPixelColor(3, 0, QColor(40, 42, 46));       // grid
        const QImage out = DataExport::inkSaver(image);
        QVERIFY(out.pixelColor(0, 0).lightness() > 240);     // white paper
        QVERIFY(out.pixelColor(1, 0).lightness() < 40);      // dark text
        const QColor trace = out.pixelColor(2, 0);
        QVERIFY(trace.lightness() < 120);                    // readable on white
        QVERIFY(std::abs(trace.hslHue() - QColor(74, 222, 128).hslHue()) < 6);   // still green
        QVERIFY(out.pixelColor(3, 0).lightness() > 200);     // light grid
    }

    void identityLine()
    {
        const QVariantMap who = DataExport::instance()->identity();
        QVERIFY(!who.value(u"computer"_s).toString().isEmpty());
        const QString line = DataExport::identityLine({{u"userName"_s, true}, {u"computerName"_s, true}, {u"timeStamp"_s, false}});
        QVERIFY(line.contains(u"User: "_s + who.value(u"user"_s).toString()));
        QVERIFY(line.contains(u"Computer: "_s + who.value(u"computer"_s).toString()));
        QVERIFY(DataExport::identityLine({{u"timeStamp"_s, false}}).isEmpty());
    }

    void imageWithHeaderAndSettingsTable()
    {
        QImage grab(800, 400, QImage::Format_ARGB32);
        grab.fill(QColor(6, 7, 9));
        const QString source = m_settingsDir.filePath(u"grab.png"_s);
        QVERIFY(grab.save(source));
        const QVariantMap metadata{{u"Timebase"_s, u"1 ms/div"_s}, {u"Trigger"_s, u"Edge, Chan 0"_s}};
        const QString plain = m_settingsDir.filePath(u"plain.png"_s);
        QVERIFY(DataExport::instance()->writeImage(QUrl::fromLocalFile(plain), source, u"Scope"_s, metadata));
        QCOMPARE(QImage(plain).size(), grab.size());   // no options: a plain copy
        const QString decorated = m_settingsDir.filePath(u"decorated.png"_s);
        QVERIFY(DataExport::instance()->writeImage(QUrl::fromLocalFile(decorated), source, u"Scope"_s, metadata,
                                                   {{u"inkSaver"_s, true}, {u"userName"_s, true}, {u"settingsTable"_s, true}}));
        const QImage out(decorated);
        QVERIFY(out.height() > grab.height() + 60);
        QVERIFY(out.pixelColor(out.width() / 2, out.height() / 2).lightness() > 240);   // ink saver
    }

    void pdfOrientation()
    {
        QImage grab(400, 200, QImage::Format_ARGB32);
        grab.fill(Qt::black);
        const QString source = m_settingsDir.filePath(u"grab2.png"_s);
        QVERIFY(grab.save(source));
        const QString pdf = m_settingsDir.filePath(u"p.pdf"_s);
        QVERIFY(DataExport::instance()->writePdf(QUrl::fromLocalFile(pdf), source, u"Bode"_s, {{u"Start"_s, u"10 Hz"_s}},
                                                 {{u"orientation"_s, u"portrait"_s}, {u"computerName"_s, true}}));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("%PDF"));
        // A4 portrait media box: 595 x 842 pt.
        QVERIFY(bytes.contains("/MediaBox [0 0 595") || bytes.contains("/MediaBox [0 0 594"));
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestHooks)
#include "tst_hooks.moc"
