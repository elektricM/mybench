// Oscilloscope tests: the ELVISmx value tables and algorithms (docs/instruments/algorithms/
// scope.md), acquisition against the simulated myDAQ, and settings persistence.
#include "scopecontroller.h"
#include "scopecore.h"

#include <mybench/dataexport.h>
#include <mybench/devicehub.h>
#include <mybench/instrumentregistry.h>
#include <mydaq/sim_device.h>

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

#include <cmath>
#include <memory>
#include <numbers>
#include <random>

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
    void triggerJitter();
    void triggerJitter_controller();
    void searchWindowGrows();
    void extraMeasurements();
    void phaseMeasurement();
    void peakMeasurement();
    void triggerTimeoutModes();
    void persistenceEpoch();
    void averaging();
    void acquireAutoTrigger();
    void acquireTwoChannelsAndAudio();
    void autoscaleOnSimulator();
    void rollMode();
    void stopIsQuick();
    void controllerDefaultsAndPersistence();
    void controllerRules();
    void controllerRun();
    void timeoutOnlyWithEdge();
    void traceStylePalettes();
    void cursorStylePalettes();
    void resetRestoresStyles();
    void legacyStylesMigrate();
    void legacyMathFftMigrates();
    void measurementSelectionIsShared();
    void stateText();
    void textLogWriter();
    void triggerTypeEdgeResetsSource();
    void wheelSteps();
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
    QTest::newRow("negative") << -0.29939 << u"V"_s << u"-299.39 mV"_s;
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

// Alex: "triggering is wobbly, even on the simulator". Cause: the record started at the sample
// after the crossing, so the edge sat anywhere within one sample (0.9 division at 5 us/div) from
// frame to frame. Sub-sample alignment puts the interpolated crossing at the trigger position.
void TestScope::triggerJitter()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.triggerType = sc::TriggerType::Edge;
    s.level = 0.0;
    s.horizontalPercent = 50;
    for (double timePerDiv : {5e-6, 20e-6, 100e-6, 1e-3}) {
        s.timePerDiv = timePerDiv;
        std::vector<double> rawOffset, alignedTime;
        for (int frame = 0; frame < 24; ++frame) {
            // Fast time bases only see an edge in a fraction of the windows: keep arming.
            sc::CycleResult r;
            for (int attempt = 0; attempt < 40 && !(r.record && r.record->triggered); ++attempt)
                r = acquirer.acquire(sim, s, never);
            QVERIFY(r.record && r.record->triggered);
            const auto& x = r.record->data[0];
            const int64_t pre = r.record->triggerIndex;
            const double fs = r.record->sampleRate;
            const double f = (s.level - x[size_t(pre - 1)]) / (x[size_t(pre)] - x[size_t(pre - 1)]);
            QVERIFY(f >= 0.0 && f <= 1.0);
            rawOffset.push_back((double(pre) - 1.0 + f) / fs);   // crossing time, undisplaced
            alignedTime.push_back(rawOffset.back() + r.record->triggerShift / fs);
        }
        auto spread = [](const std::vector<double>& v) {
            return *std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end());
        };
        const double dt = 1.0 / 200000.0;
        // Before the alignment the edge moved by about one sample from frame to frame (0.9 division
        // at 5 us/div); after it, the crossing does not move.
        if (timePerDiv == 5e-6)
            QVERIFY2(spread(rawOffset) > 0.5 * dt, qPrintable(u"unaligned spread %1 s"_s.arg(spread(rawOffset))));
        // The displaced record puts the crossing at the trigger position within a small fraction
        // of a sample (limited by the simulator noise), whatever the frame.
        QVERIFY2(spread(alignedTime) < 0.05 * std::max(dt, timePerDiv / 200.0),
                 qPrintable(u"%1 s/div: spread %2 s"_s.arg(timePerDiv).arg(spread(alignedTime))));
    }
}

