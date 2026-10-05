// Spectrum Analyzer tests: display conversions, measurements, peak search, the third-octave
// filter bank, and the controller against the simulated myDAQ (AI 0: 1 kHz, 1 V peak, 1 % third
// harmonic; AudioInput Left: 440 Hz, 0.5 V peak).
#include "dsaanalysis.h"
#include "dsacontroller.h"
#include "dsaformat.h"
#include "octaveanalysis.h"

#include <mybench/framework.h>

#include <mydaq/dsp/waveform.h>

#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace mybench;
using namespace mybench::instruments;
using namespace Qt::StringLiterals;
namespace dsp = mydaq::dsp;

namespace {

std::vector<double> sine(double frequency, double amplitude, double rate, size_t n, double third = 0)
{
    std::vector<double> x(n);
    for (size_t k = 0; k < n; ++k) {
        const double t = double(k) / rate;
        x[k] = amplitude * (std::sin(2 * std::numbers::pi * frequency * t)
                            + third * std::sin(2 * std::numbers::pi * 3 * frequency * t));
    }
    return x;
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

}  // namespace

class TestDsa : public QObject
{
    Q_OBJECT

    DsaController* dsa() const
    {
        return qobject_cast<DsaController*>(InstrumentRegistry::instance()->controller(u"dsa"_s));
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_dsa"_s);
        InstrumentController::setRestoreSavedSettings(false);
        QVERIFY(DeviceHub::instance()->isSimulated());
        registerDsa(*InstrumentRegistry::instance());
        InstrumentRegistry::instance()->createAll();
        QVERIFY(dsa());
    }

    void cleanupTestCase() { InstrumentRegistry::instance()->shutdown(); }

    // --- dsaformat ----------------------------------------------------------------------------

    void unitLabels()
    {
        dsp::SpectrumFormat f;
        QCOMPARE(dsa::magnitudeUnit(f), u"dBVrms"_s);
        f.mode = dsp::SpectrumMode::Peak;
        QCOMPARE(dsa::magnitudeUnit(f), u"dBVpk"_s);
        f.density = true;
        QCOMPARE(dsa::magnitudeUnit(f), u"dBVpk/√Hz"_s);
        f.unit = dsp::SpectrumUnit::Dbm;
        QCOMPARE(dsa::magnitudeUnit(f), u"dBm/Hz"_s);
        f.unit = dsp::SpectrumUnit::Linear;
        f.mode = dsp::SpectrumMode::Rms;
        QCOMPARE(dsa::magnitudeUnit(f), u"Vrms^2/Hz"_s);   // Linear is power (ELVISmx)
        f.density = false;
        f.mode = dsp::SpectrumMode::Peak;
        QCOMPARE(dsa::magnitudeUnit(f), u"Vpk^2"_s);
        QCOMPARE(dsa::powerUnit(dsp::SpectrumUnit::Linear, dsp::SpectrumMode::Rms), u"Vrms^2"_s);
        QCOMPARE(dsa::powerUnit(dsp::SpectrumUnit::Db, dsp::SpectrumMode::Peak), u"dBVpk"_s);
    }

    void powerValues()
    {
        // A 1 V peak sine: 0.5 Vrms².
        QVERIFY(near(dsa::powerValue(0.5, dsp::SpectrumUnit::Db, dsp::SpectrumMode::Rms), -3.0103, 1e-3));
        QVERIFY(near(dsa::powerValue(0.5, dsp::SpectrumUnit::Db, dsp::SpectrumMode::Peak), 0.0, 1e-9));
        QVERIFY(near(dsa::powerValue(0.5, dsp::SpectrumUnit::Dbm, dsp::SpectrumMode::Rms), 10.0, 1e-9));
        QCOMPARE(dsa::powerValue(0.5, dsp::SpectrumUnit::Linear, dsp::SpectrumMode::Rms), 0.5);
        QVERIFY(near(dsa::levelValue(0.5, dsp::SpectrumUnit::Linear, dsp::SpectrumMode::Rms), 0.5, 1e-9));   // power, no sqrt
        // Linear spectrum values are V^2 too: a 1 V peak sine reads 0.5 at its line.
        dsp::PowerSpectrum ps;
        ps.info.df = 1.0;
        ps.info.enbw = 1.0;
        ps.power = {0.0, 0.5};
        const std::vector<double> lin = dsa::formatSpectrum(ps, {dsp::SpectrumMode::Peak, dsp::SpectrumUnit::Linear, false, 50.0});
        QVERIFY(near(lin[1], 1.0, 1e-9));
        QCOMPARE(dsa::powerValue(0.0, dsp::SpectrumUnit::Db, dsp::SpectrumMode::Rms), -400.0);
        QCOMPARE(dsa::fixedText(std::nan("")), u"0.00"_s);
        QCOMPARE(dsa::fixedText(-0.001), u"0.00"_s);
        QCOMPARE(dsa::fixedText(-3.0103), u"−3.01"_s);
    }

