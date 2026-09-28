// White-box test of the simulator's digital signal model (exact edge times used by ctr0).
#include "sim/digital_timeline.h"

#include <QTest>

#include <cmath>

using mydaq::sim::DigitalChange;
using mydaq::sim::DigitalTimeline;

namespace {

// Transitions found by sampling levelAt() on a fine grid over (t0, t1].
struct Sampled {
    int rising = 0;
    int falling = 0;
    std::vector<double> times;   // grid time right after each transition
};

Sampled sample(const DigitalTimeline& line, double t0, double t1, double step)
{
    Sampled s;
    bool level = line.levelAt(t0);
    for (double t = t0 + step; t <= t1; t += step) {
        const bool now = line.levelAt(t);
        if (now != level) {
            (now ? s.rising : s.falling)++;
            s.times.push_back(t);
            level = now;
        }
    }
    return s;
}

// Every change from nextChange() matches the sampled transitions (within one grid step).
void checkAgainstSampling(const DigitalTimeline& line, double t0, double t1, double step)
{
    const Sampled s = sample(line, t0, t1, step);
    QCOMPARE(line.countEdges(t0, t1, true), int64_t(s.rising));
    QCOMPARE(line.countEdges(t0, t1, false), int64_t(s.falling));
    DigitalChange c = line.nextChange(t0);
    for (double expected : s.times) {
        QVERIFY2(c.time > expected - step - 1e-12 && c.time <= expected + 1e-12,
                 qPrintable(QStringLiteral("%1 vs %2").arg(c.time).arg(expected)));
        QCOMPARE(c.level, line.levelAt(expected));
        // previousChange() walks back to the same change.
        QVERIFY(std::abs(line.previousChange(c.time + 0.5 * step).time - c.time) < 1e-12);
        c = line.nextChange(c.time);
    }
    QVERIFY(c.time > t1 - 1e-9);
}

}  // namespace

class TestDigitalTimeline : public QObject {
    Q_OBJECT

private slots:
    void squareWave()
    {
        DigitalTimeline line;
        line.setSquare(0.0, 0.0, 100.0, 0.3);   // 100 Hz, 30 % high, rising at t = k / 100
        QVERIFY(line.levelAt(0.001));
        QVERIFY(!line.levelAt(0.004));
        const DigitalChange next = line.nextChange(0.0105);
        QVERIFY(std::abs(next.time - 0.013) < 1e-12 && !next.level);
        const DigitalChange rise = line.nextEdge(0.0105, true);
        QVERIFY(std::abs(rise.time - 0.02) < 1e-12 && rise.level);
        const DigitalChange back = line.previousEdge(0.0105, true);
        QVERIFY(std::abs(back.time - 0.01) < 1e-12);
        QCOMPARE(line.countEdges(0.0, 1.0, true), int64_t(100));   // rising edges at 0.01 .. 1.00
        QCOMPARE(line.countEdges(0.0, 1.0, false), int64_t(100));
        checkAgainstSampling(line, 0.0, 0.1005, 1e-5);   // no edge exactly at the window end
    }

    void reversingPhase()
    {
        DigitalTimeline line;
        line.setSquare(0.0, 5.0, -50.0, 0.5);   // phase decreasing: an encoder turning backwards
        checkAgainstSampling(line, 0.0, 0.2003, 1e-5);
        QCOMPARE(line.countEdges(0.0, 1.0, true), int64_t(50));
    }

    void segmentsAndBoundaries()
    {
        DigitalTimeline line;
        line.setConstant(0.0, false);
        line.setSquare(0.01, 0.0, 200.0, 0.5);   // starts high: a rising edge at the boundary
        line.setConstant(0.0303, true);          // held high
        line.setSquare(0.05, 0.75, 1000.0, 0.5); // enters low, then 1 kHz
        line.setConstant(0.06, false);
        checkAgainstSampling(line, 0.0, 0.07, 1e-6);
        const DigitalChange first = line.nextChange(0.0);
        QVERIFY(std::abs(first.time - 0.01) < 1e-12 && first.level);
        QVERIFY(line.previousChange(0.01).time < 0.0);   // nothing before the first edge
        // Rewriting from t discards what was scheduled after t.
        line.setConstant(0.02, false);
        QVERIFY(!line.levelAt(0.055));
        QCOMPARE(line.nextChange(0.0201).time, std::numeric_limits<double>::infinity());
    }

    void updateAtPeriodBoundary()
    {
        // A pulse train changed exactly at a period boundary: one rising edge there, not two.
        DigitalTimeline line;
        line.setSquare(0.0, 0.0, 1000.0, 0.25);
        line.setSquare(0.005, 0.0, 2000.0, 0.5);
        QCOMPARE(line.countEdges(0.0045, 0.0055, true), int64_t(1));
        checkAgainstSampling(line, 0.0, 0.0101, 1e-6);
    }

    void tinyDutyCycle()
    {
        DigitalTimeline line;
        line.setSquare(0.0, 0.0, 1.0, 2e-8);   // 20 ns pulses at 1 Hz
        QCOMPARE(line.countEdges(0.0, 3.5, true), int64_t(3));
        const DigitalChange fall = line.nextChange(1.0);
        QVERIFY(std::abs(fall.time - (1.0 + 2e-8)) < 1e-12 && !fall.level);
    }
};

QTEST_GUILESS_MAIN(TestDigitalTimeline)
#include "tst_digitaltimeline.moc"
