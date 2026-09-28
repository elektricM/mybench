#include "mydaq/dsp/trigger.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace mydaq::dsp;

namespace {

// sin(2*pi*n/period)
std::vector<double> sine(size_t n, double period)
{
    std::vector<double> x(n);
    for (size_t k = 0; k < n; ++k)
        x[k] = std::sin(2.0 * std::numbers::pi * double(k) / period);
    return x;
}

}  // namespace

class TestTrigger : public QObject {
    Q_OBJECT

private slots:
    void risingEdgeInterpolated()
    {
        const double period = 1000.0;
        const std::vector<double> x = sine(5000, period);
        // Rising crossing of 0.5: phase asin(0.5) = pi/6, i.e. period/12 into each cycle.
        const std::optional<double> p = findEdge(x, {0.5, Slope::Rising, 0.0});
        QVERIFY(p.has_value());
        QVERIFY2(std::abs(*p - period / 12.0) < 1e-2, qPrintable(QString::number(*p)));
    }

    void firstCycleNeedsArming()
    {
        // x starts at 0, never below level - hysteresis before its first rise: that rise is ignored
        // and the trigger fires on the next rising crossing, one period later.
        const std::vector<double> x = sine(3000, 1000.0);
        const std::optional<double> p = findEdge(x, {0.0, Slope::Rising, 0.1});
        QVERIFY(p.has_value());
        QVERIFY(std::abs(*p - 1000.0) < 1e-6);
    }

    void fallingEdge()
    {
        const std::vector<double> x = sine(3000, 1000.0);
        const std::optional<double> p = findEdge(x, {0.0, Slope::Falling, 0.0});
        QVERIFY(p.has_value());
        QVERIFY(std::abs(*p - 500.0) < 1e-6);
    }

    void startIndex()
    {
        const std::vector<double> x = sine(5000, 1000.0);
        const std::optional<double> p = findEdge(x, {0.5, Slope::Rising, 0.0}, 1100);
        QVERIFY(p.has_value());
        QVERIFY(std::abs(*p - (2000.0 + 1000.0 / 12.0)) < 1e-2);
        QVERIFY(*p >= 1100.0);
    }

    void hysteresisRejectsNoise()
    {
        // Slow ramp with a ripple of +-0.05 around the level: without hysteresis it re-triggers.
        std::vector<double> x;
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int k = 0; k < 1000; ++k) {
                const double ramp = -1.0 + 2.0 * k / 1000.0;
                x.push_back(ramp + 0.05 * ((k % 2) ? 1.0 : -1.0));
            }
        }
        QVERIFY(findEdges(x, {0.0, Slope::Rising, 0.0}).size() > 3);
        QCOMPARE(findEdges(x, {0.0, Slope::Rising, 0.2}).size(), size_t(3));
    }

    void notFound()
    {
        QVERIFY(!findEdge(std::vector<double>(100, 0.2), {0.5, Slope::Rising, 0.0}).has_value());
        QVERIFY(!findEdge({}, {0.0, Slope::Rising, 0.0}).has_value());
        QCOMPARE(findEdges(sine(10000, 1000.0), {0.0, Slope::Rising, 0.0}).size(), size_t(9));
    }
};

QTEST_GUILESS_MAIN(TestTrigger)
#include "tst_trigger.moc"