    void scales()
    {
        QCOMPARE(dsa::defaultManualRange(dsp::SpectrumUnit::Db), (dsa::ValueRange{-120, 10}));
        QCOMPARE(dsa::defaultManualRange(dsp::SpectrumUnit::Linear), (dsa::ValueRange{0, 1}));
        const std::vector<double> db{-3, -50, -400, -130};
        const dsa::ValueRange r = dsa::autoRange(db, dsp::SpectrumUnit::Db);
        QCOMPARE(r.maximum, 0.0);
        QCOMPARE(r.minimum, -140.0);   // -400 floor ignored
        const std::vector<double> deep{0, -300};
        QCOMPARE(dsa::autoRange(deep, dsp::SpectrumUnit::Db).minimum, -150.0);   // top (10) - 160 dB
        const std::vector<double> lin{0.0, 0.7};
        QCOMPARE(dsa::autoRange(lin, dsp::SpectrumUnit::Linear).maximum, 1.0);
    }

    // --- dsaanalysis -----------------------------------------------------------------------------

    void measurements()
    {
        // 1 kHz, 1 V peak, 1 % third harmonic at 80 kS/s, 800 samples (coherent).
        const std::vector<double> x = sine(1000, 1.0, 80000, 800, 0.01);
        const dsp::PowerSpectrum p = dsp::powerSpectrum(x, 80000, dsp::WindowType::None);
        const dsa::Measurements m = dsa::measure(p, x);
        QVERIFY(m.valid);
        QVERIFY(near(m.fundamentalFrequency, 1000, 0.01));
        QVERIFY(near(m.fundamentalPower, 0.5, 1e-6));
        QVERIFY(near(m.thdPercent, 1.0, 0.01));
        QVERIFY(near(m.sinadDb, 40.0, 0.05));
        QVERIFY(near(m.vpk, 1.0, 0.02));   // (max - min) / 2
        QCOMPARE(m.harmonics.size(), size_t(10));
        QVERIFY(near(m.harmonics[2].frequency, 3000, 0.1));

        const dsa::Measurements none = dsa::measure(dsp::PowerSpectrum{}, {});
        QVERIFY(!none.valid);
        QCOMPARE(none.thdPercent, 0.0);
        QCOMPARE(none.snrDb, 0.0);
    }

    // SNR = SINAD without the harmonics: a 1 % third harmonic sets SINAD to 40 dB but leaves SNR
    // at the level of the (here: one interharmonic, -60 dB) noise.
    void snrExcludesHarmonics()
    {
        std::vector<double> x = sine(1000, 1.0, 80000, 800, 0.01);
        const std::vector<double> noise = sine(1500, 0.001, 80000, 800);
        for (size_t k = 0; k < x.size(); ++k)
            x[k] += noise[k];
        const dsa::Measurements m = dsa::measure(dsp::powerSpectrum(x, 80000, dsp::WindowType::None), x);
        QVERIFY(m.valid);
        QVERIFY2(near(m.sinadDb, 40.0, 0.05), qPrintable(QString::number(m.sinadDb)));
        QVERIFY2(near(m.snrDb, 60.0, 0.05), qPrintable(QString::number(m.snrDb)));
        // Without harmonics SNR and SINAD agree.
        const std::vector<double> clean = sine(1000, 1.0, 80000, 800);
        std::vector<double> y = clean;
        for (size_t k = 0; k < y.size(); ++k)
            y[k] += noise[k];
        const dsa::Measurements c = dsa::measure(dsp::powerSpectrum(y, 80000, dsp::WindowType::None), y);
        QVERIFY2(near(c.snrDb, c.sinadDb, 0.01), qPrintable(QString::number(c.snrDb - c.sinadDb)));
    }

