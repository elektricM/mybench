#include "mydaq/dsp/measure.h"

#include "mydaq/dsp/waveform.h"

#include <QTest>

#include <cmath>
#include <numbers>
#include <random>

using namespace mydaq::dsp;

namespace {

constexpr double kPi = std::numbers::pi;

std::vector<double> addNoise(std::vector<double> x, double sigma, unsigned seed = 1)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, sigma);
    for (double& v : x)
        v += g(rng);
    return x;
}

std::vector<double> tone(double f, double amplitude, double phase, double offset, double fs, size_t n)
{
    return generate({Waveform::Sine, f, amplitude, offset, phase, 0.5}, fs, n);
}

double relativeError(double value, double expected) { return std::abs(value - expected) / std::abs(expected); }

double wrapDegrees(double deg) { return std::remainder(deg, 360.0); }

}  // namespace

class TestMeasure : public QObject {
    Q_OBJECT

private slots:
    void levels()
    {
        const std::vector<double> x{1.0, -1.0, 3.0, 1.0};
        QCOMPARE(mean(x), 1.0);
        QCOMPARE(rms(x), std::sqrt(3.0));
        QCOMPARE(acRms(x), std::sqrt(2.0));
        QCOMPARE(minimum(x), -1.0);
        QCOMPARE(maximum(x), 3.0);
        QCOMPARE(peakToPeak(x), 4.0);
        QVERIFY(std::isnan(mean({})));
        QVERIFY(std::isnan(rms({})));
        QVERIFY(std::isnan(peakToPeak({})));
        QVERIFY(std::isnan(frequency({}, 1000.0)));
    }

    void sineLevels()
    {
        const std::vector<double> x = tone(50.0, 2.0, 0.0, 0.5, 10000.0, 10000);
        QVERIFY(std::abs(acRms(x) - 2.0 / std::numbers::sqrt2) < 1e-9);
        QVERIFY(std::abs(rms(x) - std::sqrt(2.0 + 0.25)) < 1e-9);
        QVERIFY(std::abs(peakToPeak(x) - 4.0) < 1e-3);
        QVERIFY(std::abs(mean(x) - 0.5) < 1e-9);
    }

    void frequencyOfNoisySine()
    {
        const double fs = 100e3;
        const double f = 1234.5;
        const std::vector<double> x = addNoise(tone(f, 1.0, 0.3, 0.2, fs, 10000), 0.01);
        QVERIFY2(relativeError(frequency(x, fs), f) < 5e-4, qPrintable(QString::number(frequency(x, fs))));
        QVERIFY(relativeError(period(x, fs), 1.0 / f) < 5e-4);
        QVERIFY(std::abs(dutyCycle(x) - 0.5) < 0.01);
    }

    void frequencyAndDutyOfNoisySquare()
    {
        const double fs = 200e3;
        const double f = 777.0;
        const std::vector<double> x =
            addNoise(generate({Waveform::Square, f, 0.5, 1.0, 0.0, 0.3}, fs, 20000), 0.02, 2);
        QVERIFY2(relativeError(frequency(x, fs), f) < 1e-3, qPrintable(QString::number(frequency(x, fs))));
        QVERIFY2(std::abs(dutyCycle(x) - 0.3) < 0.01, qPrintable(QString::number(dutyCycle(x))));
        QVERIFY(std::abs(peakToPeak(x) - 1.0) < 0.2);
    }

    void noCompletePeriod()
    {
        const std::vector<double> x = tone(1.0, 1.0, 0.0, 0.0, 1000.0, 300);   // 0.3 periods
        QVERIFY(std::isnan(frequency(x, 1000.0)));
        QVERIFY(std::isnan(dutyCycle(std::vector<double>(100, 1.0))));
    }

    void extractSingleToneClean()
    {
        const double fs = 48000.0;
        const std::vector<double> x = tone(1234.567, 0.8, 0.7, 0.1, fs, 4096);
        const Tone t = extractSingleTone(x, fs);
        QVERIFY2(relativeError(t.frequency, 1234.567) < 1e-8, qPrintable(QString::number(t.frequency, 'g', 12)));
        QVERIFY(relativeError(t.amplitude, 0.8) < 1e-8);
        QVERIFY(std::abs(t.phase - 0.7) < 1e-6);
        QVERIFY(std::abs(t.offset - 0.1) < 1e-8);
    }

    void extractSingleToneNoisy()
    {
        const double fs = 100e3;
        const std::vector<double> x = addNoise(tone(9876.5, 1.0, -2.0, 0.0, fs, 10000), 1e-3, 7);
        const Tone t = extractSingleTone(x, fs);
        QVERIFY(relativeError(t.frequency, 9876.5) < 1e-6);
        QVERIFY(relativeError(t.amplitude, 1.0) < 1e-3);
        QVERIFY(std::abs(t.phase + 2.0) < 1e-3);
    }