// Rare events: without a crossing the search window doubles (up to ~0.25 s of samples) and
// shrinks again after a run of triggered cycles.
// The same through the controller: the crossing in the published trace buffer sits at the trigger
// position on every frame.
void TestScope::triggerJitter_controller()
{
    mybench::DeviceHub::instance()->select(u"sim:0"_s);
    ScopeController c;
    c.initialize();
    c.setTimePerDiv(100e-6);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerLevel(0.0);
    QSignalSpy frames(&c, &ScopeController::recordChanged);
    c.run();
    std::vector<double> crossing;
    QElapsedTimer clock;
    clock.start();
    while (crossing.size() < 20 && clock.elapsed() < 15000) {
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 5000);
        frames.clear();
        if (!c.hasRecord())
            continue;
        const auto snap = c.ch0()->snapshot();
        const double half = 0.5 * c.screenTime();
        for (size_t i = 1; i < snap->size(); ++i) {
            const double x = snap->xAt(i), y = snap->yAt(i), y0 = snap->yAt(i - 1);
            if (std::abs(x - half) < 0.1 * c.screenTime() && y0 < 0.0 && y >= 0.0) {
                crossing.push_back(snap->xAt(i - 1) + (0.0 - y0) / (y - y0) * (x - snap->xAt(i - 1)));
                break;
            }
        }
    }
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 2000);
    QVERIFY(crossing.size() >= 10);
    const auto [lo, hi] = std::minmax_element(crossing.begin(), crossing.end());
    const double dt = 1.0 / 200000.0;
    QVERIFY2(*hi - *lo < 0.1 * dt, qPrintable(u"crossing spread %1 s (sample %2 s)"_s.arg(*hi - *lo).arg(dt)));
    QVERIFY(std::abs(0.5 * (*hi + *lo) - 0.5 * c.screenTime()) < 0.1 * dt);
}

void TestScope::searchWindowGrows()
{
    mydaq::SimDevice sim;
    sc::Acquirer acquirer;
    sc::Snapshot s;
    s.timePerDiv = 1e-3;
    s.triggerType = sc::TriggerType::Edge;
    s.level = 3.0;   // never crossed
    QCOMPARE(acquirer.searchFactor(), int64_t(1));
    int64_t previous = 0;
    for (int i = 0; i < 5; ++i) {
        const sc::CycleResult r = acquirer.acquire(sim, s, never);
        QVERIFY(r.timedOut && !r.record);
        QVERIFY(acquirer.plan().config.samplesPerChannel >= previous);
        previous = acquirer.plan().config.samplesPerChannel;
    }
    const int64_t grown = acquirer.searchFactor();
    QVERIFY(grown > 1);
    // 200 kS/s: capped at 0.25 s = 50 000 extra samples.
    QVERIFY(acquirer.plan().config.samplesPerChannel <= 2001 + 50000);
    s.level = 0.0;
    for (int i = 0; i < 24; ++i) {
        const sc::CycleResult r = acquirer.acquire(sim, s, never);
        QVERIFY(r.record && r.record->triggered);
        QCOMPARE(r.record->data[0].size(), size_t(2001));
    }
    QVERIFY(acquirer.searchFactor() < grown);   // shrinks back during a triggered run
}

void TestScope::extraMeasurements()
{
    // A 1 kHz square wave, 0..2 V, with 10 % overshoot spikes: 50 kS/s.
    const double fs = 50000;
    std::vector<double> x;
    for (int i = 0; i < 5000; ++i) {
        const double t = double(i) / fs;
        const double phase = std::fmod(t * 1000.0, 1.0);
        double v = phase < 0.5 ? 2.0 : 0.0;
        if (phase >= 0.0 && phase < 0.02)   // ring at the rising edge
            v = 2.2;
        x.push_back(v);
    }
    const sc::Measurements m = sc::measure(x, fs, 0.5);
    QVERIFY(std::abs(m.dutyCycle - 0.5) < 0.02);
    QVERIFY(std::abs(m.overshoot - 10.0) < 1.0);
    QVERIFY(std::abs(m.preshoot) < 1.0);
    QVERIFY(std::abs(m.acRms - std::sqrt(m.rms * m.rms - m.mean * m.mean)) < 1e-9);
    QVERIFY(m.crestFactor > 1.0 && m.crestFactor < 2.0);
    // Slew rate of a linear ramp 0 -> 1 V in 100 samples (500 V/s at 50 kS/s), held, then a fast fall.
    std::vector<double> ramp;
    for (int period = 0; period < 8; ++period) {
        for (int i = 0; i < 100; ++i)
            ramp.push_back(double(i) / 99.0);
        for (int i = 0; i < 100; ++i)
            ramp.push_back(1.0);
        for (int i = 0; i < 100; ++i)
            ramp.push_back(0.0);
    }
    const sc::Measurements r = sc::measure(ramp, fs, 0.1);
    const double expectedSlew = fs / 99.0;   // 1 V in 99 sample intervals
    QVERIFY2(std::abs(r.slewRate - expectedSlew) < 0.03 * expectedSlew, qPrintable(QString::number(r.slewRate)));
    const auto sinus = sc::measure(sine(5000, fs, 1000, 1.0), fs, 0.5);
    QVERIFY(std::abs(sinus.crestFactor - std::sqrt(2.0)) < 0.01);
    QVERIFY(std::abs(sinus.acRms - std::sqrt(0.5)) < 0.005);
}