    void windowedTime()
    {
        const std::vector<double> x(64, 1.0);
        QCOMPARE(dsa::scaledTimeDomainWindow(x, dsp::WindowType::None), x);
        const std::vector<double> w = dsa::scaledTimeDomainWindow(x, dsp::WindowType::Hanning);
        double mean = 0;
        for (double v : w)
            mean += v / double(w.size());
        QVERIFY(near(mean, 1.0, 1e-9));   // unit coherent gain
        QVERIFY(near(w.front(), 0.0, 1e-12));
    }

    void peakSearch()
    {
        const std::vector<double> y{0, 5, 0, 3, 0, 9, 0, 1, 0};
        QCOMPARE(dsa::searchPeak(y, dsa::PeakSearch::Highest, 0, 0.5).value(), size_t(5));
        QCOMPARE(dsa::searchPeak(y, dsa::PeakSearch::NextLower, 5, 0.5).value(), size_t(1));
        QCOMPARE(dsa::searchPeak(y, dsa::PeakSearch::NextLower, 1, 0.5).value(), size_t(3));
        QCOMPARE(dsa::searchPeak(y, dsa::PeakSearch::Left, 5, 0.5).value(), size_t(3));
        QCOMPARE(dsa::searchPeak(y, dsa::PeakSearch::Right, 5, 0.5).value(), size_t(7));
        QVERIFY(!dsa::searchPeak(y, dsa::PeakSearch::Right, 7, 0.5));
        QCOMPARE(dsa::localMaxima(y, 2.0).size(), size_t(3));   // 1 is not prominent enough
    }

    void averagingForSingle()
    {
        dsa::AveragingSettings s;
        QCOMPARE(dsa::recordsForSingle(s), 5);
        s.mode = dsp::AveragingMode::None;
        QCOMPARE(dsa::recordsForSingle(s), 1);
        s.mode = dsp::AveragingMode::PeakHold;
        s.averages = 7;
        QCOMPARE(dsa::recordsForSingle(s), 7);
    }

    // --- octave ---------------------------------------------------------------------------------

    void octaveBands()
    {
        const std::vector<octave::Band>& b = octave::bands();
        QCOMPARE(b.size(), size_t(31));   // the 31 ISO bands 20 Hz ... 20 kHz
        QCOMPARE(b.front().label, u"20 Hz"_s);
        QCOMPARE(b.back().label, u"20 kHz"_s);
        QCOMPARE(b[size_t(octave::bandIndex(1000))].label, u"1 kHz"_s);
        QCOMPARE(b[size_t(octave::bandIndex(31.5))].label, u"31.5 Hz"_s);
        QVERIFY(near(octave::weightingDb(octave::Weighting::A, 1000), 0.0, 0.01));
        QVERIFY(near(octave::weightingDb(octave::Weighting::A, 100), -19.1, 0.1));
        QVERIFY(near(octave::weightingDb(octave::Weighting::C, 1000), 0.0, 0.01));
        QVERIFY(near(octave::weightingDb(octave::Weighting::C, 31.5), -3.0, 0.1));
        QVERIFY(near(octave::weightingDb(octave::Weighting::B, 1000), 0.0, 0.01));
        QCOMPARE(octave::weightingDb(octave::Weighting::Z, 50), 0.0);
        QCOMPARE(octave::timeConstants(octave::Averaging::Impulse, 0).fall, 1.5);
    }

    void octaveFilters()
    {
        // 0 dB at the centre, about -3 dB at the edges, far down one band away.
        const octave::Band& band = octave::bands()[size_t(octave::bandIndex(1000))];
        const octave::BandPassFilter filter(band.lower, band.upper, band.centre, octave::kSampleRate);
        QVERIFY(near(filter.responseDb(band.centre), 0.0, 1e-9));
        QVERIFY2(near(filter.responseDb(band.lower), -3.0, 0.3), qPrintable(QString::number(filter.responseDb(band.lower))));
        QVERIFY(filter.responseDb(band.centre * std::pow(10.0, 0.2)) < -30.0);
        // Lowest band: still well formed at 50 kS/s.
        const octave::Band& low = octave::bands().front();
        const octave::BandPassFilter lowFilter(low.lower, low.upper, low.centre, octave::kSampleRate);
        QVERIFY(near(lowFilter.responseDb(low.centre), 0.0, 1e-6));
        QVERIFY(lowFilter.settlingTime() > filter.settlingTime());
    }

