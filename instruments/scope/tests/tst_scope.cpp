// Oscilloscope tests: the ELVISmx value tables and algorithms (docs/instruments/algorithms/
// scope.md), acquisition against the simulated myDAQ, and settings persistence.
#include "scopecontroller.h"
#include "scopecore.h"

#include <mybench/devicehub.h>
#include <mydaq/sim_device.h>

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace mybench::instruments;
namespace sc = mybench::instruments::scope;

namespace {

std::vector<double> sine(size_t n, double fs, double f, double amplitude, double offset = 0.0)
{
    std::vector<double> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = offset + amplitude * std::sin(2 * std::numbers::pi * f * double(i) / fs);
    return x;
}

bool never()
{
    return false;
}

}  // namespace

class TestScope : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void valueLists();
    void ratePolicy_data();
    void ratePolicy();
    void ratePolicyMaximum();
    void inputRange();
    void pretrigger();
    void triggerRising();
    void triggerFalling();
    void triggerNone();
    void triggerHysteresis();
    void measurements();
    void pulseMeasurements();
    void formatting_data();
    void formatting();
    void threshold();
    void autoscaleRules();
    void mathFunctions();
    void validation();
    void planSharedSource();
    void acquireImmediate();
    void acquireEdge();
    void acquireTimeout();
    void acquireAutoTrigger();
    void acquireTwoChannelsAndAudio();
    void autoscaleOnSimulator();
    void rollMode();
    void stopIsQuick();
    void controllerDefaultsAndPersistence();
    void controllerRules();
    void controllerRun();
};

void TestScope::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    mybench::InstrumentController::setRestoreSavedSettings(false);
}

void TestScope::valueLists()
{
    const auto& t = sc::timePerDivValues();
    QCOMPARE(t.size(), size_t(15));
    QCOMPARE(t.front(), 5e-6);
    QCOMPARE(t.back(), 200e-3);
    QCOMPARE(sc::voltsPerDivValues(false).size(), size_t(9));
    QCOMPARE(sc::voltsPerDivValues(false).back(), 5.0);
    QCOMPARE(sc::voltsPerDivValues(true).back(), 1.0);
    QCOMPARE(sc::divisionLabel(5e-3, u"s"_s), u"5 ms"_s);
    QCOMPARE(sc::divisionLabel(500e-6, u"s"_s), u"500 µs"_s);
    QCOMPARE(sc::divisionLabel(1.0, u"V"_s), u"1 V"_s);
    QCOMPARE(sc::divisionLabel(0.02, u"V"_s), u"20 mV"_s);
    QCOMPARE(sc::nearestIndex(t, 5e-3), 9);
}

void TestScope::ratePolicy_data()
{
    QTest::addColumn<double>("timePerDiv");
    QTest::addColumn<double>("rate");
    QTest::addColumn<qint64>("length");
    // docs/instruments/algorithms/scope.md §2, Rmax = 200 kS/s.
    QTest::newRow("5 us") << 5e-6 << 200000.0 << qint64(11);
    QTest::newRow("10 us") << 10e-6 << 200000.0 << qint64(21);
    QTest::newRow("20 us") << 20e-6 << 200000.0 << qint64(41);
    QTest::newRow("50 us") << 50e-6 << 200000.0 << qint64(101);
    QTest::newRow("100 us") << 100e-6 << 200000.0 << qint64(201);
    QTest::newRow("200 us") << 200e-6 << 200000.0 << qint64(401);
    QTest::newRow("500 us") << 500e-6 << 200000.0 << qint64(1001);
    QTest::newRow("1 ms") << 1e-3 << 200000.0 << qint64(2001);
    QTest::newRow("2 ms") << 2e-3 << 125000.0 << qint64(2501);
    QTest::newRow("5 ms") << 5e-3 << 50000.0 << qint64(2501);
    QTest::newRow("10 ms") << 10e-3 << 25000.0 << qint64(2501);
    QTest::newRow("20 ms") << 20e-3 << 12500.0 << qint64(2501);
    QTest::newRow("50 ms") << 50e-3 << 5000.0 << qint64(2501);
    QTest::newRow("100 ms") << 100e-3 << 2500.0 << qint64(2501);
    QTest::newRow("200 ms") << 200e-3 << 2000.0 << qint64(4001);
}