void TestScope::peakMeasurement()
{
    // Peak = largest |v| of the record: an offset sine 0.5 + 1.2 sin -> +1.7 V; its negative -> 1.7 V too.
    std::vector<double> x(2000);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.5 + 1.2 * std::sin(2 * M_PI * 1000.0 * double(i) / 200000.0);
    const sc::Measurements m = sc::measure(x, 200000.0, 1.0);
    QVERIFY(std::abs(m.peak - 1.7) < 1e-3);
    for (double& v : x)
        v = -v;
    QVERIFY(std::abs(sc::measure(x, 200000.0, 1.0).peak - 1.7) < 1e-3);
    QVERIFY(std::isnan(sc::Measurements{}.peak));
    // In the catalog (label "Peak", volts) and as a readout.
    bool found = false;
    for (const QVariant& e : ScopeChannel::measurementCatalog())
        found = found || (e.toMap().value(u"flag"_s).toInt() == ScopeChannel::Peak && e.toMap().value(u"label"_s).toString() == u"Peak"_s);
    QVERIFY(found);
    QCOMPARE(ScopeChannel::measurementUnit(ScopeChannel::Peak), u"V"_s);
    // The numeric accessor used by the measurement cards' statistics.
    ScopeController c;
    c.initialize();
    QVERIFY(std::isnan(c.channel0()->measurement(ScopeChannel::Peak)));
}

void TestScope::phaseMeasurement()
{
    const double fs = 50000;
    const auto a = sine(5000, fs, 1000, 1.0);
    for (double degrees : {0.0, 30.0, 90.0, -45.0, 170.0}) {
        std::vector<double> b(a.size());
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = 0.7 * std::sin(2 * std::numbers::pi * 1000.0 * double(i) / fs + degrees * std::numbers::pi / 180.0);
        const double phase = sc::phaseBetween(a, b, fs, 1000.0);
        QVERIFY2(std::abs(phase - degrees) < 0.5, qPrintable(u"%1 -> %2"_s.arg(degrees).arg(phase)));
    }
    // The channel skew is compensated: b sampled 2 us later reads 0.72 degrees more phase.
    std::vector<double> late(a.size());
    for (size_t i = 0; i < late.size(); ++i)
        late[i] = std::sin(2 * std::numbers::pi * 1000.0 * (double(i) / fs + 2e-6));
    QVERIFY(std::abs(sc::phaseBetween(a, late, fs, 1000.0, 2e-6)) < 0.1);
    QVERIFY(std::isnan(sc::phaseBetween(a, std::vector<double>(a.size(), 0.0), fs, 1000.0)));
    QVERIFY(std::isnan(sc::phaseBetween(std::vector<double>(10, 1.0), a, fs, 1000.0)));
}

// Alex: the trigger timeout was too fast and "Waiting for trigger" replaced the trace. Now nothing
// changes until the (configurable) timeout has passed; Normal keeps the last trace and waits.
void TestScope::triggerTimeoutModes()
{
    mybench::DeviceHub::instance()->select(u"sim:0"_s);
    ScopeController c;
    c.initialize();
    QCOMPARE(c.noTriggerMode(), ScopeController::KeepLastTrace);   // Normal is the default
    QCOMPARE(c.triggerTimeout(), 1.0);
    QCOMPARE(c.triggerHoldoff(), 0.0);
    c.setTriggerHoldoff(20.0);
    QCOMPARE(c.triggerHoldoff(), 10.0);
    c.setTriggerHoldoff(0.0);
    c.setTimePerDiv(1e-3);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerTimeout(0.0);   // never
    c.run();
    QTRY_VERIFY_WITH_TIMEOUT(c.hasRecord() && !c.timedOut(), 5000);
    // The signal disappears from the trigger point (level above it): the trace stays and nothing
    // reports a timeout, for as long as we care to look.
    c.setTriggerLevel(3.0);
    QTest::qWait(1500);
    QVERIFY(c.hasRecord());
    QVERIFY(!c.timedOut());
    QVERIFY(!c.waitingForTrigger());   // Never: no waiting state, no dimming
    // With a timeout the waiting indicator (not the trace) appears once it has passed.
    c.setTriggerTimeout(0.5);
    QTRY_VERIFY_WITH_TIMEOUT(c.waitingForTrigger(), 3000);
    QVERIFY(c.hasRecord());
    // Auto with a short timeout free-runs: a record without trigger appears.
    c.setNoTriggerMode(ScopeController::ShowUntriggered);
    c.setTriggerTimeout(0.25);
    const int before = c.acquisitionCount();
    QTRY_VERIFY_WITH_TIMEOUT(c.acquisitionCount() > before + 2, 4000);
    QVERIFY(std::isnan(c.triggerTime()));
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 2000);
}