    void octaveBankLevels()
    {
        // A 1 kHz sine of 1 V peak (-3.01 dBV) lands in the 1 kHz band; neighbours are much lower.
        octave::OctaveFilterBank bank;
        bank.setTimeConstants(octave::timeConstants(octave::Averaging::Fast, 0));
        const std::vector<double> x = sine(1000, 1.0, octave::kSampleRate, 50000);
        bank.process(x);
        const int k = octave::bandIndex(1000);
        const auto level = [&](int b) { return 10 * std::log10(bank.meanSquare()[size_t(b)]); };
        QVERIFY2(near(level(k), -3.01, 0.2), qPrintable(QString::number(level(k))));
        QVERIFY(level(k - 1) < level(k) - 15);
        QVERIFY(level(k + 1) < level(k) - 15);
        QVERIFY(level(0) < level(k) - 60);
        QVERIFY(near(bank.elapsed(), 1.0, 1e-9));
    }

    // --- controller -----------------------------------------------------------------------------

    void defaults()
    {
        DsaController* c = dsa();
        QCOMPARE(c->analysisMode(), DsaController::FftAnalysis);
        QCOMPARE(c->sourceChannel(), 0);
        QCOMPARE(c->voltageRangeIndex(), 0);
        QCOMPARE(c->frequencySpan(), 40000.0);
        QCOMPARE(c->lines(), 400);
        QCOMPARE(c->resolutionLabels(), (QStringList{u"200"_s, u"400"_s, u"800"_s, u"1600"_s, u"3200"_s}));
        QCOMPARE(c->windowLabels().first(), u"None"_s);
        QCOMPARE(c->averagingMode(), DsaController::RmsAveraging);
        QCOMPARE(c->weighting(), DsaController::ExponentialWeighting);
        QCOMPARE(c->averages(), 5);
        QCOMPARE(c->units(), DsaController::DbUnits);
        QCOMPARE(c->magnitudeMode(), DsaController::RmsMagnitude);
        QCOMPARE(c->scaleMode(), DsaController::AutoScale);
        QCOMPARE(c->expectedSampleRate(), 80000.0);
        QCOMPARE(c->lineSpacing(), 100.0);
        QCOMPARE(c->magnitudeUnit(), u"dBVrms"_s);
        c->setFrequencySpan(1e6);
        QCOMPARE(c->frequencySpan(), 100000.0);   // 0.5 x maximum rate
        c->setSourceChannel(2);
        QVERIFY(c->audioSource());
        QCOMPARE(c->effectiveRangeIndex(), 1);    // audio: ±2 V only
        c->resetSettings();
        QCOMPARE(c->frequencySpan(), 40000.0);
        QCOMPARE(c->sourceChannel(), 0);
    }

    void singleSpectrum()
    {
        DsaController* c = dsa();
        c->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 5000);
        QVERIFY2(!c->hasError(), qPrintable(c->errorText()));
        QVERIFY(c->hasResult());
        QCOMPARE(c->averagesDone(), 5);   // Single completes the RMS average
        QVERIFY(near(c->fundamentalFrequency(), 1000, 1.0));
        // Fundamental power is always linear V^2 (0.5 for 1 V peak), whatever the Units.
        QVERIFY2(near(c->fundamentalPower(), 0.5, 0.01), qPrintable(QString::number(c->fundamentalPower())));
        QCOMPARE(c->powerUnit(), u"Vrms^2"_s);
        QVERIFY2(near(c->thd(), 1.0, 0.1), qPrintable(QString::number(c->thd())));
        QVERIFY2(c->sinad() > 38 && c->sinad() < 41, qPrintable(QString::number(c->sinad())));
        QVERIFY2(c->snr() > c->sinad() + 10, qPrintable(QString::number(c->snr())));   // harmonics excluded
        QVERIFY2(near(c->vpk(), 1.0, 0.02), qPrintable(QString::number(c->vpk())));
        QCOMPARE(c->spectrum()->count(), 401);
        QVERIFY(!c->harmonics().isEmpty());
        QCOMPARE(c->harmonics().first().toMap().value(u"name"_s).toString(), u"F"_s);
        // Peak table: the fundamental and the third harmonic, not the noise ripples.
        QCOMPARE(c->peaks().size(), 2);
        QVERIFY(near(c->peaks().at(0).toMap().value(u"frequency"_s).toDouble(), 1000, 1));
        QVERIFY(near(c->peaks().at(1).toMap().value(u"frequency"_s).toDouble(), 3000, 1));