void TestScope::ratePolicy()
{
    QFETCH(double, timePerDiv);
    QFETCH(double, rate);
    QFETCH(qint64, length);
    const sc::RatePlan plan = sc::planRate(timePerDiv, 200000.0);
    QCOMPARE(plan.rate, rate);
    QCOMPARE(plan.samples, length);
}

void TestScope::ratePolicyMaximum()
{
    const sc::RatePlan plan = sc::planRate(1e-3, 200000.0, sc::SamplingMode::HighResolution);
    QCOMPARE(plan.rate, 200000.0);
    QCOMPARE(plan.samples, 2001);
    QCOMPARE(sc::planRate(50e-3, 200000.0, sc::SamplingMode::HighResolution).samples, 100001);
    const sc::RatePlan slow = sc::planRate(200e-3, 200000.0, sc::SamplingMode::HighResolution);
    QCOMPARE(slow.rate, 50000.0);
    QCOMPARE(slow.samples, 100001);
}

void TestScope::inputRange()
{
    using sc::InputRange;
    using sc::Source;
    QCOMPARE(sc::inputRangeFor(Source::AI0, 5.0), 10.0);
    QCOMPARE(sc::inputRangeFor(Source::AI0, 0.5), 10.0);
    QCOMPARE(sc::inputRangeFor(Source::AI0, 0.2), 2.0);
    QCOMPARE(sc::inputRangeFor(Source::AI1, 0.01), 2.0);
    QCOMPARE(sc::inputRangeFor(Source::AudioLeft, 1.0), 2.0);
    QCOMPARE(sc::inputRangeFor(Source::AI0, 0.01, InputRange::Range10V), 10.0);
    QCOMPARE(sc::inputRangeFor(Source::AI0, 5.0, InputRange::Range2V), 2.0);
    QCOMPARE(sc::inputRangeFor(Source::AudioRight, 1.0, InputRange::Range10V), 2.0);
}

void TestScope::pretrigger()
{
    QCOMPARE(sc::pretriggerSamples(2501, 50), 1250);   // 1250.5 -> 1250 (half to even)
    QCOMPARE(sc::pretriggerSamples(2501, 10), 250);
    QCOMPARE(sc::pretriggerSamples(2501, 0), 0);
    QCOMPARE(sc::pretriggerSamples(2501, 100), 2501);
    QCOMPARE(sc::pretriggerSamples(11, 50), 6);        // 5.5 -> 6 (half to even)
    QCOMPARE(sc::pretriggerSamples(101, 50), 50);      // 50.5 -> 50
}