void TestScope::averaging()
{
    // Noise falls like 1 / sqrt(N) on a repetitive signal; changing the length restarts the mean.
    sc::RecordAverager averager;
    QCOMPARE(averager.apply(*std::make_unique<sc::Record>().get()), 1);   // off: untouched
    averager.setWindow(16);
    auto rms = [](const std::vector<double>& x) {
        double s = 0;
        for (double v : x)
            s += v * v;
        return std::sqrt(s / double(x.size()));
    };
    std::mt19937 rng(1);
    std::normal_distribution<double> noise(0.0, 0.1);
    double last = 0;
    for (int i = 0; i < 16; ++i) {
        sc::Record r;
        r.sampleRate = 1000;
        r.data[0].assign(2000, 0.0);
        for (double& v : r.data[0])
            v = 1.0 + noise(rng);
        std::vector<double> ac(r.data[0]);
        QCOMPARE(averager.apply(r), i + 1);
        for (double& v : r.data[0])
            v -= 1.0;
        last = rms(r.data[0]);
        if (i == 0)
            QVERIFY(std::abs(last - 0.1) < 0.01);
    }
    QVERIFY2(last < 0.035, qPrintable(QString::number(last)));   // 0.1 / 4
    sc::Record shorter;
    shorter.sampleRate = 1000;
    shorter.data[0].assign(100, 2.0);
    QCOMPARE(averager.apply(shorter), 1);     // length changed: restart
    QCOMPARE(shorter.data[0][0], 2.0);
    ScopeController c;
    c.initialize();
    c.setAveraging(5);
    QCOMPARE(c.averaging(), 4);
    c.setAveraging(1000);
    QCOMPARE(c.averaging(), 64);
}

void TestScope::persistenceEpoch()
{
    ScopeController c;
    c.initialize();
    const int start = c.persistenceEpoch();
    c.channel0()->setVoltsPerDiv(2.0);
    QVERIFY(c.persistenceEpoch() > start);
    int e = c.persistenceEpoch();
    c.setTimePerDiv(1e-3);
    QVERIFY(c.persistenceEpoch() > e);
    e = c.persistenceEpoch();
    c.channel1()->setPosition(1.0);
    QVERIFY(c.persistenceEpoch() > e);
    e = c.persistenceEpoch();
    c.setTriggerLevel(0.5);
    QVERIFY(c.persistenceEpoch() > e);
    e = c.persistenceEpoch();
    c.setPersistence(500);          // not a display-geometry change
    c.setNoTriggerMode(ScopeController::ShowUntriggered);
    QCOMPARE(c.persistenceEpoch(), e);
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
    c.channel1()->style()->setLineColor(QColor(u"#ff0000"_s));
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
    QCOMPARE(d.channel1()->style()->lineColor(), QColor(u"#ff0000"_s));
    QCOMPARE(d.timePerDiv(), 1e-3);
    QCOMPARE(d.triggerType(), ScopeController::Edge);
    QCOMPARE(d.triggerLevel(), 0.25);
    QVERIFY(d.math()->enabled());
    QCOMPARE(d.math()->operation(), ScopeMath::Difference);
    QVERIFY(d.cursorsOn());
    QCOMPARE(d.cursor1()->position(), c.cursor1()->position());

    d.resetSettings();
    QCOMPARE(d.channel1()->source(), ScopeChannel::AI1);
    QCOMPARE(d.channel1()->style()->lineWidth(), 1.0);   // ELVISmx default width
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

    // Edge without crossing: after the timeout the LED lights, the graph is cleared (ELVISmx mode),
    // waiting status. (The default mode keeps the last trace: see triggerTimeoutModes.)
    c.setNoTriggerMode(ScopeController::ClearGraph);
    c.setTriggerTimeout(0.2);
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
}


