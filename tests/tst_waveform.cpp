#include "mydaq/dsp/waveform.h"

#include "mydaq/dsp/measure.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace mydaq::dsp;

namespace {

// myDAQ-like sample clock: 100 MHz / integer divisor.
double coerce(double requested)
{
    const double divisor = std::max(500.0, std::round(100e6 / requested));
    return 100e6 / divisor;
}

}  // namespace

class TestWaveform : public QObject {
    Q_OBJECT

private slots:
    void shapes()
    {
        QVERIFY(std::abs(waveformShape(Waveform::Sine, 0.25) - 1.0) < 1e-15);
        QVERIFY(std::abs(waveformShape(Waveform::Sine, 7.0)) < 1e-12);
        QCOMPARE(waveformShape(Waveform::Square, 0.1, 0.2), 1.0);
        QCOMPARE(waveformShape(Waveform::Square, 0.3, 0.2), -1.0);
        QCOMPARE(waveformShape(Waveform::Square, -0.9, 0.2), 1.0);   // negative phases wrap
        QCOMPARE(waveformShape(Waveform::Triangle, 0.0), 0.0);
        QCOMPARE(waveformShape(Waveform::Triangle, 0.25), 1.0);
        QCOMPARE(waveformShape(Waveform::Triangle, 0.5), 0.0);
        QCOMPARE(waveformShape(Waveform::Triangle, 0.75), -1.0);
        QCOMPARE(waveformShape(Waveform::Sawtooth, 0.25), 0.5);
        QCOMPARE(waveformShape(Waveform::Sawtooth, 0.75), -0.5);
        QCOMPARE(waveformShape(Waveform::Dc, 0.3), 0.0);
    }

    void generateSine()
    {
        const WaveformParams p{Waveform::Sine, 125.0, 0.75, 0.25, 1.1, 0.5};
        const std::vector<double> x = generate(p, 10000.0, 4000);
        const Tone t = fitSine(x, 10000.0, 125.0);
        QVERIFY(std::abs(t.amplitude - 0.75) < 1e-12);
        QVERIFY(std::abs(t.phase - 1.1) < 1e-12);
        QVERIFY(std::abs(t.offset - 0.25) < 1e-12);
        // startTime continues the same waveform.
        const std::vector<double> tail = generate(p, 10000.0, 10, 0.1);
        QVERIFY(std::abs(tail[0] - generate(p, 10000.0, 1001)[1000]) < 1e-12);
    }

    void generateSquareAndDc()
    {
        const std::vector<double> sq = generate({Waveform::Square, 100.0, 1.0, 0.5, 0.0, 0.25}, 10000.0, 10000);
        QVERIFY(std::abs(dutyCycle(sq) - 0.25) < 0.005);
        QCOMPARE(maximum(sq), 1.5);
        QCOMPARE(minimum(sq), -0.5);
        const std::vector<double> dc = generate({Waveform::Dc, 100.0, 1.0, -0.7, 0.0, 0.5}, 1000.0, 10);
        QCOMPARE(dc, std::vector<double>(10, -0.7));
    }

    void noiseIsReproducibleAndBounded()
    {
        const WaveformParams p{Waveform::Noise, 0.0, 0.5, 1.0, 0.0, 0.5};
        const std::vector<double> a = generate(p, 1000.0, 5000, 0.0, 42);
        QCOMPARE(generate(p, 1000.0, 5000, 0.0, 42), a);
        QVERIFY(generate(p, 1000.0, 5000, 0.0, 43) != a);
        QVERIFY(minimum(a) >= 0.5 && maximum(a) <= 1.5);
        QVERIFY(std::abs(mean(a) - 1.0) < 0.02);
    }

    void planHitsFrequency_data()
    {
        QTest::addColumn<double>("frequency");
        for (double f : {0.1, 0.123, 1.0, 10.0, 100.0, 1000.0, 1234.567, 12345.678, 20000.0 / 3.0, 49999.9, 99999.9})
            QTest::newRow(qPrintable(QString::number(f))) << f;
    }
    void planHitsFrequency()
    {
        QFETCH(double, frequency);
        const BufferPlan plan = planPeriodicBuffer(frequency, 200e3, coerce);
        QVERIFY2(plan.relativeError < 1e-5, qPrintable(QString::number(plan.relativeError)));
        QVERIFY(plan.length >= 2 && plan.length <= 100000);
        QVERIFY(plan.sampleRate <= 200e3 && plan.sampleRate == coerce(plan.sampleRate));
        QVERIFY(plan.cycles >= 1);
        QCOMPARE(plan.frequency, double(plan.cycles) * plan.sampleRate / double(plan.length));
        QVERIFY(std::abs(plan.frequency - frequency) / frequency <= plan.relativeError * (1 + 1e-9));
        // A rate near the maximum is kept whenever a whole period fits.
        if (frequency * 100000 >= 200e3 * 1.05)
            QVERIFY(plan.sampleRate >= 0.96 * 200e3);
    }

    void planPrefersShortBuffers()
    {
        const BufferPlan plan = planPeriodicBuffer(1000.0, 200e3, coerce);
        QCOMPARE(plan.sampleRate, 200e3);
        QCOMPARE(plan.length, size_t(200));
        QCOMPARE(plan.cycles, size_t(1));
        QCOMPARE(plan.relativeError, 0.0);
        const BufferPlan longer = planPeriodicBuffer(1000.0, 200e3, coerce, 100000, 1000);
        QCOMPARE(longer.length, size_t(1000));
        QCOMPARE(longer.cycles, size_t(5));
        const BufferPlan dc = planPeriodicBuffer(0.0, 200e3, coerce, 100000, 64);
        QCOMPARE(dc.cycles, size_t(0));
        QCOMPARE(dc.length, size_t(64));
        QCOMPARE(dc.sampleRate, 200e3);
    }

    // The buffer loops seamlessly: sample L would equal sample 0.
    void periodicBufferIsSeamless()
    {
        const WaveformParams p{Waveform::Sine, 1234.567, 1.0, 0.0, 0.3, 0.5};
        const BufferPlan plan = planPeriodicBuffer(p.frequency, 200e3, coerce);
        const std::vector<double> b = generatePeriodic(p, plan);
        QCOMPARE(b.size(), plan.length);
        QVERIFY(std::abs(b[0] - std::sin(0.3)) < 1e-12);
        std::vector<double> twice = b;
        twice.insert(twice.end(), b.begin(), b.end());
        const Tone t = extractSingleTone(twice, plan.sampleRate);
        QVERIFY(std::abs(t.frequency - plan.frequency) / plan.frequency < 1e-7);
        QVERIFY(std::abs(t.amplitude - 1.0) < 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestWaveform)
#include "tst_waveform.moc"