        // Display settings reformat the stopped result.
        c->setUnits(DsaController::DbmUnits);
        QVERIFY(near(c->fundamentalPower(), 0.5, 0.01));
        QVERIFY(near(c->spectrum()->valueAt(1000), 10.0, 0.1));
        // dBm: Mode is inert (grayed), Peak must not shift the level by 3 dB.
        c->setMagnitudeMode(DsaController::PeakMagnitude);
        QVERIFY(near(c->spectrum()->valueAt(1000), 10.0, 0.1));
        QCOMPARE(c->magnitudeUnit(), u"dBm"_s);
        QVERIFY(near(c->fundamentalPower(), 0.5, 0.01));
        c->setMagnitudeMode(DsaController::RmsMagnitude);
        c->setUnits(DsaController::LinearUnits);
        QVERIFY(near(c->fundamentalPower(), 0.5, 0.01));
        QCOMPARE(c->powerUnit(), u"Vrms^2"_s);
        QCOMPARE(c->magnitudeUnit(), u"Vrms^2"_s);
        QVERIFY(near(c->spectrum()->valueAt(1000), 0.5, 0.01));   // Linear = power in V^2
        c->setMagnitudeMode(DsaController::PeakMagnitude);
        QVERIFY(near(c->spectrum()->valueAt(1000), 1.0, 0.01));
        QVERIFY(near(c->fundamentalPower(), 1.0, 0.02));
        QCOMPARE(c->powerUnit(), u"Vpk^2"_s);
        c->setScaleMode(DsaController::ManualScale);
        QCOMPARE(c->spectrumMaximum(), 1.0);
        QCOMPARE(c->spectrumMinimum(), 0.0);
        c->setUnits(DsaController::DbUnits);
        QCOMPARE(c->spectrumMaximum(), 10.0);    // Manual limits follow the unit's defaults
        QCOMPARE(c->spectrumMinimum(), -120.0);

        // Cursors and peak search.
        c->setMagnitudeMode(DsaController::RmsMagnitude);
        c->setCursorsOn(true);
        c->searchPeak(DsaController::HighestPeak);
        QVERIFY(near(c->cursor1Position(), 1000, 0.01));
        c->stepCursor(+1);
        QVERIFY(near(c->cursor1Position(), 1100, 0.01));
        c->searchPeak(DsaController::PeakRight);
        QVERIFY2(near(c->cursor1Position(), 3000, 0.01), qPrintable(QString::number(c->cursor1Position())));
        QVERIFY(!c->cursorDeltaText().isEmpty());

