// Audio Equalizer: Bessel design (prototype, lowpass, bandpass), equalizer processing against its
// frequency response, octave-band analysis, meters, and the controller streaming the simulator's
// inputs to its outputs.
#include "aeqcontroller.h"
#include "equalizer.h"

#include <mybench/devicehub.h>

#include <mydaq/dsp/fft.h>
#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/window.h>

#include <QStandardPaths>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace mybench;
using namespace mybench::instruments;
using namespace Qt::StringLiterals;

namespace {

double db(std::complex<double> h) { return 20.0 * std::log10(std::abs(h)); }

// Steady-state amplitude of a sine through the equalizer (last 20 ms of 200 ms).
double sineGain(aeq::Equalizer& eq, double frequency, const aeq::EqSettings& s)
{
    const double fs = eq.sampleRate();
    const size_t n = size_t(fs * 0.2);
    std::vector<double> x(n), y(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = std::sin(2 * std::numbers::pi * frequency * double(i) / fs);
    eq.process(x, y, s);
    eq.process(x, y, s);   // settled, constant settings
    double peak = 0;
    for (size_t i = n - size_t(fs * 0.02); i < n; ++i)
        peak = std::max(peak, std::abs(y[i]));
    return peak;
}

}  // namespace

class TstAeq : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        InstrumentController::setRestoreSavedSettings(false);
        DeviceHub::instance()->select(u"sim:0"_s);
        QVERIFY(DeviceHub::instance()->isConnected());
    }

    void prototype()
    {
        const auto poles = aeq::besselPrototypePoles(4);
        QCOMPARE(int(poles.size()), 4);
        std::complex<double> denominatorAtJ1 = 1;
        std::complex<double> denominatorAt0 = 1;
        for (const auto p : poles) {
            QVERIFY(p.real() < 0);
            denominatorAtJ1 *= std::complex<double>(0, 1) - p;
            denominatorAt0 *= -p;
        }
        // H(s) = prod(-p) / prod(s - p): -3 dB at 1 rad/s.
        QVERIFY(std::abs(std::abs(denominatorAt0 / denominatorAtJ1) - std::sqrt(0.5)) < 1e-9);
        // Known 4th-order Bessel pole (magnitude-normalized): -0.9952 ± 1.2571j·... check sum.
        double realSum = 0;
        for (const auto p : poles)
            realSum += p.real();
        QVERIFY(std::abs(realSum - (-10.0 / 2.1139176749)) < 1e-4);
    }

    void lowpass()
    {
        const auto lp = aeq::besselLowpass(4, 320, 50000);
        QCOMPARE(int(lp.sections.size()), 2);
        QVERIFY(lp.maxPoleRadius() < 1.0);
        QVERIFY(std::abs(db(lp.response(1e-3, 50000))) < 1e-6);
        QVERIFY(std::abs(db(lp.response(320, 50000)) + 3.0103) < 0.01);
        QVERIFY(db(lp.response(5000, 50000)) < -60);
    }

    void bandpass_data()
    {
        QTest::addColumn<double>("low");
        QTest::addColumn<double>("high");
        QTest::addColumn<double>("rate");
        QTest::newRow("mid 50k") << 320.0 << 2560.0 << 50000.0;
        QTest::newRow("treble 50k") << 2560.0 << 20840.0 << 50000.0;
        QTest::newRow("treble 200k") << 2560.0 << 20840.0 << 200000.0;
    }
    void bandpass()
    {
        QFETCH(double, low);
        QFETCH(double, high);
        QFETCH(double, rate);
        const auto bp = aeq::besselBandpass(4, low, high, rate);
        QCOMPARE(int(bp.sections.size()), 4);
        QVERIFY(bp.maxPoleRadius() < 1.0);
        // Unity at the (warped) geometric center, -3 dB at both edges.
        const double w1 = 2 * rate * std::tan(std::numbers::pi * low / rate);
        const double w2 = 2 * rate * std::tan(std::numbers::pi * high / rate);
        const double center = rate / std::numbers::pi * std::atan(std::sqrt(w1 * w2) / (2 * rate));
        QVERIFY(std::abs(db(bp.response(center, rate))) < 1e-6);
        QVERIFY(std::abs(db(bp.response(low, rate)) + 3.0103) < 0.02);
        QVERIFY(std::abs(db(bp.response(high, rate)) + 3.0103) < 0.02);
        QVERIFY(db(bp.response(low / 8, rate)) < -30);
    }

    void equalizerMatchesResponse()
    {
        aeq::Equalizer eq(50000);
        aeq::EqSettings s;
        s.gainsDb = {6, -6, 3};
        s.volume = 0.8;
        for (double f : {100.0, 1000.0, 5000.0}) {
            eq.reset();
            const double measured = sineGain(eq, f, s);
            const double expected = std::abs(eq.response(f, s));
            QVERIFY2(std::abs(measured - expected) < 0.01 * expected + 1e-4,
                     qPrintable(u"%1 Hz: %2 vs %3"_s.arg(f).arg(measured).arg(expected)));
        }
        // Bass boost raises the low end relative to the mids.
        QVERIFY(db(eq.response(60, s)) - db(eq.response(1000, s)) > 6);
        // Bypass: volume x input.
        s.bypass = true;
        eq.reset();
        QVERIFY(std::abs(sineGain(eq, 1000, s) - 0.8) < 0.005);   // sampled peak of the sine
    }

    void gainChangesAreRamped()
    {
        aeq::Equalizer eq(50000);
        aeq::EqSettings s;
        s.gainsDb = {0, 0, 0};
        std::vector<double> x(500, 0.5), y(500);
        eq.process(x, y, s);
        eq.process(x, y, s);
        const double before = y.back();
        s.volume = 1.0;   // 0.5 -> 1.0: doubles the output, spread over the block
        eq.process(x, y, s);
        double largestStep = 0;
        for (size_t i = 1; i < y.size(); ++i)
            largestStep = std::max(largestStep, std::abs(y[i] - y[i - 1]));
        QVERIFY(largestStep < 0.01);
        QVERIFY(std::abs(y.back() - 2 * before) < 0.02);
    }

    void octaveAnalysis()
    {
        const aeq::OctaveBands bands = aeq::octaveBands(24, 25, 25000);
        QVERIFY(std::abs(double(bands.centers.size()) - 24 * std::log2(1000.0)) <= 2);
        QVERIFY(std::find_if(bands.centers.begin(), bands.centers.end(), [](double c) { return std::abs(c - 1000) < 1e-9; })
                != bands.centers.end());
        // Power is conserved when bands tile the spectrum.
        std::vector<double> power(2001, 0.0);
        power[100] = 1.0;   // 100 Hz with df = 1 Hz
        power[1500] = 0.5;
        const std::vector<double> p = aeq::bandPowers(bands, power, 1.0);
        double total = 0;
        for (double v : p)
            total += v;
        QVERIFY(std::abs(total - 1.5) < 1e-9);
    }

    void analysisWindow()
    {
        // History: nothing before 0.3 s, then the largest power of two up to the 1 s window.
        QCOMPARE(aeq::analysisLength(10000, 50000), size_t(0));
        QCOMPARE(aeq::analysisLength(16384, 50000), size_t(16384));
        QCOMPARE(aeq::analysisLength(40000, 50000), size_t(32768));
        QCOMPARE(aeq::analysisLength(65536, 50000), size_t(65536));
        QCOMPARE(aeq::maximumAnalysisLength(50000), size_t(65536));
        QCOMPARE(aeq::maximumAnalysisLength(200000), size_t(65536));   // capped: 262144 points made the 200 kS/s stream drop out
        // A 50 Hz tone (0.5 V^2 rms) lands in a single 1/24-octave band with the long window;
        // the old 0.3 s window smears it over neighbours (bands are 2.9 Hz wide there).
        const double fs = 50000;
        const aeq::OctaveBands bands = aeq::octaveBands(24, 25, 25000);
        auto peakBand = [&](size_t n) {
            std::vector<double> x(n);
            for (size_t i = 0; i < n; ++i)
                x[i] = std::sin(2 * std::numbers::pi * 50.0 * double(i) / fs);
            const mydaq::dsp::PowerSpectrum ps = mydaq::dsp::powerSpectrum(x, fs, mydaq::dsp::WindowType::Hanning);
            const std::vector<double> p = aeq::bandPowers(bands, ps.power, ps.info.df);
            return *std::max_element(p.begin(), p.end()) / ps.info.enbw;
        };
        QVERIFY2(peakBand(65536) > peakBand(16384) * 1.3, qPrintable(u"%1 vs %2"_s.arg(peakBand(65536)).arg(peakBand(16384))));
    }

    void levels()
    {
        std::vector<double> x(1000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 2.0 * std::sin(2 * std::numbers::pi * double(i) / 100.0);
        const aeq::Level level = aeq::measureLevel(x, 2.0);
        QVERIFY(std::abs(level.peakDb) < 0.01);
        QVERIFY(std::abs(level.rmsDb + 3.0103) < 0.01);
        QCOMPARE(aeq::measureLevel(std::vector<double>(10, 0.0), 2.0).peakDb, -120.0);
    }

    void routing()
    {
        AeqController ctl;
        ctl.initialize();
        QVERIFY(ctl.routingError().isEmpty());
        QCOMPARE(ctl.bass(), 1.0);   // ELVISmx saved defaults
        QCOMPARE(ctl.volume(), 0.5);
        QVERIFY(ctl.resources().testFlag(ResourceArbiter::AnalogInput));
        QVERIFY(ctl.resources().testFlag(ResourceArbiter::AnalogOutput));
        QVERIFY(ctl.feedbackInSimulator());
        // AI and audio inputs share the converters: the other side follows to the same kind (as the
        // Scope does), so a mixed selection cannot be made.
        ctl.chooseInput(1, AeqController::SourceAI1);
        QCOMPARE(ctl.inputLeft(), AeqController::SourceAI0);
        QVERIFY(ctl.routingError().isEmpty());
        // Nothing to play: the run is refused with a warning, not a device rejection.
        ctl.setInputLeft(AeqController::SourceNone);
        ctl.setInputRight(AeqController::SourceNone);
        QVERIFY(!ctl.routingError().isEmpty());
        ctl.run();
        QVERIFY(ctl.isIdle());
        QVERIFY(!ctl.hasError());
        QVERIFY(!ctl.inputWarning().isEmpty());
        ctl.setInputLeft(AeqController::SourceNone);
        ctl.setInputRight(AeqController::SourceAI1);
        QVERIFY(ctl.routingError().isEmpty());
        ctl.setOutputRight(AeqController::SinkAudioOutLeft);
        QVERIFY(!ctl.routingError().isEmpty());   // same output twice
    }

    void streams()
    {
        AeqController ctl;
        ctl.initialize();
        ctl.useAnalogInputs();   // AI 0: 1 kHz sine 1 V; AI 1: 200 Hz square 0.5 V
        QVERIFY(!ctl.feedbackInSimulator());
        QCOMPARE(ctl.timeSpanIndex(), 1);   // ELVISmx Time graph: 100 ms
        QCOMPARE(ctl.timeSpan(), 0.1);
        QVERIFY(ctl.autoScaleTime() && ctl.autoScaleSpectrum());
        ctl.flat();
        ctl.setVolume(1.0);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isRunning() && ctl.spectrumOut()->count() > 0, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ctl.meters().size() == 4 && ctl.meters()[2].toMap()[u"peak"_s].toDouble() > -12, 3000);
        const QVariantMap inL = ctl.meters()[0].toMap();
        const QVariantMap outL = ctl.meters()[2].toMap();
        // Flat bands at 1 kHz: output level follows |H(1 kHz)| (Bessel bands do not sum to exactly 1).
        aeq::Equalizer eq(ctl.streamRate());
        aeq::EqSettings flat;
        flat.gainsDb = {0, 0, 0};
        flat.volume = 1.0;
        const double expected = inL[u"peak"_s].toDouble() + db(eq.response(1000, flat));
        QVERIFY2(std::abs(outL[u"peak"_s].toDouble() - expected) < 1.0,
                 qPrintable(u"out %1 dBFS, expected %2"_s.arg(outL[u"peak"_s].toDouble()).arg(expected)));
        // Live: bypass at volume 0.25 = -12 dB relative to the input.
        ctl.setBypass(true);
        ctl.setVolume(0.25);
        QTest::qWait(400);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(ctl.meters()[2].toMap()[u"peak"_s].toDouble()
                                          - (ctl.meters()[0].toMap()[u"peak"_s].toDouble() - 12.04)) < 0.5, 3000);
        QCOMPARE(ctl.dropouts(), 0);
        QVERIFY(ctl.isRunning());
        // Time view: 100 ms = 5000 samples at 50 kS/s; 20 ms (triggered) = 1000.
        QTRY_COMPARE_WITH_TIMEOUT(ctl.timeIn()->count(), qsizetype(std::llround(ctl.streamRate() * 0.1)), 3000);
        ctl.setTimeSpanIndex(0);
        QTRY_COMPARE_WITH_TIMEOUT(ctl.timeIn()->count(), qsizetype(std::llround(ctl.streamRate() * 0.02)), 3000);
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
    }

    void highRateKeepsUp()
    {
        // 200 kS/s with the default buffer: the spectrum window (capped at 65536 points) must not
        // make the streaming loop miss its deadlines.
        AeqController ctl;
        ctl.initialize();
        ctl.useAnalogInputs();
        ctl.setRateIndex(2);      // 200 kS/s
        ctl.setLatencyIndex(2);   // 250 ms (default)
        QCOMPARE(ctl.streamRate(), 200000.0);
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isRunning() && ctl.spectrumOut()->count() > 0, 5000);
        QTest::qWait(2500);   // the window grows to its full 1 s meanwhile
        QCOMPARE(ctl.dropouts(), 0);
        QVERIFY(ctl.isRunning());
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
    }
};

QTEST_MAIN(TstAeq)
#include "tst_aeq.moc"
