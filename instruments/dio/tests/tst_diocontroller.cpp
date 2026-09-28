// DioController against the simulator (docs/instruments/dio.md section 11): directions,
// manual pattern and actions, sequences, Single, presets moving lines, custom sets, outputs kept
// after Stop and released on pause / on request, lines of other instruments.
#include "diocontroller.h"

#include <mybench/devicehub.h>
#include <mybench/instrumentregistry.h>
#include <mybench/resourcearbiter.h>

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QTest>

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

    void rampSteps()
    {
        m_dio->setStepInterval(20);
        m_dio->setPattern(DioController::Ramp);
        m_dio->run();
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->writtenValue() >= 5, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(m_dio->writtenValue() == 0 && m_dio->sequenceStep() == 0, 2000);   // wraps at 15
    }

    void singleAlternating()
    {
        m_dio->setStepInterval(20);
        m_dio->setPattern(DioController::Alternating);
        QList<int> values;
        connect(m_dio.get(), &DioController::writtenChanged, this, [&] { values << m_dio->writtenValue(); });
        m_dio->runOnce();
        QTRY_VERIFY(m_dio->isIdle() && !values.isEmpty());
        QCOMPARE(values, (QList<int>{0x5, 0xA}));
        QCOMPARE(m_sim->dioDirection(), uint8_t(0x0F));   // Single done: outputs keep driving
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