void TestScope::triggerRising()
{
    // Window [pre, pre + N) of a 2N acquisition; the crossing is previous < L <= sample.
    std::vector<double> x{-1, -1, -1, -1, -0.5, 0.0, 0.5, 1, 1, 1};
    const sc::TriggerSettings t{0.0, sc::Slope::Rising, 0.0};
    auto k = sc::findTrigger(x, 5, 2, t);
    QVERIFY(k);
    QCOMPARE(*k, 3);   // x[5] = 0.0 (level <= sample), previous x[4] = -0.5
    // The first sample of the window never triggers (previous starts at the level).
    std::vector<double> y{-1, 0.5, 1, 1, 1, 1};
    QVERIFY(!sc::findTrigger(y, 3, 1, t));
    // Level exactly equal to the previous sample does not count: previous < level is strict.
    std::vector<double> z{0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
    QVERIFY(!sc::findTrigger(z, 3, 0, t));
}

void TestScope::triggerFalling()
{
    std::vector<double> x{1, 1, 1, 0.5, 0.0, -0.5, -1, -1};
    const auto k = sc::findTrigger(x, 4, 1, {0.0, sc::Slope::Falling, 0.0});
    QVERIFY(k);
    QCOMPARE(*k, 3);   // x[4] = 0.0 <= level, previous 0.5 > level
}

void TestScope::triggerNone()
{
    const std::vector<double> x = sine(2000, 1000.0, 10.0, 1.0);
    QVERIFY(!sc::findTrigger(x, 1000, 500, {3.0, sc::Slope::Rising, 0.0}));
}

void TestScope::triggerHysteresis()
{
    // Noise around the level: without hysteresis the first wiggle triggers, with 0.2 V it waits
    // for the signal to go below level - 0.2 first.
    std::vector<double> x{0.0, -0.01, 0.01, -0.01, 0.01, -0.3, -0.1, 0.2, 0.5};
    auto plain = sc::findTrigger(x, 9, 0, {0.0, sc::Slope::Rising, 0.0});
    QVERIFY(plain);
    QCOMPARE(*plain, 2);
    auto hyst = sc::findTrigger(x, 9, 0, {0.0, sc::Slope::Rising, 0.2});
    QVERIFY(hyst);
    QCOMPARE(*hyst, 7);
}

void TestScope::measurements()
{
    const double fs = 50000;
    const std::vector<double> x = sine(2501, fs, 1000.0, 1.0);
    const sc::Measurements m = sc::measure(x, fs, 1.0);
    QVERIFY(std::abs(m.rms - 1.0 / std::sqrt(2.0)) < 2e-3);
    QVERIFY(std::abs(m.frequency - 1000.0) < 0.5);
    QVERIFY(std::abs(m.peakToPeak - 2.0) < 5e-3);   // peaks fall between samples
    QVERIFY(std::abs(m.mean) < 1e-3);
    QVERIFY(std::abs(m.period - 1e-3) < 1e-6);
    QVERIFY(std::abs(m.counterFrequency - 1000.0) < 1.0);
    QVERIFY(std::abs(m.dutyCycle - 0.5) < 0.01);
    // Freq is shown only when the tone exceeds 0.2 x V/div: 1 V peak at 5 V/div -> '?'.
    QVERIFY(std::isnan(sc::measure(x, fs, 5.0).frequency));
    QVERIFY(std::isfinite(sc::measure(x, fs, 2.0).frequency));
}

void TestScope::pulseMeasurements()
{
    // 200 Hz, 20 % duty, +-0.5 V with linear 20 us edges at 100 kS/s.
    const double fs = 100000;
    std::vector<double> x(5000);
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = std::fmod(double(i) / fs, 5e-3);
        double v;
        if (t < 20e-6)
            v = -0.5 + t / 20e-6;
        else if (t < 1e-3)
            v = 0.5;
        else if (t < 1e-3 + 20e-6)
            v = 0.5 - (t - 1e-3) / 20e-6;
        else
            v = -0.5;
        x[i] = v;
    }
    const sc::Measurements m = sc::measure(x, fs, 1.0);
    QVERIFY2(std::abs(m.dutyCycle - 0.2) < 0.01, qPrintable(QString::number(m.dutyCycle)));
    QVERIFY(std::abs(m.period - 5e-3) < 1e-5);
    QVERIFY2(std::abs(m.riseTime - 16e-6) < 3e-6, qPrintable(QString::number(m.riseTime)));
    QVERIFY(std::abs(m.fallTime - 16e-6) < 3e-6);
    QVERIFY(std::abs(m.positiveWidth - 1e-3) < 2e-5);
    QVERIFY(std::abs(m.negativeWidth - 4e-3) < 2e-5);
    QVERIFY(std::abs(m.high - 0.5) < 0.01);
    QVERIFY(std::abs(m.low + 0.5) < 0.01);
    QVERIFY(std::abs(m.rms - 0.5) < 0.01);
}

void TestScope::formatting_data()
{
    QTest::addColumn<double>("value");
    QTest::addColumn<QString>("unit");
    QTest::addColumn<QString>("text");
    QTest::newRow("rms") << 0.70711 << u"V"_s << u"707.11 mV"_s;
    QTest::newRow("khz") << 1000.0 << u"Hz"_s << u"1.000 kHz"_s;
    QTest::newRow("zero") << 0.0 << u"V"_s << u"0.00 V"_s;
    QTest::newRow("us") << 25e-6 << u"s"_s << u"25.00 µs"_s;
    QTest::newRow("volts") << 6.861 << u"V"_s << u"6.861 V"_s;
    QTest::newRow("hz") << 20.053 << u"Hz"_s << u"20.053 Hz"_s;
    QTest::newRow("negative") << -0.29939 << u"V"_s << u"−299.39 mV"_s;
    QTest::newRow("nan") << std::nan("") << u"Hz"_s << u"?"_s;
    QTest::newRow("inf") << HUGE_VAL << u"Hz"_s << u"?"_s;
}