    void fitSineAtKnownFrequency()
    {
        const double fs = 10000.0;
        // 12.3 periods: no whole number of cycles, the 3-parameter fit is still exact.
        const std::vector<double> x = tone(100.0, 1.25, 2.5, -0.3, fs, 1230);
        const Tone t = fitSine(x, fs, 100.0);
        QVERIFY(std::abs(t.amplitude - 1.25) < 1e-10);
        QVERIFY(std::abs(t.phase - 2.5) < 1e-10);
        QVERIFY(std::abs(t.offset + 0.3) < 1e-10);
    }

    // Bode-style gain/phase between two channels.
    void gainAndPhaseDifference()
    {
        const double fs = 200e3;
        const double f = 1000.0;
        const std::vector<double> stimulus = addNoise(tone(f, 2.0, 0.4, 0.0, fs, 20000), 5e-4, 1);
        const std::vector<double> response = addNoise(tone(f, 2.0 / std::numbers::sqrt2, 0.4 - kPi / 4, 0.0, fs, 20000), 5e-4, 2);
        const Tone a = fitSine(stimulus, fs, f);
        const Tone b = fitSine(response, fs, f);
        QVERIFY(std::abs(20.0 * std::log10(b.amplitude / a.amplitude) + 3.0103) < 0.01);
        QVERIFY(std::abs(wrapDegrees((b.phase - a.phase) * 180.0 / kPi) + 45.0) < 0.05);
    }

    void toneAtAndSingleBin()
    {
        const double fs = 1000.0;
        const std::vector<double> x = tone(50.0, 1.0, 1.0, 0.0, fs, 1000);   // 50 whole cycles
        const Tone t = toneAt(x, fs, 50.0);
        QVERIFY(std::abs(t.amplitude - 1.0) < 1e-9);
        QVERIFY(std::abs(t.phase - 1.0) < 1e-9);
        const Tone flat = toneAt(x, fs, 50.0, WindowType::FlatTop);
        QVERIFY(std::abs(flat.amplitude - 1.0) < 1e-9);
        // Single-bin DFT equals the FFT bin at bin centers.
        const std::vector<Complex> bins = rfft(x);
        QVERIFY(std::abs(singleBinDft(x, 50.0 / 1000.0) - bins[50]) < 1e-9);
        QVERIFY(std::abs(singleBinDft(x, 51.0 / 1000.0) - bins[51]) < 1e-9);
    }

    void thdOfKnownHarmonics()
    {
        const double fs = 100e3;
        const double f = 1037.3;
        std::vector<double> x = tone(f, 1.0, 0.0, 0.1, fs, 16384);
        const std::vector<double> h2 = tone(2 * f, 0.01, 0.5, 0.0, fs, 16384);
        const std::vector<double> h3 = tone(3 * f, 0.005, 1.0, 0.0, fs, 16384);
        for (size_t k = 0; k < x.size(); ++k)
            x[k] += h2[k] + h3[k];
        const HarmonicAnalysis h = analyzeHarmonics(x, fs);
        QVERIFY(relativeError(h.fundamentalFrequency, f) < 1e-4);
        QVERIFY(relativeError(h.fundamentalPower, 0.5) < 1e-3);
        QVERIFY2(std::abs(h.thd * 100.0 - std::sqrt(1.0 + 0.25)) < 0.005, qPrintable(QString::number(h.thd * 100.0)));
        QVERIFY(std::abs(h.harmonicLevelDb(2) + 40.0) < 0.05);
        QVERIFY(std::abs(h.harmonicLevelDb(3) + 46.0206) < 0.05);
        QVERIFY(h.harmonicLevelDb(4) < -150.0);
        QVERIFY(std::abs(h.thdPlusNoise - h.thd) < 1e-4);
        QVERIFY(h.harmonicFrequency.size() >= 40);   // every harmonic below Nyquist
        const HarmonicAnalysis limited = analyzeHarmonics(powerSpectrum(x, fs, WindowType::BlackmanHarris7), 3);
        QCOMPARE(limited.harmonicPower.size(), size_t(3));
    }

    void noiseMeasurements()
    {
        const double fs = 100e3;
        const double sigma = 1e-3;
        const std::vector<double> x = addNoise(tone(2000.3, 1.0, 0.0, 0.0, fs, 32768), sigma, 11);
        const HarmonicAnalysis h = analyzeHarmonics(x, fs);   // 7-term B-Harris: no leakage into the noise
        const double snr = 10.0 * std::log10(0.5 / (sigma * sigma));
        QVERIFY2(std::abs(h.snr - snr) < 0.5, qPrintable(QString::number(h.snr)));
        QVERIFY(std::abs(h.sinad - snr) < 0.5);
        QVERIFY(std::abs(h.noisePower / (sigma * sigma) - 1.0) < 0.1);
    }
};

QTEST_GUILESS_MAIN(TestMeasure)
#include "tst_measure.moc"
