// BodeSweeper against the simulated myDAQ: AO 0 -> AI 0 directly and AO 0 -> RC low-pass
// (1 kHz) -> AI 1, so every point must match H = 1 / (1 + j f / 1 kHz).
#include "bodemath.h"
#include "bodesweep.h"

#include <mydaq/sim_device.h>

#include <QElapsedTimer>
#include <QTest>

#include <cmath>
#include <complex>
#include <numbers>

using namespace mybench::instruments::bode;

namespace {

PointValues expected(double f, double cutoff = 1000.0)
{
    return pointValues(1.0 / std::complex<double>(1.0, f / cutoff), false);
}

}  // namespace

class TestBodeSweep : public QObject
{
    Q_OBJECT

private slots:
    void fastSweepMatchesRc_data()
    {
        QTest::addColumn<double>("frequency");
        QTest::newRow("100 Hz") << 100.0;
        QTest::newRow("1 kHz") << 1000.0;
        QTest::newRow("6.3 kHz") << 6309.573;
        QTest::newRow("10 kHz") << 10000.0;
        QTest::newRow("20 kHz") << 20000.0;
    }
    void fastSweepMatchesRc()
    {
        QFETCH(double, frequency);
        mydaq::SimDevice sim(QStringLiteral("sim:test"));
        BodeSweeper sweeper(sim, [] { return false; });
        SweepOptions options;
        options.rangeProbe = false;
        const auto point = sweeper.measure(frequency, options);
        QVERIFY(point);
        QVERIFY(std::abs(point->frequency / frequency - 1) < 1e-5);
        const PointValues v = pointValues(point->response, false);
        const PointValues e = expected(point->frequency);
        QVERIFY2(std::abs(v.gainDb - e.gainDb) < 0.05, qPrintable(QStringLiteral("%1 dB, expected %2").arg(v.gainDb).arg(e.gainDb)));
        QVERIFY2(std::abs(v.phase - e.phase) < 0.5, qPrintable(QStringLiteral("%1 deg, expected %2").arg(v.phase).arg(e.phase)));
        QVERIFY(!point->clipped);
        QCOMPARE(point->stimulusRange, 10.0);   // 2 V peak does not fit ±2 V with the 1.8 V rule
        sweeper.finish();
    }

    void skewIsCorrected()
    {
        mydaq::SimDevice sim(QStringLiteral("sim:test"));
        sim.setChannelSkew(5e-6);   // 18 deg at 10 kHz if uncorrected
        BodeSweeper sweeper(sim, [] { return false; });
        SweepOptions options;
        options.rangeProbe = false;
        const auto point = sweeper.measure(10000, options);
        QVERIFY(point);
        const PointValues v = pointValues(point->response, false);
        QVERIFY2(std::abs(v.phase - expected(10000).phase) < 0.5, qPrintable(QString::number(v.phase)));
    }

    void standardModeProbesRanges()
    {
        // ELVISmx mode: 1.6 s pre-measurement; 0.5 V peak fits ±2 V on both channels at 100 Hz.
        mydaq::SimDevice sim(QStringLiteral("sim:test"));
        BodeSweeper sweeper(sim, [] { return false; });
        SweepOptions options;
        options.amplitude = 0.5;
        QElapsedTimer clock;
        clock.start();
        const auto point = sweeper.measure(100, options);
        QVERIFY(point);
        QVERIFY(clock.elapsed() >= 1500);
        QCOMPARE(point->stimulusRange, 2.0);
        QCOMPARE(point->responseRange, 2.0);
        QCOMPARE(point->sampleRate, 200000.0);   // 3200 x 100 Hz clamped to the maximum
        const PointValues v = pointValues(point->response, false);
        QVERIFY(std::abs(v.gainDb - expected(100).gainDb) < 0.05);
        QVERIFY(std::abs(point->stimulusAmplitude - 0.5) < 0.01);
    }

    void clippedStimulusIsRemeasured()
    {
        // Fast mode, 1.5 V: the stimulus starts on ±2 V (1.5 <= 1.8) and is not clipped; 1.8 V + noise
        // could reach 98 % of 2 V, so a 1.99 V request must end on ±10 V without error.
        mydaq::SimDevice sim(QStringLiteral("sim:test"));
        BodeSweeper sweeper(sim, [] { return false; });
        SweepOptions options;
        options.rangeProbe = false;
        options.amplitude = 1.5;
        auto point = sweeper.measure(1000, options);
        QVERIFY(point);
        QCOMPARE(point->stimulusRange, 2.0);
        options.amplitude = 1.99;
        options.rangeProbe = true;   // probe: 2.2 x 1.99 > 2 -> ±10 V
        point = sweeper.measure(1000, options);
        QVERIFY(point);
        QCOMPARE(point->stimulusRange, 10.0);
    }

    void noStimulus()
    {
        mydaq::SimDevice sim(QStringLiteral("sim:test"));
        BodeSweeper sweeper(sim, [] { return false; });
        SweepOptions options;
        options.rangeProbe = false;
        options.amplitude = 0.0;
        try {
            sweeper.measure(1000, options);
            QFAIL("no error");
        } catch (const mydaq::DeviceError& error) {
            QCOMPARE(error.message(), BodeSweeper::noStimulusMessage());
        }
    }

    void stopIsQuick()
    {
        // A 1 Hz point takes seconds; a stop request must end it within ~200 ms.
        mydaq::SimDevice sim(QStringLiteral("sim:test"));
        QElapsedTimer clock;
        clock.start();
        BodeSweeper sweeper(sim, [&clock] { return clock.elapsed() > 300; });
        SweepOptions options;
        options.rangeProbe = false;
        const auto point = sweeper.measure(1.0, options);
        QVERIFY(!point);
        QVERIFY2(clock.elapsed() < 600, qPrintable(QString::number(clock.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestBodeSweep)
#include "tst_bodesweep.moc"
