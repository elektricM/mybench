// Bode Analyzer pure functions: frequency grid, rates, ranges, phase handling, markers.
#include "bodemath.h"

#include <QTest>

#include <cmath>
#include <complex>
#include <numbers>

using namespace mybench::instruments::bode;

class TestBodeMath : public QObject
{
    Q_OBJECT

private slots:
    void defaultGrid()
    {
        // bode.md 3.1: 100 Hz ... 10 kHz, 5 per decade -> 11 points.
        const std::vector<double> f = sweepFrequencies(100, 10000, 5);
        QCOMPARE(f.size(), size_t(11));
        const double expected[] = {100, 158.4893, 251.1886, 398.1072, 630.9573, 1000, 1584.893, 2511.886, 3981.072,
                                   6309.573, 10000};
        for (size_t i = 0; i < f.size(); ++i)
            QVERIFY2(std::abs(f[i] / expected[i] - 1) < 1e-6, qPrintable(QString::number(f[i])));
        QCOMPARE(f.front(), 100.0);
    }

    void gridEnds()
    {
        // Stop not on the grid: the sweep ends at the last point below it.
        const std::vector<double> f = sweepFrequencies(100, 900, 1);
        QCOMPARE(f.size(), size_t(1));
        QCOMPARE(sweepFrequencies(1, 20000, 1).size(), size_t(5));      // 1, 10, 100, 1k, 10k
        QCOMPARE(sweepFrequencies(1000, 1000, 5).size(), size_t(1));    // Start = Stop: one point
        QVERIFY(sweepFrequencies(2000, 1000, 5).empty());               // Start > Stop: none
        QCOMPARE(sweepFrequencies(1, 20000, 10).size(), size_t(44));    // 1 ... 10^4.3 = 19953 Hz
    }

    void rates()
    {
        QCOMPARE(acquisitionRate(1, 200000), 3200.0);         // 5 periods in 16 000 samples
        QCOMPARE(acquisitionRate(0.01, 200000), 100.0);       // lower clamp
        QCOMPARE(acquisitionRate(1000, 200000), 200000.0);    // saturates above 62.5 Hz
        QCOMPARE(stimulusRateLimit(1, 200000), 10000.0);      // 10 000 samples per period
        QCOMPARE(stimulusRateLimit(1000, 200000), 180000.0);   // never the AI clock
    }

    void ranges()
    {
        QCOMPARE(selectRange(0.5), 2.0);
        QCOMPARE(selectRange(0.909), 2.0);
        QCOMPARE(selectRange(0.92), 10.0);   // 2.2 x 0.92 > 2 V
        QCOMPARE(selectRange(3.0), 10.0);
        QVERIFY(isClipped({0.1, 1.97, -0.2}, 2.0));
        QVERIFY(!isClipped({0.1, 1.9, -1.9}, 2.0));
        QCOMPARE(peakMagnitude({0.1, -2.5, 1.0}), 2.5);
    }

    void phases()
    {
        QCOMPARE(wrapDegrees(190), -170.0);
        QCOMPARE(wrapDegrees(-180), 180.0);
        QCOMPARE(wrapDegrees(180), 180.0);
        QCOMPARE(wrapDegrees(-540), 180.0);
        const std::vector<double> u = unwrapDegrees({-170, 175, 160, -175});
        QCOMPARE(u[1], -185.0);
        QCOMPARE(u[2], -200.0);
        QCOMPARE(u[3], -175.0);
    }

    void responseAndPolarity()
    {
        // Response half the stimulus, 45 deg behind, plus the skew's extra phase which is removed.
        const double f = 1000, skew = 5e-6;
        const double extra = 2 * std::numbers::pi * f * skew;
        const std::complex<double> h = response(2.0, 0.3, 1.0, 0.3 - std::numbers::pi / 4 + extra, f, skew);
        const PointValues v = pointValues(h, false);
        QVERIFY(std::abs(v.gain - 0.5) < 1e-12);
        QVERIFY(std::abs(v.gainDb + 6.0206) < 1e-3);
        QVERIFY(std::abs(v.phase + 45.0) < 1e-9);
        const PointValues inv = pointValues(h, true);
        QVERIFY(std::abs(inv.phase - 135.0) < 1e-9);
        QVERIFY(std::abs(inv.gain - 0.5) < 1e-12);
        QVERIFY(std::isinf(pointValues({0, 0}, false).gainDb));
    }

    void lowPassMarkers()
    {
        // First-order low-pass at 1 kHz, 10 per decade: -3 dB at 1 kHz, no unity or -180 crossing.
        std::vector<double> f = sweepFrequencies(10, 100000, 10), g, p;
        for (double x : f) {
            const std::complex<double> h = 1.0 / std::complex<double>(1.0, x / 1000.0);
            g.push_back(20 * std::log10(std::abs(h)));
            p.push_back(std::arg(h) * 180 / std::numbers::pi);
        }
        const Markers m = computeMarkers(f, g, p);
        QCOMPARE(m.cutoff.size(), size_t(1));
        QVERIFY2(std::abs(m.cutoff[0] / 1000 - 1) < 0.02, qPrintable(QString::number(m.cutoff[0])));
        QVERIFY(std::isnan(m.unityGainFrequency));   // always below 0 dB
        QVERIFY(std::isnan(m.phaseCrossoverFrequency));
    }

    void loopMargins()
    {
        // Loop gain L = 100 / (s/w1 + 1)^3 with w1 = 100 Hz: unity crossover and -180 crossing both exist.
        std::vector<double> f = sweepFrequencies(1, 20000, 50), g, p;
        for (double x : f) {
            const std::complex<double> one(1.0, x / 100.0);
            const std::complex<double> l = 10.0 / (one * one * one);
            g.push_back(20 * std::log10(std::abs(l)));
            p.push_back(std::arg(l) * 180 / std::numbers::pi);   // wrapped
        }
        const Markers m = computeMarkers(f, g, p);
        // |L| = 1 where (1 + (f/100)^2)^1.5 = 10 -> f = 100 * sqrt(10^(2/3) - 1) = 190.8 Hz.
        QVERIFY2(std::abs(m.unityGainFrequency / 190.8 - 1) < 0.01, qPrintable(QString::number(m.unityGainFrequency)));
        // Phase there: -3 atan(1.908) = -187.05 deg -> phase margin -7.05 deg (unstable loop).
        QVERIFY2(std::abs(m.phaseMargin + 7.05) < 0.3, qPrintable(QString::number(m.phaseMargin)));
        // -180 deg at f = 100 tan(60 deg) = 173.2 Hz where |L| = 10/8: gain margin -1.94 dB.
        QVERIFY2(std::abs(m.phaseCrossoverFrequency / 173.2 - 1) < 0.01, qPrintable(QString::number(m.phaseCrossoverFrequency)));
        QVERIFY2(std::abs(m.gainMargin + 1.94) < 0.1, qPrintable(QString::number(m.gainMargin)));
    }

    void estimates()
    {
        QVERIFY(estimatedPointSeconds(1000, 200000, true, 1) > 1.6);
        QVERIFY(estimatedPointSeconds(1000, 200000, false, 1) < 0.2);
        QVERIFY(std::abs(estimatedPointSeconds(1, 200000, false, 1) - (3.0 + 5.0 + 0.04)) < 1e-9);
        QCOMPARE(settlingSeconds(10000), 0.02);
    }
};

QTEST_GUILESS_MAIN(TestBodeMath)
#include "tst_bodemath.moc"