void TestScope::formatting()
{
    QFETCH(double, value);
    QFETCH(QString, unit);
    QFETCH(QString, text);
    QCOMPARE(sc::formatMeasurement(value, unit), text);
    QCOMPARE(sc::formatFixed(50000.0, u"S/s"_s, 2), u"50.00 kS/s"_s);
    QCOMPARE(sc::formatFixed(200000.0, u"S/s"_s, 2), u"200.00 kS/s"_s);
}

void TestScope::threshold()
{
    const std::vector<double> ranges{0.05, 0.1, 0.25, 0.5, 1, 1, 2, 5, 10};
    QCOMPARE(sc::thresholdIndex(ranges, 2.2), 6);     // 6.07 -> 6
    QCOMPARE(sc::thresholdIndex(ranges, 4.0), 7);     // 6.67 -> 7
    QCOMPARE(sc::thresholdIndex(ranges, 0.01), 0);
    QCOMPARE(sc::thresholdIndex(ranges, 50.0), 8);
    QCOMPARE(sc::thresholdIndex(ranges, 1.0), 4);
}

void TestScope::autoscaleRules()
{
    // 1 V peak sine (the simulator's AI 0): A = 2.2 V -> 0.66 V -> 500 mV/div (spec 11).
    sc::AutoscaleProbe p{1.01, -1.01, 1000.0, 1.0};
    sc::AutoscaleChannel c = sc::autoscaleChannel(p, false);
    QCOMPARE(c.signalRange, 2.0);
    QCOMPARE(c.voltsPerDiv, 0.5);
    QCOMPARE(c.midLevel, 0.0);
    QVERIFY(c.tonePresent);
    QCOMPARE(sc::autoscaleTimePerDiv(1000.0), 500e-6);   // 0.5 / f
    QCOMPARE(sc::autoscaleTimePerDiv(200.0), 2e-3);      // 2.5 ms -> 2 ms (threshold + round)
    QVERIFY(std::isnan(sc::autoscaleTimePerDiv(sc::kNaN)));
    // +-7.5 V: A = 16.5 -> 4.95 V -> 5 V/div; +-10 V ramp -> 5 V/div (both observed in ELVISmx).
    c = sc::autoscaleChannel({7.5, -7.5, 136.0, 4.0}, false);
    QCOMPARE(c.voltsPerDiv, 5.0);
    QCOMPARE(sc::autoscaleChannel({10.0, -10.0, 20.0, 5.0}, false).voltsPerDiv, 5.0);
    // Flat signal: no tone.
    c = sc::autoscaleChannel({0.001, -0.001, 50.0, 0.0001}, false);
    QVERIFY(!c.tonePresent);
    QCOMPARE(c.voltsPerDiv, 0.01);
    // Audio: never above 1 V/div.
    c = sc::autoscaleChannel({1.9, -1.9, 440.0, 1.8}, true);
    QVERIFY(c.voltsPerDiv <= 1.0);
}

void TestScope::mathFunctions()
{
    const std::vector<double> a{1, 2, 3};
    const std::vector<double> b{0.5, -1, 2};
    QCOMPARE(sc::computeMath(sc::MathFunction::Sum, a, b), (std::vector<double>{1.5, 1, 5}));
    QCOMPARE(sc::computeMath(sc::MathFunction::Difference, a, b), (std::vector<double>{0.5, 3, 1}));
    QCOMPARE(sc::computeMath(sc::MathFunction::Product, a, b), (std::vector<double>{0.5, -2, 6}));
    QCOMPARE(sc::computeMath(sc::MathFunction::InvertB, a, b), (std::vector<double>{-0.5, 1, -2}));
    const sc::FftResult fft = sc::fftAnalyse(sine(1000, 1000.0, 100.0, std::sqrt(2.0)), 1000.0);
    QCOMPARE(fft.db.size(), size_t(501));
    QCOMPARE(fft.binWidth, 1.0);
    QVERIFY(std::abs(fft.db[100]) < 0.1);   // 1 V RMS -> 0 dBV at bin 100
    QVERIFY(std::abs(fft.peakFrequency - 100.0) < 0.1);
    QVERIFY(std::abs(fft.peakLevel) < 0.1);
}

