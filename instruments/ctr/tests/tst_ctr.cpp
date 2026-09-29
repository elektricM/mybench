// Counter/Timer: timing rules and terminals against the simulator's implementation of the device
// contract, statistics helpers, and the controller running measurements, pulse trains and
// FREQOUT on the simulated myDAQ.
#include "countermath.h"
#include "ctrcontroller.h"

#include <mybench/devicehub.h>
#include <mybench/resourcearbiter.h>

#include <mydaq/sim_device.h>

#include <QStandardPaths>
#include <QtTest>

#include <cmath>
#include <numeric>

using namespace mybench;
using namespace mybench::instruments;
using namespace Qt::StringLiterals;

class TstCtr : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<mydaq::SimDevice> sim() { return DeviceHub::instance()->simulator(); }
    void resetSim()
    {
        sim()->reset();
        for (int line = 0; line < mydaq::kDioLines; ++line)
            sim()->setDigitalSource(line, {});
        sim()->setEncoder({});
    }
    static mydaq::SimDigitalSource clock(double hz)
    {
        mydaq::SimDigitalSource s;
        s.kind = mydaq::SimDigitalSource::Kind::Clock;
        s.frequency = hz;
        return s;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        InstrumentController::setRestoreSavedSettings(false);
        QVERIFY(DeviceHub::instance()->simulator());
        DeviceHub::instance()->select(u"sim:0"_s);
        QVERIFY(DeviceHub::instance()->isConnected());
    }

    void freqOutTable()
    {
        const auto& table = ctr::freqOutTable();
        QCOMPARE(table.front().frequency, 20e6);
        QCOMPARE(table.back().frequency, 6250.0);
        for (size_t i = 1; i < table.size(); ++i)
            QVERIFY(table[i].frequency < table[i - 1].frequency);
        // 10 MHz / n duplicates 20 MHz / 2n: 16 + 8 + 16 distinct values.
        QCOMPARE(int(table.size()), 40);
        QCOMPARE(ctr::coerceFreqOut(1e6).frequency, 1e6);
        QCOMPARE(ctr::coerceFreqOut(1e6).timebase, 10e6);
        QCOMPARE(ctr::coerceFreqOut(1.0).frequency, 6250.0);
        for (double f = 1000; f < 3e7; f *= 1.37)
            QCOMPARE(ctr::coerceFreqOut(f).frequency, sim()->coerceFrequencyOutput(f));
    }

    void pulseCoercion()
    {
        for (double f = 0.05; f < 3e7; f *= 1.91)
            QCOMPARE(ctr::coercePulseFrequency(f), sim()->coerceCounterFrequency(f));
        QCOMPARE(ctr::coercePulseFrequency(1e9), 25e6);
        QCOMPARE(ctr::coercePulseDuty(25e6, 0.1), 0.5);    // 2 of 4 ticks high at least
        QCOMPARE(ctr::coercePulseDuty(1000, 0.25), 0.25);
    }

    void timebaseRule()
    {
        using M = mydaq::CounterMeasurement;
        QCOMPARE(ctr::inputTimebase(M::Frequency, 1, 1e6), 100e6);
        QCOMPARE(ctr::inputTimebase(M::Frequency, 0.01, 1e6), 20e6);    // 100 s period
        QCOMPARE(ctr::inputTimebase(M::Frequency, 0.001, 1e6), 100e3);  // 1000 s period
        QCOMPARE(ctr::inputTimebase(M::Period, 1e-6, 10), 100e6);
        QCOMPARE(ctr::inputTimebase(M::PulseWidth, 1e-6, 100), 20e6);
        QCOMPARE(ctr::inputTimebase(M::CountEdges, 1, 2), 100e6);
    }

    void terminalsMatchTheDevice_data()
    {
        QTest::addColumn<int>("measurement");
        QTest::addColumn<int>("direction");
        QTest::addColumn<bool>("option");   // resetOnDio1 / zIndexEnabled
        QTest::addColumn<int>("expected");
        using D = mydaq::CounterInputConfig::Direction;
        QTest::newRow("count up") << 0 << int(D::Up) << false << 0x01;
        QTest::newRow("count up reset") << 0 << int(D::Up) << true << 0x03;
        QTest::newRow("count external") << 0 << int(D::External) << false << 0x05;
        QTest::newRow("count external reset") << 0 << int(D::External) << true << 0x07;
        QTest::newRow("frequency") << 1 << 0 << false << 0x02;
        QTest::newRow("period") << 2 << 0 << false << 0x02;
        QTest::newRow("pulse width") << 3 << 0 << false << 0x02;
        QTest::newRow("semi-period") << 4 << 0 << false << 0x02;
        QTest::newRow("two-edge") << 5 << 0 << false << 0x06;
        QTest::newRow("linear") << 6 << 0 << false << 0x05;
        QTest::newRow("angular z") << 7 << 0 << true << 0x07;
    }
    void terminalsMatchTheDevice()
    {
        QFETCH(int, measurement);
        QFETCH(int, direction);
        QFETCH(bool, option);
        QFETCH(int, expected);
        resetSim();
        mydaq::CounterInputConfig config;
        config.measurement = mydaq::CounterMeasurement(measurement);
        config.direction = mydaq::CounterInputConfig::Direction(direction);
        config.resetOnDio1 = option;
        config.zIndexEnabled = option;
        QCOMPARE(int(ctr::inputLines(config)), expected);
        auto task = sim()->createCounterInputTask(config);
        QCOMPARE(int(task->dioLinesUsed()), expected);
    }

    void statistics()
    {
        ctr::RunningStats stats;
        QVERIFY(std::isnan(stats.mean()));
        const std::vector<double> values{2, 4, 4, 4, 5, 5, 7, 9};
        for (double v : values)
            stats.add(v);
        stats.add(qQNaN());   // ignored
        QCOMPARE(stats.count(), qint64(8));
        QCOMPARE(stats.mean(), 5.0);
        QCOMPARE(stats.minimum(), 2.0);
        QCOMPARE(stats.maximum(), 9.0);
        QVERIFY(std::abs(stats.standardDeviation() - std::sqrt(32.0 / 7.0)) < 1e-12);

        ctr::CountUnwrapper unwrap(0);
        QCOMPARE(unwrap.add(3), 3.0);
        QCOMPARE(unwrap.add(4294967295.0), -1.0);   // counted down past zero
        QCOMPARE(unwrap.add(4294967290.0), -6.0);
        QCOMPARE(unwrap.add(10), 10.0);             // and up again across the wrap

        ctr::RateEstimator rate(0.5);
        QVERIFY(std::isnan(rate.add(0.0, 0)));
        QVERIFY(std::isnan(rate.add(0.2, 20)));
        QCOMPARE(rate.add(1.0, 100), 100.0);
    }

    void measuresFrequency()
    {
        resetSim();
        sim()->setDigitalSource(1, clock(1000));
        CtrController ctl;
        ctl.initialize();
        QCOMPARE(ctl.counterFunction(), CtrController::Measure);
        QCOMPARE(ctl.measurement(), CtrController::Frequency);
        QCOMPARE(ctl.dioLines(), 0x02);
        QVERIFY(ctl.resources().testFlag(ResourceArbiter::Counter));
        QVERIFY(!ctl.resources().testFlag(ResourceArbiter::FrequencyOutput));
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.statCount() >= 3, 5000);
        QVERIFY(std::abs(ctl.value() - 1000.0) < 0.01);
        QVERIFY(ctl.valueText().contains(u"Hz"_s));
        QVERIFY(ctl.secondaryText().contains(u"ms"_s));
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
        QTemporaryDir dir;
        const QString path = dir.filePath(u"ctr.csv"_s);
        QVERIFY(ctl.exportData(QUrl::fromLocalFile(path)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(u"Time (s),Frequency (Hz)"_s));
        QVERIFY(text.contains(u"# Terminals: DIO 1"_s));
    }

    void measuresFreqOutLoopback()
    {
        // Self test: FREQOUT (DIO 4) jumpered to DIO 1, measured by ctr0 in the same run.
        resetSim();
        mydaq::SimDigitalSource jumper;
        jumper.kind = mydaq::SimDigitalSource::Kind::FrequencyOutput;
        sim()->setDigitalSource(1, jumper);
        CtrController ctl;
        ctl.initialize();
        ctl.setFreqOutEnabled(true);
        ctl.setFreqOutFrequency(12345);   // coerced to 12.5 kHz (100 kHz / 8)
        QCOMPARE(ctl.coercedFreqOut(), 12500.0);
        QCOMPARE(ctl.dioLines(), 0x12);
        QVERIFY(ctl.resources().testFlag(ResourceArbiter::FrequencyOutput));
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.freqOutActual() == 12500.0 && std::abs(ctl.value() - 12500.0) < 0.5, 5000);
        ctl.setFreqOutFrequency(20e6 / 16);   // live
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(ctl.value() - 1.25e6) < 100, 5000);
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
        QVERIFY(!sim()->frequencyOutputRunning());
        QCOMPARE(ctl.freqOutActual(), 0.0);
    }

    void bufferedSingle()
    {
        resetSim();
        sim()->setDigitalSource(1, clock(1000));
        CtrController ctl;
        ctl.initialize();
        ctl.setMeasurement(CtrController::Period);
        ctl.setSampling(CtrController::Buffered);
        ctl.setBufferedSamples(50);
        ctl.runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.acquisitionCount() > 0 && ctl.isIdle(), 5000);
        QVERIFY(ctl.statCount() >= 50);
        QVERIFY(std::abs(ctl.statMean() - 1e-3) < 1e-7);
        QVERIFY(ctl.statDeviation() < 1e-7);
    }

    void countsDownThroughZero()
    {
        resetSim();
        sim()->setDigitalSource(0, clock(200));
        mydaq::SimDigitalSource low;
        low.kind = mydaq::SimDigitalSource::Kind::Level;
        sim()->setDigitalSource(2, low);   // external direction low = count down
        CtrController ctl;
        ctl.initialize();
        ctl.setMeasurement(CtrController::CountEdges);
        ctl.setCountDirection(CtrController::CountExternal);
        QCOMPARE(ctl.dioLines(), 0x05);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.value() < -20, 5000);   // not 4294967xxx
        QVERIFY(ctl.valueText().startsWith(u'-'));
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
    }

    void encoderAngle()
    {
        resetSim();
        mydaq::SimEncoder encoder;
        encoder.enabled = true;
        encoder.speed = 1024;   // one revolution per second
        sim()->setEncoder(encoder);
        CtrController ctl;
        ctl.initialize();
        ctl.setMeasurement(CtrController::AngularEncoder);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.statCount() >= 10, 5000);
        QVERIFY(ctl.secondaryText().contains(u"rpm"_s));
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
    }

    void pulseTrainLive()
    {
        resetSim();
        CtrController ctl;
        ctl.initialize();
        ctl.setCounterFunction(CtrController::PulseTrain);
        QCOMPARE(ctl.dioLines(), 0x08);
        ctl.run();
        QTRY_COMPARE_WITH_TIMEOUT(ctl.outputFrequency(), 1000.0, 3000);
        ctl.setPulseFrequency(2000);
        ctl.setDutyCycle(0.25);
        QTRY_COMPARE_WITH_TIMEOUT(ctl.outputFrequency(), 2000.0, 3000);
        QCOMPARE(ctl.outputDutyCycle(), 0.25);
        QVERIFY(ctl.isRunning());
        ctl.setHighLowTimes(1e-3, 3e-3);
        QCOMPARE(ctl.pulseFrequency(), 250.0);
        QCOMPARE(ctl.dutyCycle(), 0.25);
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
    }

    void finitePulseTrainEnds()
    {
        resetSim();
        CtrController ctl;
        ctl.initialize();
        ctl.setCounterFunction(CtrController::PulseTrain);
        ctl.setPulseGeneration(CtrController::FinitePulses);
        ctl.setPulseCount(20);
        ctl.setPulseFrequency(1000);
        ctl.run();
        // 20 ms of pulses, then the run ends. Not waiting for isRunning() first: on Windows (15.6 ms
        // timer granularity) the whole run can be over before the first poll sees it running.
        QTRY_COMPARE_WITH_TIMEOUT(ctl.outputState(), u"Done"_s, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 5000);
    }

    void unsupportedOptionsAreRefusedUpFront()
    {
        // A limited backend (like the USB one today): the panel greys these; Run says why.
        resetSim();
        DeviceHub::instance()->setSimulatorUnsupportedFeatures(
            {u"counter.pulseWidth"_s, u"counter.fallingEdge"_s, u"pulse.idleHigh"_s});
        CtrController ctl;
        ctl.initialize();
        QVERIFY(ctl.unsupportedInUse().isEmpty());
        QVERIFY(ctl.measurementSupported(CtrController::Frequency));
        QVERIFY(!ctl.measurementSupported(CtrController::PulseWidth));
        QVERIFY(ctl.measurementSupported(CtrController::SemiPeriod));
        ctl.setMeasurement(CtrController::PulseWidth);
        QCOMPARE(ctl.unsupportedInUse(), QStringList{u"Pulse Width"_s});
        ctl.run();
        QVERIFY(ctl.isIdle());
        QVERIFY(!ctl.hasError());
        QVERIFY(ctl.inputWarning().contains(u"Pulse Width"_s));
        ctl.setMeasurement(CtrController::Frequency);
        ctl.setActiveEdge(CtrController::Falling);
        QCOMPARE(ctl.unsupportedInUse(), QStringList{u"Falling edge"_s});
        ctl.setActiveEdge(CtrController::Rising);
        ctl.setCounterFunction(CtrController::PulseTrain);
        ctl.setIdleHigh(true);
        QCOMPARE(ctl.unsupportedInUse(), QStringList{u"Idle state High"_s});
        DeviceHub::instance()->setSimulatorUnsupportedFeatures({});
        QVERIFY(ctl.unsupportedInUse().isEmpty());
    }

    void trendStartsEmptyOnANewRun()
    {
        resetSim();
        sim()->setDigitalSource(1, clock(1000));
        CtrController ctl;
        ctl.initialize();
        ctl.setUpdateInterval(0.01);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.trend()->count() >= 100, 10000);
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
        const qsizetype before = ctl.trend()->count();
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isRunning(), 2000);
        QVERIFY2(ctl.trend()->count() < before, qPrintable(QString::number(ctl.trend()->count())));
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
    }

    void finitePulsesKeepFreqOutRunning()
    {
        resetSim();
        CtrController ctl;
        ctl.initialize();
        ctl.setCounterFunction(CtrController::PulseTrain);
        ctl.setPulseGeneration(CtrController::FinitePulses);
        ctl.setPulseCount(20);
        ctl.setPulseFrequency(1000);
        ctl.setFreqOutEnabled(true);
        ctl.run();
        QTRY_COMPARE_WITH_TIMEOUT(ctl.outputState(), u"Done"_s, 3000);
        QTest::qWait(200);
        QVERIFY(ctl.isRunning());
        QVERIFY(sim()->frequencyOutputRunning());
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
        QVERIFY(!sim()->frequencyOutputRunning());
    }

    void nothingToRun()
    {
        CtrController ctl;
        ctl.initialize();
        ctl.setCounterFunction(CtrController::CounterOff);
        QCOMPARE(ctl.dioLines(), 0);
        QVERIFY(!ctl.resources().testFlag(ResourceArbiter::Counter));
        ctl.run();
        QVERIFY(ctl.isIdle());
        QVERIFY(ctl.hasError());
    }
};

QTEST_MAIN(TstCtr)
#include "tst_ctr.moc"