// Audit scope #2/#3: the Graph Properties palettes of ELVISmx.
// The Timeout indicator belongs to Edge triggering: it never lights in Immediate, and switching to
// Immediate after a timeout puts it out (also once the run is over).
void TestScope::timeoutOnlyWithEdge()
{
    mybench::DeviceHub::instance()->select(u"sim:0"_s);
    ScopeController c;
    c.initialize();
    c.run();
    QTRY_VERIFY_WITH_TIMEOUT(c.hasRecord(), 5000);
    QVERIFY(!c.timedOut());   // Immediate
    c.setTriggerTimeout(0.2);
    c.setTimePerDiv(1e-3);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerLevel(3.0);
    QTRY_VERIFY_WITH_TIMEOUT(c.timedOut(), 5000);
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 5000);
    QVERIFY(c.timedOut());   // last state of the run
    c.setTriggerType(ScopeController::Immediate);
    QVERIFY(!c.timedOut());
}

// The trace and cursor looks are the shared TraceStyle (edited in the TraceStyleDialog) and persist.
void TestScope::traceStylePalettes()
{
    using TS = mybench::plot::TraceStyle;
    ScopeController c;
    c.initialize();
    TS* style = c.channel0()->style();
    QCOMPARE(style->lineWidth(), 1.0);   // ELVISmx default width
    QVERIFY(style->isDefault());
    style->setLineWidth(0);
    QCOMPARE(style->lineWidth(), 0.0);   // "none": points only
    style->setLineStyle(TS::DashDotDot);
    style->setInterpolation(TS::CenterStepVertical);
    style->setPointStyle(TS::Diamond);
    style->setFillColor(QColor(u"#123456"_s));
    ScopeController d;
    d.initialize();
    d.restoreSettings(c.saveSettings());
    TS* restored = d.channel0()->style();
    QCOMPARE(restored->lineWidth(), 0.0);
    QCOMPARE(restored->lineStyle(), TS::DashDotDot);
    QCOMPARE(restored->interpolation(), TS::CenterStepVertical);
    QCOMPARE(restored->pointStyle(), TS::Diamond);
    QCOMPARE(restored->fillColor(), QColor(u"#123456"_s));
    // The reference traces have a style of their own (dashed / dotted by default).
    QCOMPARE(d.refAStyle()->lineStyle(), TS::Dash);
    QCOMPARE(d.refBStyle()->lineStyle(), TS::Dot);
    QCOMPARE(d.refBStyle()->lineWidth(), 1.2);
    c.refAStyle()->setLineColor(QColor(u"#00ff00"_s));
    ScopeController e;
    e.initialize();
    e.restoreSettings(c.saveSettings());
    QCOMPARE(e.refAStyle()->lineColor(), QColor(u"#00ff00"_s));
    // A style change is a settings change (saved, live).
    QSignalSpy changed(&c, &ScopeController::settingsChanged);
    c.math()->style()->setLineWidth(2);
    QCOMPARE(changed.count(), 1);
}

void TestScope::cursorStylePalettes()
{
    using TS = mybench::plot::TraceStyle;
    ScopeController c;
    c.initialize();
    ScopeCursor* cursor = c.cursor1();
    QCOMPARE(cursor->style()->lineStyle(), TS::Dash);   // cursors are dashed
    QCOMPARE(cursor->style()->lineWidth(), 1.0);
    cursor->style()->setLineWidth(3);
    cursor->style()->setPointStyle(TS::Circle);
    cursor->style()->setLineStyle(TS::DashDotDot);
    cursor->setCursorStyle(ScopeCursor::SmallCrosshair);
    ScopeController d;
    d.initialize();
    d.restoreSettings(c.saveSettings());
    QCOMPARE(d.cursor1()->style()->lineWidth(), 3.0);
    QCOMPARE(d.cursor1()->style()->pointStyle(), TS::Circle);
    QCOMPARE(d.cursor1()->cursorStyle(), ScopeCursor::SmallCrosshair);
    cursor->setCursorStyle(ScopeCursor::NoCursor);
    QCOMPARE(cursor->cursorStyle(), ScopeCursor::NoCursor);
    cursor->resetStyle();
    QCOMPARE(cursor->style()->lineWidth(), 1.0);
    QCOMPARE(cursor->style()->lineStyle(), TS::Dash);
    QCOMPARE(cursor->cursorStyle(), ScopeCursor::VerticalLine);
    QCOMPARE(cursor->style()->pointStyle(), TS::NoPoints);
}