void TestScope::validation()
{
    sc::Snapshot s;
    QVERIFY(sc::validate(s).isEmpty());
    s.channel[0].enabled = false;
    QCOMPARE(sc::validate(s), u"Enable at least one channel."_s);
    s.channel[0].enabled = true;
    s.channel[1].enabled = true;
    s.channel[0].source = sc::Source::AudioLeft;
    QVERIFY(sc::validate(s).startsWith(u"AI channels cannot be used with AudioInput channels."_s));
    s.channel[1].source = sc::Source::AudioRight;
    QVERIFY(sc::validate(s).isEmpty());
}

void TestScope::planSharedSource()
{
    mydaq::SimDevice sim;
    sc::Snapshot s;
    s.channel[1] = {sc::Source::AI0, true, 0.1, sc::InputRange::Auto};   // same source, 2 V range
    const sc::AcquisitionPlan plan = sc::planAcquisition(s, sim);
    QCOMPARE(plan.config.channels.size(), size_t(1));   // acquired once, shown twice
    QCOMPARE(plan.column[0], 0);
    QCOMPARE(plan.column[1], 0);
    QCOMPARE(plan.range[1], 10.0);                        // the larger range wins
    s.triggerType = sc::TriggerType::Edge;
    QCOMPARE(sc::planAcquisition(s, sim).config.samplesPerChannel, 2 * 2501);
}

void TestScope::acquireImmediate()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    const sc::Snapshot s;   // defaults: CH 0 = AI 0 (1 kHz, 1 V sine), 5 ms/div
    const sc::CycleResult r = acquirer.acquire(sim, s, never);
    QVERIFY(r.record);
    QCOMPARE(r.record->sampleRate, 50000.0);
    QCOMPARE(r.record->data[0].size(), size_t(2501));
    QVERIFY(!r.record->enabled(1));
    QVERIFY(!r.timedOut);
    const sc::Measurements m = sc::measure(r.record->data[0], r.record->sampleRate, 1.0);
    QVERIFY2(std::abs(m.frequency - 1000.0) < 0.1, qPrintable(QString::number(m.frequency, 'f', 4)));
    QVERIFY(std::abs(m.rms - 0.7071) < 0.01);
    QVERIFY(std::abs(m.peakToPeak - 2.0) < 0.05);
    // Second cycle reuses the task.
    QVERIFY(acquirer.acquire(sim, s, never).record);
}

void TestScope::acquireEdge()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.timePerDiv = 500e-6;
    s.triggerType = sc::TriggerType::Edge;
    for (double h : {50.0, 10.0, 90.0}) {
        s.horizontalPercent = h;
        for (int i = 0; i < 3; ++i) {
            const sc::CycleResult r = acquirer.acquire(sim, s, never);
            QVERIFY(r.record);
            QVERIFY(r.record->triggered);
            const auto& x = r.record->data[0];
            const int64_t pre = r.record->triggerIndex;
            QCOMPARE(pre, sc::pretriggerSamples(1001, h));
            QCOMPARE(x.size(), size_t(1001));
            QVERIFY2(x[size_t(pre - 1)] < 0.0 && x[size_t(pre)] >= 0.0,
                     qPrintable(u"%1 %2"_s.arg(x[size_t(pre - 1)]).arg(x[size_t(pre)])));
        }
    }
    // Falling slope at 0.5 V.
    s.slope = sc::Slope::Falling;
    s.level = 0.5;
    s.horizontalPercent = 50;
    const sc::CycleResult r = acquirer.acquire(sim, s, never);
    QVERIFY(r.record);
    const auto& x = r.record->data[0];
    QVERIFY(x[size_t(r.record->triggerIndex - 1)] > 0.5 && x[size_t(r.record->triggerIndex)] <= 0.5);
}

void TestScope::acquireTimeout()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.timePerDiv = 1e-3;
    s.triggerType = sc::TriggerType::Edge;
    s.level = 3.0;   // above the 1 V signal
    const sc::CycleResult r = acquirer.acquire(sim, s, never);
    QVERIFY(r.timedOut);
    QVERIFY(!r.record);   // ELVISmx: nothing to display, the previous record stays
}

void TestScope::acquireAutoTrigger()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.timePerDiv = 1e-3;
    s.triggerType = sc::TriggerType::Edge;
    s.level = 3.0;
    s.noTrigger = sc::NoTrigger::Auto;
    const sc::CycleResult r = acquirer.acquire(sim, s, never);
    QVERIFY(r.timedOut);
    QVERIFY(r.record);
    QVERIFY(!r.record->triggered);
    QCOMPARE(r.record->data[0].size(), size_t(2001));
}

