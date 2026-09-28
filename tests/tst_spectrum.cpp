#include "mydaq/dsp/spectrum.h"

#include "mydaq/dsp/waveform.h"

#include <QTest>

#include <cmath>
#include <numbers>
#include <random>

using namespace mydaq::dsp;

Q_DECLARE_METATYPE(mydaq::dsp::WindowType)

namespace {

std::vector<double> sine(double amplitude, double cyclesPerRecord, size_t n, double offset = 0.0, double phase = 0.0)
{
    std::vector<double> x(n);
    for (size_t k = 0; k < n; ++k)
        x[k] = offset + amplitude * std::sin(2.0 * std::numbers::pi * cyclesPerRecord * double(k) / double(n) + phase);
    return x;
}

double db(double ratio) { return 10.0 * std::log10(ratio); }

void addWindows()
{
    QTest::addColumn<WindowType>("window");
    for (WindowType w : windowTypes())
        QTest::newRow(qPrintable(windowName(w))) << w;
}

}  // namespace

class TestSpectrum : public QObject {
    Q_OBJECT

private slots:
    // A tone on a bin reads its exact RMS amplitude with every window.
    void coherentToneAmplitude_data() { addWindows(); }
    void coherentToneAmplitude()
    {
        QFETCH(WindowType, window);
        const size_t n = 1024;
        const double fs = 1024.0;
        const double a = 1.5;
        const PowerSpectrum s = powerSpectrum(sine(a, 100.0, n, 0.25, 0.3), fs, window);
        QCOMPARE(s.power.size(), n / 2 + 1);
        QCOMPARE(s.info.df, 1.0);
        QVERIFY(std::abs(s.power[100] - a * a / 2.0) < 1e-9);
        QVERIFY(std::abs(s.frequency(100) - 100.0) < 1e-12);
        // DC bin reads the mean when the tone's leakage does not reach it.
        QVERIFY(std::abs(std::sqrt(s.power[0]) - 0.25) < 1e-9);
        // Integrated tone power (sum of the lobe / ENBW) is the tone power.
        const PeakEstimate p = estimatePeak(s, 100, mainLobeHalfWidth(window));
        QVERIFY(std::abs(p.power - a * a / 2.0) < 1e-9);
        QVERIFY(std::abs(p.frequency - 100.0) < 1e-9);
    }

    // Between bins: flat top keeps the peak accurate; lobe integration works for every window.
    void offBinTone_data() { addWindows(); }
    void offBinTone()
    {
        QFETCH(WindowType, window);
        const size_t n = 4096;
        const double fs = 40960.0;
        const double a = 0.8;
        const PowerSpectrum s = powerSpectrum(sine(a, 400.37, n), fs, window);
        const PeakEstimate p = findPeak(s, mainLobeHalfWidth(window) + 1);
        QCOMPARE(p.bin, size_t(400));
        if (window != WindowType::None) {
            QVERIFY2(std::abs(db(p.power / (a * a / 2.0))) < 0.05, qPrintable(QString::number(db(p.power / (a * a / 2.0)))));
            QVERIFY(std::abs(p.frequency - 4003.7) < 2.0);
        }
        if (window == WindowType::FlatTop)
            QVERIFY(std::abs(db(s.power[400] / (a * a / 2.0))) < 0.02);
    }

    void displayUnits()
    {
        SpectrumInfo info;
        info.length = 1000;
        info.df = 10.0;
        info.enbw = 1.5;
        const double power = 1.0;   // 1 Vrms
        SpectrumFormat f;
        f.unit = SpectrumUnit::Db;
        QVERIFY(std::abs(formatPower(power, f, info)) < 1e-12);
        f.mode = SpectrumMode::Peak;
        QVERIFY(std::abs(formatPower(power, f, info) - 3.0103) < 1e-4);
        QVERIFY(std::abs(formatPower(power, f, info, false)) < 1e-12);   // DC is not doubled
        f.mode = SpectrumMode::Rms;
        f.unit = SpectrumUnit::Dbm;
        QVERIFY(std::abs(formatPower(power, f, info) - 13.0103) < 1e-4);
        f.unit = SpectrumUnit::Linear;
        QVERIFY(std::abs(formatPower(0.25, f, info) - 0.5) < 1e-12);
        f.density = true;
        QVERIFY(std::abs(formatPower(0.25, f, info) - std::sqrt(0.25 / 15.0)) < 1e-12);
        QCOMPARE(formatPower(0.0, {SpectrumMode::Rms, SpectrumUnit::Db, false, 50.0}, info), -400.0);

        QCOMPARE(spectrumUnitLabel({SpectrumMode::Rms, SpectrumUnit::Linear, false, 50.0}), QStringLiteral("Vrms"));
        QCOMPARE(spectrumUnitLabel({SpectrumMode::Peak, SpectrumUnit::Linear, false, 50.0}), QStringLiteral("Vpk"));
        QCOMPARE(spectrumUnitLabel({SpectrumMode::Rms, SpectrumUnit::Db, false, 50.0}), QStringLiteral("dBV"));
        QCOMPARE(spectrumUnitLabel({SpectrumMode::Rms, SpectrumUnit::Dbm, true, 50.0}), QStringLiteral("dBm/Hz"));
    }