// Reset Settings… (pane ⋯ menu) also returns every trace style, the cursors and the measurement
// selection to their defaults (the old Graph Properties "Reset to Default").
void TestScope::resetRestoresStyles()
{
    using TS = mybench::plot::TraceStyle;
    ScopeController c;
    c.initialize();
    c.channel0()->style()->setLineWidth(4);
    c.channel1()->style()->setPointStyle(TS::Cross);
    c.math()->style()->setLineColor(QColor(u"#ff0000"_s));
    c.refBStyle()->setLineWidth(3);
    c.cursor2()->style()->setLineWidth(5);
    c.setMeasurementShown(ScopeChannel::Mean, true);
    c.setShowDeltaTime(false);
    c.resetSettings();
    QCOMPARE(c.channel0()->style()->lineWidth(), 1.0);
    QCOMPARE(c.channel1()->style()->pointStyle(), TS::NoPoints);
    QVERIFY(!c.math()->style()->lineColor().isValid());
    QCOMPARE(c.refBStyle()->lineWidth(), 1.2);
    QCOMPARE(c.refBStyle()->lineStyle(), TS::Dot);
    QCOMPARE(c.cursor2()->style()->lineWidth(), 1.0);
    QCOMPARE(c.channel0()->measurementMask(), int(ScopeChannel::kElvisMeasurements));
    QVERIFY(c.showDeltaTime());
}

// Sessions of older versions: ScopeTraceStyle key names, the cursor look in the cursor, the
// Graph Properties background (now the app's dark-displays preference) and Display Measurements.
void TestScope::legacyStylesMigrate()
{
    using TS = mybench::plot::TraceStyle;
    const QByteArray old = R"({
        "background": "White",
        "channel0": { "source": "AI0", "enabled": true, "voltsPerDiv": 0.5, "displayMeasurements": false,
                      "style": { "lineColor": "#ff8800", "lineWidth": 2, "lineStyle": "DashDot",
                                 "pointStyle": "FilledSquare", "interpolation": "Step", "fillTo": "Zero",
                                 "fillColor": "", "points": true } },
        "mathTrace": { "enabled": false, "operation": "Difference",
                       "style": { "lineStyle": "Solid", "pointStyle": "NoPoints", "fillTo": "NoFill" } },
        "cursor1": { "channel": 1, "color": "#00ffff", "lineStyle": "Dot", "lineWidth": 3,
                     "pointStyle": "Circle", "cursorStyle": "FullCrosshair", "showValue": false },
        "refAStyle": { "lineStyle": "DashDotDot", "lineWidth": 2 }
    })";
    ScopeController c;
    c.initialize();
    c.restoreSettings(QJsonDocument::fromJson(old).object());
    TS* style = c.channel0()->style();
    QCOMPARE(style->lineColor(), QColor(u"#ff8800"_s));
    QCOMPARE(style->lineWidth(), 2.0);
    QCOMPARE(style->lineStyle(), TS::DashDot);
    QCOMPARE(style->pointStyle(), TS::FilledSquare);
    QCOMPARE(style->interpolation(), TS::Step);
    QCOMPARE(style->fillTo(), TS::Zero);
    QVERIFY(!style->fillColor().isValid());
    QCOMPARE(c.channel0()->voltsPerDiv(), 0.5);
    QCOMPARE(c.math()->operation(), ScopeMath::Difference);
    QCOMPARE(c.math()->style()->pointStyle(), TS::NoPoints);
    QCOMPARE(c.math()->style()->fillTo(), TS::NoFill);
    QCOMPARE(c.cursor1()->channel(), 1);
    QCOMPARE(c.cursor1()->style()->lineColor(), QColor(u"#00ffff"_s));
    QCOMPARE(c.cursor1()->style()->lineStyle(), TS::Dot);
    QCOMPARE(c.cursor1()->style()->lineWidth(), 3.0);
    QCOMPARE(c.cursor1()->style()->pointStyle(), TS::Circle);
    QCOMPARE(c.cursor1()->cursorStyle(), ScopeCursor::FullCrosshair);
    QVERIFY(!c.cursor1()->showValue());
    QCOMPARE(c.refAStyle()->lineStyle(), TS::DashDotDot);
    // Saved again in the new format, without the removed settings.
    const QJsonObject saved = c.saveSettings();
    QVERIFY(!saved.contains(u"background"_s));
    QVERIFY(!saved.value(u"channel0"_s).toObject().contains(u"displayMeasurements"_s));
    QCOMPARE(saved.value(u"channel0"_s).toObject().value(u"style"_s).toObject().value(u"lineStyle"_s).toString(), u"dashDot"_s);
}