void TestScope::acquireTwoChannelsAndAudio()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.channel[1].enabled = true;   // AI 1: 200 Hz +-0.5 V square, 20 % duty
    sc::CycleResult r = acquirer.acquire(sim, s, never);
    QVERIFY(r.record && r.record->enabled(0) && r.record->enabled(1));
    QVERIFY(r.record->skew[1] > 0);
    const sc::Measurements m1 = sc::measure(r.record->data[1], r.record->sampleRate, 1.0);
    QVERIFY2(std::abs(m1.rms - 0.5) < 0.02, qPrintable(QString::number(m1.rms)));
    QVERIFY(std::abs(m1.frequency - 200.0) < 1.0);
    QVERIFY(std::abs(m1.peakToPeak - 1.0) < 0.05);
    QVERIFY(std::abs(m1.dutyCycle - 0.2) < 0.02);

    // Audio: AC coupled (mean removed), +-2 V range.
    s.channel[0].source = sc::Source::AudioLeft;
    s.channel[1].source = sc::Source::AudioRight;
    r = acquirer.acquire(sim, s, never);
    QVERIFY(r.record);
    QCOMPARE(acquirer.plan().config.channels[0].range, 2.0);
    QVERIFY(std::abs(sc::measure(r.record->data[0], r.record->sampleRate, 0.1).mean) < 1e-9);
    const sc::Measurements left = sc::measure(r.record->data[0], r.record->sampleRate, 0.1);
    QVERIFY(std::abs(left.frequency - 440.0) < 1.0);
}

void TestScope::autoscaleOnSimulator()
{
    mydaq::SimDevice sim;
    sc::Snapshot s;
    s.channel[0].voltsPerDiv = 5.0;
    const auto r = sc::runAutoscale(sim, s, never);
    QVERIFY(r);
    QCOMPARE(r->channel[0].voltsPerDiv, 0.5);
    QVERIFY(r->channel[0].tonePresent);
    QCOMPARE(r->timePerDiv, 500e-6);
    QVERIFY(std::isnan(r->channel[1].voltsPerDiv));
}

void TestScope::rollMode()
{
    mydaq::SimDevice sim;
    sc::Snapshot s;
    s.timePerDiv = 50e-3;   // 5 kS/s, window 2501
    s.roll = true;
    sc::Roller roller;
    roller.start(sim, s);
    QCOMPARE(roller.windowLength(), 2501);
    QVERIFY(roller.matches(sc::planAcquisition(s, sim)));   // no restart while settings are unchanged
    s.channel[0].voltsPerDiv = 0.1;                          // other input range: new task
    QVERIFY(!roller.matches(sc::planAcquisition(s, sim)));
    s.channel[0].voltsPerDiv = 1.0;
    QTest::qWait(150);
    QVERIFY(roller.poll() > 0);
    const int64_t first = roller.filled();
    QVERIFY(first > 100 && first < 2501);
    QTest::qWait(100);
    roller.poll();
    QVERIFY(roller.filled() > first);
    const sc::Record rec = roller.record();
    QCOMPARE(int64_t(rec.data[0].size()), roller.filled());
    QCOMPARE(rec.sampleRate, 5000.0);
    roller.stop();
}

void TestScope::stopIsQuick()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.timePerDiv = 200e-3;                  // 2 s screen, Edge: 4 s acquisition
    s.triggerType = sc::TriggerType::Edge;
    QElapsedTimer clock;
    clock.start();
    const sc::CycleResult r = acquirer.acquire(sim, s, [&] { return clock.elapsed() > 100; });
    QVERIFY(r.stopped);
    QVERIFY2(clock.elapsed() < 400, qPrintable(QString::number(clock.elapsed())));
}

