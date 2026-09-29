// DioController against the simulator (docs/instruments/dio.md section 11): directions,
// manual pattern and actions, sequences, Single, presets moving lines, custom sets, outputs kept
// after Stop and released on pause / on request, lines of other instruments.
#include "diocontroller.h"

#include <mybench/devicehub.h>
#include <mybench/instrumentregistry.h>
#include <mybench/resourcearbiter.h>

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QSemaphore>
#include <QTest>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <chrono>
#include <thread>

using namespace Qt::StringLiterals;
using mybench::instruments::DioController;

namespace {

// Stand-in for the Counter/Timer: declares DIO 1 (its default input terminal) and idles.
class FakeCounter : public mybench::InstrumentController
{
public:
    FakeCounter() : InstrumentController(u"fakectr"_s)
    {
        setRequiredResources(mybench::ResourceArbiter::Counter | mybench::ResourceArbiter::DigitalLines, 0x02);
    }
    ~FakeCounter() override { shutdown(); }

protected:
    WorkerFunction createWorker() override
    {
        return [](mybench::WorkerContext& ctx) {
            while (ctx.sleepMs(20)) {
            }
        };
    }
};

}  // namespace

class TestDioController : public QObject
{
    Q_OBJECT

    std::shared_ptr<mydaq::SimDevice> m_sim;
    std::unique_ptr<DioController> m_dio;

private slots:
    void initTestCase()
    {
        mybench::InstrumentController::setRestoreSavedSettings(false);
        m_sim = std::dynamic_pointer_cast<mydaq::SimDevice>(mydaq::DeviceManager::instance().device(u"sim:0"_s));
        QVERIFY(m_sim);
        mybench::DeviceHub::instance()->select(u"sim:0"_s);
        QTRY_VERIFY(mybench::DeviceHub::instance()->isConnected());
        mybench::InstrumentRegistry::instance()->registerInstrument({
            .id = u"fakectr"_s, .title = u"Counter/Timer"_s, .shortTitle = u"CTR"_s,
            .module = u"Fake"_s, .createController = [] { return new FakeCounter; },
        });
    }

    void init()
    {
        m_sim->reset();
        m_dio = std::make_unique<DioController>();
        m_dio->initialize();
    }
    void cleanup()
    {
        m_dio->stop();
        QTRY_VERIFY(m_dio->isIdle());
        m_dio.reset();
    }

    void defaults()
    {
        QCOMPARE(m_dio->inputMask(), 0xF0);
        QCOMPARE(m_dio->outputMask(), 0x0F);
        QCOMPARE(m_dio->readPreset(), int(mybench::instruments::dio::Lines4to7));
        QCOMPARE(m_dio->writePreset(), int(mybench::instruments::dio::Lines0to3));
        QCOMPARE(m_dio->pattern(), DioController::Manual);
        QCOMPARE(m_dio->stepInterval(), 100);
        QCOMPARE(m_dio->sampleInterval(), 100);
        QCOMPARE(m_dio->inputRadix(), DioController::Hex);
        QCOMPARE(m_dio->dioLines(), 0xFF);   // exactly the lines in use
        QCOMPARE(m_dio->patternNames().value(1), u"Ramp (0-15)"_s);
    }