// Older sessions had the spectrum as a math function: it moves to the FFT section (the only FFT
// path) and the math trace goes off; a disabled FFT math only falls back to a regular operation.
void TestScope::legacyMathFftMigrates()
{
    const QByteArray fftOfCh1 = R"({ "fftSource": "FftOff",
        "mathTrace": { "enabled": true, "operation": "FftB", "voltsPerDiv": 2 } })";
    ScopeController c;
    c.initialize();
    c.restoreSettings(QJsonDocument::fromJson(fftOfCh1).object());
    QCOMPARE(c.fftSource(), ScopeController::FftChannel1);
    QVERIFY(c.fftShown());
    QVERIFY(!c.math()->enabled());
    QVERIFY(!c.math()->fft());
    QCOMPARE(c.math()->operation(), ScopeMath::Sum);
    QCOMPARE(c.math()->voltsPerDiv(), 2.0);

    const QByteArray disabledFft = R"({ "fftSource": "FftOff",
        "mathTrace": { "enabled": false, "operation": "FftA" } })";
    ScopeController d;
    d.initialize();
    d.restoreSettings(QJsonDocument::fromJson(disabledFft).object());
    QCOMPARE(d.fftSource(), ScopeController::FftOff);
    QVERIFY(!d.fftShown());
    QCOMPARE(d.math()->operation(), ScopeMath::Sum);

    // The math trace cannot become an FFT any more; the FFT view does not depend on math.
    d.math()->setEnabled(true);
    d.math()->setOperation(ScopeMath::FftA);
    QCOMPARE(d.math()->operation(), ScopeMath::Sum);
    QVERIFY(!d.fftShown());
    d.setFftSource(ScopeController::FftChannel0);
    QVERIFY(d.fftShown());
}

// The measurement strip has one set of rows for every channel: adding or removing one writes both
// channels; older sessions with per-channel choices keep the union. Every measurement can be shown.
void TestScope::measurementSelectionIsShared()
{
    ScopeController c;
    c.initialize();
    c.setMeasurementShown(ScopeChannel::Phase, true);   // the last flag of the catalog
    c.setMeasurementShown(ScopeChannel::Rms, false);
    const int expected = (ScopeChannel::kElvisMeasurements & ~ScopeChannel::Rms) | ScopeChannel::Phase;
    QCOMPARE(c.channel0()->measurementMask(), expected);
    QCOMPARE(c.channel1()->measurementMask(), expected);
    QVERIFY(c.channel1()->shows(ScopeChannel::Phase));
    QCOMPARE(c.measurementCatalog().size(), qsizetype(ScopeChannel::kMeasurementCount));
    c.channel0()->setMeasurementMask(ScopeChannel::kAllMeasurements);
    QCOMPARE(c.channel0()->readouts().size(), qsizetype(ScopeChannel::kMeasurementCount));

    const QByteArray old = R"({ "channel0": { "measurementMask": 1 }, "channel1": { "measurementMask": 8 } })";
    ScopeController d;
    d.initialize();
    d.restoreSettings(QJsonDocument::fromJson(old).object());
    QCOMPARE(d.channel0()->measurementMask(), int(ScopeChannel::Rms | ScopeChannel::Mean));
    QCOMPARE(d.channel1()->measurementMask(), int(ScopeChannel::Rms | ScopeChannel::Mean));
}

// The pane-header state text comes from the controller only: Running (Immediate), Triggered,
// Waiting for trigger (Normal / Clear) and Timeout — free running (Auto).
void TestScope::stateText()
{
    mybench::DeviceHub::instance()->select(u"sim:0"_s);
    ScopeController c;
    c.initialize();
    c.setTimePerDiv(1e-3);
    c.run();
    QTRY_VERIFY_WITH_TIMEOUT(c.hasRecord(), 5000);
    QVERIFY(c.statusText().isEmpty());   // Immediate: "Running"
    c.setTriggerType(ScopeController::Edge);
    QTRY_COMPARE_WITH_TIMEOUT(c.statusText(), u"Triggered"_s, 5000);
    c.setTriggerTimeout(0.25);
    c.setTriggerLevel(3.0);   // above the 1 V test signal
    QTRY_COMPARE_WITH_TIMEOUT(c.statusText(), u"Waiting for trigger"_s, 5000);
    c.setNoTriggerMode(ScopeController::ShowUntriggered);
    QTRY_COMPARE_WITH_TIMEOUT(c.statusText(), u"Timeout — free running"_s, 5000);
    c.setTriggerLevel(0.0);
    QTRY_COMPARE_WITH_TIMEOUT(c.statusText(), u"Triggered"_s, 5000);
    c.setTriggerType(ScopeController::Immediate);
    QTRY_VERIFY_WITH_TIMEOUT(c.statusText().isEmpty(), 5000);
    c.stop();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle(), 2000);
    QVERIFY(c.statusText().isEmpty());
}