void TestScope::controllerDefaultsAndPersistence()
{
    ScopeController c;
    c.initialize();
    // ELVISmx launch state.
    QCOMPARE(c.channel0()->source(), ScopeChannel::AI0);
    QVERIFY(c.channel0()->enabled());
    QCOMPARE(c.channel1()->source(), ScopeChannel::AI1);
    QVERIFY(!c.channel1()->enabled());
    QCOMPARE(c.channel0()->voltsPerDiv(), 1.0);
    QCOMPARE(c.timePerDiv(), 5e-3);
    QCOMPARE(c.triggerType(), ScopeController::Immediate);
    QCOMPARE(c.horizontalPosition(), 50.0);
    QCOMPARE(c.triggerHysteresis(), 0.0);
    QVERIFY(!c.cursorsOn());
    QCOMPARE(c.cursor1()->channel(), 0);
    QCOMPARE(c.cursor2()->channel(), 1);
    QCOMPARE(c.channel0()->measurementMask(), int(ScopeChannel::kElvisMeasurements));

    c.channel1()->setSource(ScopeChannel::AI0);
    c.channel1()->setVoltsPerDiv(0.2);
    c.channel1()->style()->setLineWidth(3);
    c.channel1()->style()->setLineColor(u"#ff0000"_s);
    c.setTimePerDiv(1e-3);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerLevel(0.25);
    c.math()->setEnabled(true);
    c.math()->setOperation(ScopeMath::Difference);
    c.setCursorsOn(true);
    const QJsonObject saved = c.saveSettings();

    ScopeController d;
    d.initialize();
    d.restoreSettings(saved);
    QCOMPARE(d.channel1()->source(), ScopeChannel::AI0);
    QCOMPARE(d.channel1()->voltsPerDiv(), 0.2);
    QCOMPARE(d.channel1()->style()->lineWidth(), 3.0);
    QCOMPARE(d.channel1()->style()->lineColor(), u"#ff0000"_s);
    QCOMPARE(d.timePerDiv(), 1e-3);
    QCOMPARE(d.triggerType(), ScopeController::Edge);
    QCOMPARE(d.triggerLevel(), 0.25);
    QVERIFY(d.math()->enabled());
    QCOMPARE(d.math()->operation(), ScopeMath::Difference);
    QVERIFY(d.cursorsOn());
    QCOMPARE(d.cursor1()->position(), c.cursor1()->position());

    d.resetSettings();
    QCOMPARE(d.channel1()->source(), ScopeChannel::AI1);
    QCOMPARE(d.channel1()->style()->lineWidth(), 1.5);
    QCOMPARE(d.timePerDiv(), 5e-3);
    QVERIFY(!d.math()->enabled());
}

void TestScope::controllerRules()
{
    ScopeController c;
    c.initialize();
    // Audio sources limit Volts/Div to 1 V.
    c.channel0()->setVoltsPerDiv(5.0);
    QVERIFY(c.setChannelSource(0, ScopeChannel::AudioLeft));
    QCOMPARE(c.channel0()->voltsPerDiv(), 1.0);
    QCOMPARE(c.channel0()->coupling(), u"AC"_s);
    QCOMPARE(c.channel0()->effectiveRange(), 2.0);
    // AI and audio cannot be mixed: the other channel follows (ELVISmx), to the other audio side.
    QCOMPARE(c.channel1()->source(), ScopeChannel::AudioRight);
    c.setChannelEnabled(1, true);
    QVERIFY(c.validationMessage().isEmpty());
    // Back to AI on CH 1: CH 0 returns to its last AI source.
    QVERIFY(c.setChannelSource(1, ScopeChannel::AI0));
    QCOMPARE(c.channel0()->source(), ScopeChannel::AI0);
    QVERIFY(c.setChannelSource(0, ScopeChannel::AudioRight));
    QCOMPARE(c.channel1()->source(), ScopeChannel::AudioLeft);   // no own audio choice: the partner side
    // A restored session may still mix them: Run refuses with the ELVISmx message.
    c.channel1()->setSource(ScopeChannel::AI1);
    QVERIFY(c.validationMessage().startsWith(u"AI channels"_s));
    // Trigger source follows the enabled channel.
    c.setChannelSource(0, ScopeChannel::AI0);
    c.setChannelSource(1, ScopeChannel::AI1);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerSource(ScopeController::Chan1Source);
    c.setChannelEnabled(1, false);
    QCOMPARE(c.triggerSource(), ScopeController::Chan0Source);
    // Coercions.
    c.setTriggerLevel(250);
    QCOMPARE(c.triggerLevel(), 100.0);
    c.setHorizontalPosition(-3);
    QCOMPARE(c.horizontalPosition(), 0.0);
    c.channel0()->setPosition(7.3);
    QCOMPARE(c.channel0()->position(), 5.0);
    c.setTimePerDiv(3e-3);
    QCOMPARE(c.timePerDiv(), 2e-3);
    QCOMPARE(c.plannedRecordLength(), 2501);
}