    void manualWritesAndReads()
    {
        m_dio->run();
        QTRY_COMPARE(m_dio->drivenMask(), 0x0F);
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));
        QTRY_COMPARE(m_dio->writtenValue(), 0);
        m_dio->toggleLine(2);   // DIO 2's switch
        QTRY_COMPARE(m_dio->writtenValue(), 4);
        QCOMPARE(m_sim->dioRead() & 0x0F, 0x04);
        QTRY_COMPARE(m_dio->lineStates() & 0x0F, 0x04);
        m_dio->applyAction(DioController::Toggle);
        QTRY_COMPARE(m_dio->writtenValue(), 0xB);
        m_dio->applyAction(DioController::Rotate);
        QTRY_COMPARE(m_dio->writtenValue(), 0x7);
        m_dio->setShiftDirection(DioController::Right);
        m_dio->applyAction(DioController::Shift);
        QTRY_COMPARE(m_dio->writtenValue(), 0x3);
        QTRY_VERIFY(m_dio->timeline()->version() > 0);
        QCOMPARE(m_dio->readbackErrors(), 0);
    }

    // The ramp counts up through every value 0..15 and wraps. The steps are paced by the wall
    // clock, so the test waits on a generous deadline for the events (a loaded runner may take
    // seconds for 16 steps of 20 ms) and asserts the order, never the speed.
    void rampSteps()
    {
        m_dio->setStepInterval(20);
        m_dio->setPattern(DioController::Ramp);
        QList<int> values;
        connect(m_dio.get(), &DioController::writtenChanged, this, [&] { values << m_dio->writtenValue(); });
        m_dio->run();
        QTRY_VERIFY_WITH_TIMEOUT(values.size() >= 18, 60000);
        m_dio->stop();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle(), 10000);
        QVERIFY(values.contains(15));
        for (int i = 1; i < values.size(); ++i)
            QCOMPARE(values[i], (values[i - 1] + 1) % 16);   // 0, 1, ... 15, 0 (wraps at 15)
        QVERIFY(values.indexOf(0, 1) > 0);                    // the wrap was seen
    }

    void singleAlternating()
    {
        m_dio->setStepInterval(20);
        m_dio->setPattern(DioController::Alternating);
        QList<int> values;
        connect(m_dio.get(), &DioController::writtenChanged, this, [&] { values << m_dio->writtenValue(); });
        m_dio->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle() && values.size() >= 2, 10000);   // both steps, however slow the runner
        QCOMPARE(values, (QList<int>{0x5, 0xA}));
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));   // Single done: outputs keep driving
    }

    // A release that a controller queued on the thread pool just before being destroyed must land
    // before the next controller drives the lines (Windows CI: it landed late and undrove the next
    // test's Single run). Holding every pool thread makes the late landing deterministic.
    void destroyedControllerReleaseLandsFirst()
    {
        m_dio->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle(), 10000);
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));
        QThreadPool* pool = QThreadPool::globalInstance();
        const int threads = pool->maxThreadCount();
        QSemaphore gate;
        for (int i = 0; i < threads; ++i)
            pool->start([&gate] { gate.acquire(); });
        m_dio->stop();   // idle and driving: queues the release behind the held threads
        std::thread opener([&gate, threads] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            gate.release(threads);
        });
        m_dio.reset();
        m_dio = std::make_unique<DioController>();
        m_dio->initialize();
        m_dio->setStepInterval(20);
        m_dio->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle(), 10000);
        opener.join();
        pool->waitForDone();
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));
    }

    // A release queued for the thread pool (Stop / Release on an idle DIO) must never land after a
    // newer run has claimed the lines again (windows CI: singleAlternating saw direction 0).
    void lateReleaseDoesNotUndriveNewRun()
    {
        m_dio->setPattern(DioController::Manual);
        m_dio->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle() && m_dio->holdsOutputs(), 10000);
        // Occupy every pool thread so the release job stays queued.
        QThreadPool* pool = QThreadPool::globalInstance();
        std::atomic<bool> hold{true};
        for (int i = 0; i < pool->maxThreadCount(); ++i)
            pool->start([&hold] {
                while (hold.load())
                    QThread::msleep(2);
            });
        m_dio->stop();       // idle + driving: queues the release
        m_dio->runOnce();    // the new run drives the same lines again
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle() && m_dio->holdsOutputs(), 10000);
        hold = false;
        QVERIFY(pool->waitForDone(10000));
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));
    }

    void presetMovesLines()
    {
        m_dio->setReadPreset(int(mybench::instruments::dio::Lines0to7));
        QCOMPARE(m_dio->inputMask(), 0xFF);
        QCOMPARE(m_dio->outputMask(), 0x00);
        QCOMPARE(m_dio->writePreset(), int(mybench::instruments::dio::NoLines));
        m_dio->setWritePreset(int(mybench::instruments::dio::Lines0to3));
        QCOMPARE(m_dio->inputMask(), 0xF0);
        QCOMPARE(m_dio->readPreset(), int(mybench::instruments::dio::Lines4to7));
        m_dio->setLineDirection(5, DioController::Unused);
        QCOMPARE(m_dio->readPreset(), int(mybench::instruments::dio::CustomLines));
        QCOMPARE(m_dio->dioLines(), 0xDF);
    }

    void customSet()
    {
        m_dio->setOutputMask(0x52);   // DIO 1, 4, 6
        QCOMPARE(m_dio->inputMask(), 0xA0);
        m_dio->setManualValue(5);
        m_dio->run();
        QTRY_COMPARE(m_dio->writtenLevels(), 0x42);
        QCOMPARE(m_sim->dioRead() & 0x52, 0x42);
    }

    void noLinesRefuses()
    {
        m_dio->setReadPreset(int(mybench::instruments::dio::NoLines));
        m_dio->setWritePreset(int(mybench::instruments::dio::NoLines));
        m_dio->run();
        QTest::qWait(50);
        QVERIFY(m_dio->isIdle());
        QVERIFY(!m_dio->hasError());
    }

    void outputsAfterStopAndRelease()
    {
        m_dio->run();
        QTRY_COMPARE(m_dio->drivenMask(), 0x0F);
        m_dio->stop();
        QTRY_VERIFY(m_dio->isIdle());
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));   // still driving
        m_dio->releaseOutputs();
        QTRY_COMPARE(m_sim->dioDirection(), uint8_t(0x00));
        QCOMPARE(m_dio->drivenMask(), 0);
    }

    // dio.md 3.2 step 5 / app.md 6.6: closing the panel (or a new session) returns every line
    // we drive to input, running or not; a second Stop does it for an idle panel.
    void closePanelReturnsLinesToInputs()
    {
        m_dio->run();
        QTRY_COMPARE(m_dio->drivenMask(), 0x0F);
        QVERIFY(m_dio->holdsOutputs());
        m_dio->closePanel();
        QTRY_VERIFY(m_dio->isIdle());
        QTRY_COMPARE(m_sim->dioDirection(), uint8_t(0x00));
        QCOMPARE(m_dio->drivenMask(), 0);
        QVERIFY(!m_dio->holdsOutputs());
    }
    void stopWhileIdleReleasesLines()
    {
        m_dio->runOnce();
        QTRY_VERIFY(m_dio->isIdle() && m_dio->drivenMask() == 0x0F);
        m_dio->stop();
        QTRY_COMPARE(m_sim->dioDirection(), uint8_t(0x00));
    }
    void sessionResetReturnsLinesToInputs()
    {
        m_dio->runOnce();
        QTRY_VERIFY(m_dio->isIdle() && m_dio->drivenMask() == 0x0F);
        m_dio->resetForSession();
        QTRY_COMPARE(m_sim->dioDirection(), uint8_t(0x00));
    }

    // Audit dio #2: Single pressed while running ends the continuous loop.
    void singleWhileRunning()
    {
        m_dio->run();
        QTRY_VERIFY(m_dio->isRunning() && m_dio->hasReading());
        m_dio->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->isIdle(), 3000);
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));   // outputs keep driving
    }

    // Audit dio #6/#7: Numeric Value is `0` before any sample and never zero padded.
    void numericValueText()
    {
        QCOMPARE(m_dio->numericValueText(false), u"0"_s);
        QCOMPARE(m_dio->numericValueText(true), u"0"_s);
        m_dio->setOutputRadix(DioController::Binary);
        m_dio->run();
        m_dio->toggleLine(0);
        QTRY_COMPARE(m_dio->writtenValue(), 1);
        QCOMPARE(m_dio->numericValueText(true), u"1"_s);   // not 0001
        m_dio->toggleLine(2);
        QTRY_COMPARE(m_dio->writtenValue(), 5);
        QCOMPARE(m_dio->numericValueText(true), u"101"_s);
    }

    // Audit dio #3: a typed Pattern Number above the maximum is rejected, not clamped.
    void patternEntryRejectsTooLarge()
    {
        QCOMPARE(m_dio->parseEntry(u"F"_s, DioController::Hex), 15);
        QCOMPARE(m_dio->parseEntry(u"0x7"_s, DioController::Hex), 7);
        QCOMPARE(m_dio->parseEntry(u"10"_s, DioController::Hex), -1);
        QCOMPARE(m_dio->parseEntry(u"16"_s, DioController::Decimal), -1);
        QCOMPARE(m_dio->parseEntry(u"1010"_s, DioController::Binary), 10);
        QCOMPARE(m_dio->parseEntry(u"abc"_s, DioController::Decimal), -1);
        QCOMPARE(m_dio->parseEntry({}, DioController::Decimal), -1);
    }

    // Audit dio #8: a device reset / unplugging ends the "still driven" state.
    void disconnectClearsDrivenLines()
    {
        m_dio->run();
        QTRY_COMPARE(m_dio->drivenMask(), 0x0F);
        m_sim->setConnected(false);
        QTRY_COMPARE(m_dio->drivenMask(), 0);
        QVERIFY(!m_dio->holdsOutputs());
        QTRY_VERIFY(m_dio->isIdle());
        m_sim->setConnected(true);
        QTRY_VERIFY(mybench::DeviceHub::instance()->isConnected());
    }

    // Audit dio #4: the timeline cursors show the line values in a chosen radix.
    void cursorValues()
    {
        m_dio->setStepInterval(20);
        m_dio->run();
        m_dio->toggleLine(0);
        m_dio->toggleLine(2);
        QTRY_VERIFY(m_dio->timelineEnd() > 0.2 && m_dio->writtenValue() == 5);
        const double t = m_dio->timelineEnd();
        QVERIFY2(m_dio->valuesAtTime(t, DioController::Hex).contains(u"Out x 5"_s),
                 qPrintable(m_dio->valuesAtTime(t, DioController::Hex)));
        QVERIFY(m_dio->valuesAtTime(t, DioController::Binary).contains(u"Out b 101"_s));
        QVERIFY(m_dio->valuesAtTime(t, DioController::Decimal).contains(u"In d "_s));
    }

    void directionChangeWhileIdleReleases()
    {
        m_dio->runOnce();
        QTRY_VERIFY(m_dio->isIdle() && m_dio->drivenMask() == 0x0F);
        m_dio->setLineDirection(0, DioController::In);
        QTRY_COMPARE(m_sim->dioDirection(), uint8_t(0x0E));
    }

    void liveDirectionChange()
    {
        m_dio->run();
        QTRY_COMPARE(m_dio->drivenMask(), 0x0F);
        m_dio->setWritePreset(int(mybench::instruments::dio::Lines0to7));
        QTRY_COMPARE(m_dio->drivenMask(), 0xFF);
        QTRY_VERIFY(m_dio->isRunning());
        QCOMPARE(m_sim->dioDirection(), uint8_t(0xFF));
        m_dio->setWritePreset(int(mybench::instruments::dio::Lines0to3));
        QTRY_COMPARE(m_sim->dioDirection(), uint8_t(0x0F));
    }

    void otherInstrumentsLines()
    {
        auto* counter = mybench::InstrumentRegistry::instance()->controller(u"fakectr"_s);
        QVERIFY(counter);
        QTRY_COMPARE(m_dio->foreignMask(), 0x02);
        QCOMPARE(m_dio->conflictMask(), 0);
        QVERIFY(m_dio->lineOwners().value(1).toMap().value(u"owner"_s).toString() == u"CTR"_s);
        counter->run();
        QTRY_VERIFY(counter->isRunning());
        QTRY_COMPARE(m_dio->conflictMask(), 0x02);
        // Running Digital I/O on DIO 1 pauses the counter (latest user action wins).
        m_dio->run();
        QTRY_VERIFY(counter->isPaused());
        QTRY_VERIFY(m_dio->isRunning());
        // And the counter taking its line back pauses Digital I/O, which frees its outputs.
        m_dio->setOutputMask(0x03);   // DIO 0-1 driven (DIO 1 overlaps the counter)
        QTRY_COMPARE(m_dio->drivenMask(), 0x03);
        counter->resume();
        QTRY_VERIFY(m_dio->isPaused());
        QTRY_COMPARE(m_sim->dioDirection() & 0x03, 0x00);
        counter->stop();
        QTRY_VERIFY(counter->isIdle());
    }

    void exportTable()
    {
        m_dio->setSampleInterval(10);
        m_dio->run();
        QTRY_VERIFY(m_dio->acquisitionCount() > 5);
        m_dio->stop();
        QTRY_VERIFY(m_dio->isIdle());
        const QVariantMap metadata = m_dio->exportMetadata();
        QVERIFY(metadata.value(u"Directions"_s).toString().startsWith(u"DIO 0 Out, DIO 1 Out"_s));
        QCOMPARE(metadata.value(u"Sample interval"_s).toString(), u"10 ms"_s);
    }
};

QTEST_MAIN(TestDioController)
#include "tst_diocontroller.moc"