    // White noise: the PSD equals sigma^2 / (fs/2) whatever the window.
    void noiseDensity()
    {
        const size_t n = 8192;
        const double fs = 10000.0;
        const double sigma = 0.1;
        std::mt19937 rng(3);
        std::normal_distribution<double> g(0.0, sigma);
        std::vector<double> x(n);
        for (double& v : x)
            v = g(rng);
        const PowerSpectrum s = powerSpectrum(x, fs, WindowType::Hanning);
        const std::vector<double> psd = formatSpectrum(s, {SpectrumMode::Rms, SpectrumUnit::Linear, true, 50.0});
        double mean = 0.0;
        for (size_t k = 10; k < psd.size() - 10; ++k)
            mean += psd[k] * psd[k];
        mean /= double(psd.size() - 20);
        QVERIFY(std::abs(mean / (sigma * sigma / (fs / 2.0)) - 1.0) < 0.1);
        const std::vector<double> f = s.frequencies();
        QCOMPARE(f.size(), n / 2 + 1);
        QCOMPARE(f.back(), fs / 2.0);
    }

    void averagerNone()
    {
        SpectrumAverager avg(AveragingMode::None);
        avg.add(complexSpectrum(sine(1.0, 10, 256), 256.0, WindowType::Hanning));
        avg.add(complexSpectrum(sine(2.0, 10, 256), 256.0, WindowType::Hanning));
        QVERIFY(std::abs(avg.result().power[10] - 2.0) < 1e-9);
        QCOMPARE(avg.count(), 1);
        QVERIFY(avg.isComplete());
    }

    void averagerRmsLinear()
    {
        SpectrumAverager avg(AveragingMode::Rms, AveragingWeighting::Linear, 4);
        QVERIFY(avg.result().power.empty());
        for (int i = 1; i <= 4; ++i) {
            avg.add(complexSpectrum(sine(double(i), 10, 256), 256.0, WindowType::None));
            QCOMPARE(avg.isComplete(), i == 4);
        }
        // Mean power of amplitudes 1..4: (1 + 4 + 9 + 16) / 4 * 0.5.
        QVERIFY(std::abs(avg.result().power[10] - 7.5 * 0.5) < 1e-9);
        avg.add(complexSpectrum(sine(100.0, 10, 256), 256.0, WindowType::None));   // held
        QVERIFY(std::abs(avg.result().power[10] - 7.5 * 0.5) < 1e-9);
        QCOMPARE(avg.count(), 4);
        avg.restart();
        QCOMPARE(avg.count(), 0);
        avg.add(complexSpectrum(sine(2.0, 10, 256), 256.0, WindowType::None));
        QVERIFY(std::abs(avg.result().power[10] - 2.0) < 1e-9);
    }

    void averagerRmsExponential()
    {
        SpectrumAverager avg(AveragingMode::Rms, AveragingWeighting::Exponential, 2);
        const double p[] = {1.0, 3.0, 5.0};   // powers of the records (rms^2)
        double expected = 0.0;
        for (int i = 0; i < 3; ++i) {
            avg.add(complexSpectrum(sine(std::sqrt(2.0 * p[i]), 10, 256), 256.0, WindowType::None));
            expected += (p[i] - expected) / std::min(i + 1, 2);
            QVERIFY(std::abs(avg.result().power[10] - expected) < 1e-9);
        }
        QCOMPARE(avg.count(), 2);
        QVERIFY(avg.isComplete());
    }

    // Vector averaging cancels records of opposite phase; RMS averaging does not.
    void averagerVector()
    {
        SpectrumAverager vector(AveragingMode::Vector, AveragingWeighting::Linear, 2);
        SpectrumAverager power(AveragingMode::Rms, AveragingWeighting::Linear, 2);
        for (double phase : {0.0, std::numbers::pi}) {
            const ComplexSpectrum s = complexSpectrum(sine(1.0, 10, 256, 0.0, phase), 256.0, WindowType::Hanning);
            vector.add(s);
            power.add(s);
        }
        QVERIFY(vector.result().power[10] < 1e-20);
        QVERIFY(std::abs(power.result().power[10] - 0.5) < 1e-9);
    }

    void averagerPeakHold()
    {
        SpectrumAverager avg(AveragingMode::PeakHold);
        for (double a : {1.0, 3.0, 2.0})
            avg.add(complexSpectrum(sine(a, 10, 256), 256.0, WindowType::None));
        QVERIFY(std::abs(avg.result().power[10] - 4.5) < 1e-9);
        QCOMPARE(avg.count(), 3);
        QVERIFY(!avg.isComplete());
    }

    void averagerRestartsOnSizeChange()
    {
        SpectrumAverager avg(AveragingMode::Rms, AveragingWeighting::Linear, 10);
        avg.add(complexSpectrum(sine(1.0, 10, 256), 256.0, WindowType::None));
        avg.add(complexSpectrum(sine(1.0, 10, 256), 256.0, WindowType::None));
        QCOMPARE(avg.count(), 2);
        avg.add(complexSpectrum(sine(1.0, 10, 512), 512.0, WindowType::None));
        QCOMPARE(avg.count(), 1);
        QCOMPARE(avg.result().power.size(), size_t(257));
        avg.configure(AveragingMode::PeakHold, AveragingWeighting::Linear, 3);
        QCOMPARE(avg.count(), 0);
        QCOMPARE(avg.mode(), AveragingMode::PeakHold);
    }
};

QTEST_GUILESS_MAIN(TestSpectrum)
#include "tst_spectrum.moc"