void TestScope::controllerRun()
{
    mybench::DeviceHub::instance()->select(u"sim:0"_s);
    ScopeController c;
    c.initialize();
    // Run Continuously with the defaults: 50 kS/s, 2501 points, AI 0 test signal.
    c.run();
    QTRY_VERIFY_WITH_TIMEOUT(c.hasRecord(), 5000);
    QCOMPARE(c.recordRate(), 50000.0);
    QCOMPARE(c.recordLength(), 2501);
    QVERIFY(std::abs(c.channel0()->rms() - 0.707) < 0.01);
    QCOMPARE(c.formatRate(c.recordRate()), u"50.00 kS/s"_s);

    // Edge without crossing: Timeout LED, graph cleared (ELVISmx), waiting status.
    c.setTimePerDiv(1e-3);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerLevel(3.0);
    QTRY_VERIFY_WITH_TIMEOUT(c.timedOut(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.hasRecord(), 5000);
    QVERIFY(c.waitingForTrigger());
    QVERIFY(!c.channel0()->hasResults());
    // Back inside the signal: triggered records, trigger at 50 % of the screen.
    c.setTriggerLevel(0.0);
    QTRY_VERIFY_WITH_TIMEOUT(c.hasRecord() && !c.timedOut(), 5000);
    QVERIFY(std::abs(c.triggerTime() - 0.5 * c.screenTime()) < 1e-5);
    QVERIFY(!c.waitingForTrigger());

    // Autoscale while running: 500 mV/div, 500 µs/div (spec 11).
    c.channel0()->setVoltsPerDiv(5.0);
    c.autoscale();
    QVERIFY(c.autoscaling());
    QTRY_VERIFY_WITH_TIMEOUT(!c.autoscaling(), 5000);
    QCOMPARE(c.channel0()->voltsPerDiv(), 0.5);
    QCOMPARE(c.timePerDiv(), 500e-6);
    QVERIFY(c.isRunning());

    // Export of the displayed record.
    QTRY_VERIFY_WITH_TIMEOUT(c.recordLength() == 1001, 5000);
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 2000);

    // Autoscale while idle: probe only, no acquisition loop afterwards (ELVISmx).
    c.channel0()->setVoltsPerDiv(5.0);
    const int records = c.acquisitionCount();
    c.autoscale();
    QTRY_VERIFY_WITH_TIMEOUT(!c.autoscaling() && c.isIdle(), 5000);
    QCOMPARE(c.channel0()->voltsPerDiv(), 0.5);
    QCOMPARE(c.acquisitionCount(), records);

    // Single: one record, then idle.
    c.runOnce();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 5000);
    QCOMPARE(c.acquisitionCount(), records + 1);

    // Stop reacts quickly even at 200 ms/div with Edge (4 s records).
    c.setTimePerDiv(200e-3);
    c.run();
    QTRY_VERIFY_WITH_TIMEOUT(c.state() == ScopeController::Running, 2000);
    QTest::qWait(300);
    QElapsedTimer clock;
    clock.start();
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 1000);
    QVERIFY2(clock.elapsed() < 400, qPrintable(QString::number(clock.elapsed())));

    // Roll mode at 50 ms/div shows data before a full screen was acquired.
    c.setTimePerDiv(50e-3);
    c.setRollMode(true);
    QVERIFY(c.rolling());
    c.run();
    QTRY_VERIFY_WITH_TIMEOUT(c.hasRecord() && c.recordLength() < 2501, 3000);
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 2000);
    QTemporaryDir dir;
    const QString csv = dir.filePath(u"scope.csv"_s);
    QVERIFY(c.exportData(QUrl::fromLocalFile(csv)));
    QFile file(csv);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(text.contains(u"# Time/Div: 50 ms"_s));
    QVERIFY(text.contains(u"Time (s),CH 0 (V)"_s));
    const QString txt = dir.filePath(u"scope.txt"_s);
    QVERIFY(c.exportElvisText(QUrl::fromLocalFile(txt)));
    QFile log(txt);
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(log.readAll()).startsWith(u"waveform\tCh0"_s));
}

QTEST_MAIN(TestScope)
#include "tst_scope.moc"