// The ELVISmx "Log" TXT layout is a registered table writer of the scope (Export Data… "*.txt").
void TestScope::textLogWriter()
{
    registerScope(*mybench::InstrumentRegistry::instance());
    QVERIFY(mybench::DataExport::instance()->nameFiltersFor(u"scope"_s).first().contains(u"*.txt"_s));
    mybench::DeviceHub::instance()->select(u"sim:0"_s);
    ScopeController c;
    c.initialize();
    c.setChannelEnabled(1, true);
    c.runOnce();
    QTRY_VERIFY_WITH_TIMEOUT(c.isIdle() && c.hasRecord(), 5000);
    QTemporaryDir dir;
    const QString txt = dir.filePath(u"scope.txt"_s);
    QVERIFY(c.exportData(QUrl::fromLocalFile(txt)));
    QFile log(txt);
    QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines = QString::fromUtf8(log.readAll()).split(u'\n');
    QCOMPARE(lines.value(0), u"waveform\tCh0\t\tCh1\t"_s);
    QVERIFY(lines.value(1).startsWith(u"t0\t"_s));
    QCOMPARE(lines.value(2), u"delta t\t2.000000E-5\t\t2.000000E-5\t"_s);   // 50 kS/s at 5 ms/div
    QVERIFY(lines.value(3).isEmpty());
    QCOMPARE(lines.value(4), u"time[0]\tCh0\ttime[1]\tCh1"_s);
    QCOMPARE(lines.size(), 5 + c.recordLength() + 1);   // + the empty string after the last newline
    const QStringList first = lines.value(5).split(u'\t');
    QCOMPARE(first.size(), 4);
    QVERIFY(first[1].contains(u'E'));
    // The generic tab-separated writer keeps "*.tsv".
    const QString tsv = dir.filePath(u"scope.tsv"_s);
    QVERIFY(c.exportData(QUrl::fromLocalFile(tsv)));
    QFile table(tsv);
    QVERIFY(table.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(table.readAll()).contains(u"Time (s)\tCH 0 (V)\tCH 1 (V)"_s));
}

// Audit scope #7: Trigger Type Edge selects Chan 0 Source again.
void TestScope::triggerTypeEdgeResetsSource()
{
    ScopeController c;
    c.initialize();
    c.setChannelEnabled(1, true);
    c.setTriggerType(ScopeController::Edge);
    c.setTriggerSource(ScopeController::Chan1Source);
    QCOMPARE(c.triggerSource(), ScopeController::Chan1Source);
    c.setTriggerType(ScopeController::Immediate);
    c.setTriggerType(ScopeController::Edge);
    QCOMPARE(c.triggerSource(), ScopeController::Chan0Source);
}

// Audit scope #11: wheel over the graph steps Time/Div, Shift + wheel the nearest channel's Volts/Div.
void TestScope::wheelSteps()
{
    ScopeController c;
    c.initialize();
    QCOMPARE(c.timePerDiv(), 5e-3);
    c.stepTimeDiv(-1);
    QCOMPARE(c.timePerDiv(), 2e-3);
    c.stepTimeDiv(2);
    QCOMPARE(c.timePerDiv(), 1e-2);
    c.stepTimeDiv(-100);
    QCOMPARE(c.timePerDiv(), 5e-6);   // clamped to the list
    c.setChannelEnabled(1, true);
    c.channel0()->setPosition(-2);
    c.channel1()->setPosition(3);
    QCOMPARE(c.channelNearestTo(-1.5), 0);
    QCOMPARE(c.channelNearestTo(2.0), 1);
    c.stepChannelScale(1, -1);
    QCOMPARE(c.channel1()->voltsPerDiv(), 0.5);
    c.stepChannelScale(0, 1);
    QCOMPARE(c.channel0()->voltsPerDiv(), 2.0);
    c.setChannelEnabled(1, false);
    QCOMPARE(c.channelNearestTo(3.0), 0);
}

QTEST_MAIN(TestScope)
#include "tst_scope.moc"