        // Export, ELVISmx-style text log (*.txt): waveform block, blank line, FFT waveform.
        QTemporaryDir txtDir;
        c->setUnits(DsaController::DbUnits);
        QVERIFY(c->exportData(QUrl::fromLocalFile(txtDir.filePath(u"dsa.txt"_s))));
        QFile txt(txtDir.filePath(u"dsa.txt"_s));
        QVERIFY(txt.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(txt.readAll()).split(u"\r\n"_s);
        QCOMPARE(lines.value(0), u"waveform\tSource signal"_s);
        QVERIFY(lines.value(1).startsWith(u"t0\t"_s));
        QVERIFY(QRegularExpression(uR"(^t0\t\d{1,2}/\d{1,2}/\d{4}  \d\d:\d\d:\d\d\.\d{6}$)"_s).match(lines.value(1)).hasMatch());
        QVERIFY(lines.value(2).startsWith(u"delta t\t1.250000E-5"_s));   // 80 kS/s
        QCOMPARE(lines.value(3), QString());
        QCOMPARE(lines.value(4), u"time[0]\tSource signal"_s);
        QVERIFY(QRegularExpression(uR"(^\d{1,2}/\d{1,2}/\d{4}  \d\d:\d\d:\d\d\.\d{6}\t-?\d\.\d{6}E[+-]\d+$)"_s).match(lines.value(5)).hasMatch());
        const qsizetype fft = lines.indexOf(u"FFT waveform"_s);
        QVERIFY(fft > 0);
        QCOMPARE(lines.value(fft - 1), QString());   // blank line before it
        QCOMPARE(lines.value(fft + 1), u"Units: dBVrms"_s);
        QVERIFY(lines.value(fft + 2).startsWith(u"0.00\t"_s));
        // 400 rows, 0 ... 39900 Hz (dsa.md 13), then the end of the file.
        QVERIFY(lines.value(fft + 2 + 399).startsWith(u"39900.00\t"_s));
        QVERIFY(lines.value(fft + 2 + 400).isEmpty());

        QTemporaryDir dir;
        QVERIFY(c->exportData(QUrl::fromLocalFile(dir.filePath(u"dsa.csv"_s))));
        QFile file(dir.filePath(u"dsa.csv"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(u"# Frequency span: 40000 Hz, lines: 400, window: None"_s));
        QVERIFY(text.contains(u"Frequency (Hz),Magnitude (dBVrms),Time (s),Amplitude (V)"_s));
        c->resetSettings();
    }

    void spanMinimumAndRestartRule()
    {
        DsaController* c = dsa();
        c->setFrequencySpan(5);
        QCOMPARE(c->frequencySpan(), 405.0);   // ELVISmx coerces to 405 Hz
        c->resetSettings();
        // Restart: only while running AND Weighting = Linear (RMS/Vector averaging).
        QVERIFY(!c->restartEnabled());   // idle
        c->run();
        QTRY_VERIFY_WITH_TIMEOUT(c->isRunning(), 3000);
        QCOMPARE(c->weighting(), DsaController::ExponentialWeighting);
        QVERIFY(!c->restartEnabled());
        c->setWeighting(DsaController::LinearWeighting);
        QVERIFY(c->restartEnabled());
        c->setAveragingMode(DsaController::PeakHoldAveraging);
        QVERIFY(!c->restartEnabled());
        c->setAveragingMode(DsaController::VectorAveraging);
        QVERIFY(c->restartEnabled());
        c->setWeighting(DsaController::ExponentialWeighting);
        QVERIFY(!c->restartEnabled());
        c->stop();
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 3000);
        c->resetSettings();
    }

    void continuousAndLiveChanges()
    {
        DsaController* c = dsa();
        c->setAveragingMode(DsaController::NoAveraging);
        c->run();
        QTRY_VERIFY_WITH_TIMEOUT(c->acquisitionCount() >= 3, 5000);
        c->setWindowIndex(1);   // Hanning: same tone power (window-compensated)
        QTRY_VERIFY_WITH_TIMEOUT(c->acquisitionCount() >= 8, 5000);
        QVERIFY2(near(c->fundamentalPower(), 0.5, 0.02), qPrintable(QString::number(c->fundamentalPower())));
        c->setSourceChannel(2);   // AudioInput Left, 440 Hz: restarts the acquisition
        QTRY_VERIFY_WITH_TIMEOUT(c->isRunning() && near(c->fundamentalFrequency(), 440, 30), 8000);
        c->stop();
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 3000);
        c->resetSettings();
    }

    void octaveMode()
    {
        DsaController* c = dsa();
        c->setAnalysisMode(DsaController::OctaveAnalysis);
        QCOMPARE(c->expectedSampleRate(), 50000.0);
        QCOMPARE(c->octaveLowBand(), 0);
        QCOMPARE(c->octaveHighBand(), 30);
        QCOMPARE(c->octaveBandLabels().at(c->octaveLowBand()), u"20 Hz"_s);
        QCOMPARE(c->octaveBandLabels().at(c->octaveHighBand()), u"20 kHz"_s);
        c->run();
        QTRY_VERIFY_WITH_TIMEOUT(c->hasOctaveResult() && c->octaveSettled(), 8000);
        QCOMPARE(c->octaveLoudestBand(), u"1 kHz"_s);
        // The 1 V peak fundamental (-3.01 dBV) dominates the overall level.
        QVERIFY2(near(c->octaveLoudestLevel(), -3.01, 0.5), qPrintable(QString::number(c->octaveLoudestLevel())));
        QCOMPARE(c->octaveLevels().size(), 31);
        c->setOctaveWeighting(DsaController::AWeighting);
        QVERIFY(near(c->octaveLoudestLevel(), -3.01, 0.5));   // A = 0 dB at 1 kHz
        c->stop();
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 3000);
        c->resetSettings();
        QCOMPARE(c->analysisMode(), DsaController::FftAnalysis);
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestDsa)
#include "tst_dsa.moc"
