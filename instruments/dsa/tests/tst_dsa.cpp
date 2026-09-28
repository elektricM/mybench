// Spectrum Analyzer tests: display conversions, measurements, peak search, the third-octave
// filter bank, and the controller against the simulated myDAQ (AI 0: 1 kHz, 1 V peak, 1 % third
// harmonic; AudioInput Left: 440 Hz, 0.5 V peak).
#include "dsaanalysis.h"
#include "dsacontroller.h"
#include "dsaformat.h"
#include "octaveanalysis.h"

#include <mybench/framework.h>

#include <mydaq/dsp/waveform.h>

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
        QCOMPARE(dsa::magnitudeUnit(f), u"Vrms/√Hz"_s);
        QCOMPARE(dsa::powerUnit(dsp::SpectrumUnit::Linear, dsp::SpectrumMode::Rms), u"Vrms²"_s);
        QCOMPARE(dsa::powerUnit(dsp::SpectrumUnit::Db, dsp::SpectrumMode::Peak), u"dBVpk"_s);
    }

    void powerValues()
    {
        // A 1 V peak sine: 0.5 Vrms².
        QVERIFY(near(dsa::powerValue(0.5, dsp::SpectrumUnit::Db, dsp::SpectrumMode::Rms), -3.0103, 1e-3));
        QVERIFY(near(dsa::powerValue(0.5, dsp::SpectrumUnit::Db, dsp::SpectrumMode::Peak), 0.0, 1e-9));
        QVERIFY(near(dsa::powerValue(0.5, dsp::SpectrumUnit::Dbm, dsp::SpectrumMode::Rms), 10.0, 1e-9));
        QCOMPARE(dsa::powerValue(0.5, dsp::SpectrumUnit::Linear, dsp::SpectrumMode::Rms), 0.5);
        QVERIFY(near(dsa::levelValue(0.5, dsp::SpectrumUnit::Linear, dsp::SpectrumMode::Rms), 0.70711, 1e-5));
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
        QCOMPARE(b.size(), size_t(34));
        QCOMPARE(b.front().label, u"10 Hz"_s);
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
        QVERIFY2(near(c->fundamentalPower(), -3.01, 0.1), qPrintable(QString::number(c->fundamentalPower())));
        QVERIFY2(near(c->thd(), 1.0, 0.1), qPrintable(QString::number(c->thd())));
        QVERIFY2(c->sinad() > 38 && c->sinad() < 41, qPrintable(QString::number(c->sinad())));
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
        QVERIFY(near(c->fundamentalPower(), 10.0, 0.1));
        c->setUnits(DsaController::LinearUnits);
        QVERIFY(near(c->fundamentalPower(), 0.5, 0.01));
        QCOMPARE(c->powerUnit(), u"Vrms²"_s);
        QVERIFY(near(c->spectrum()->valueAt(1000), 0.7071, 0.01));
        c->setMagnitudeMode(DsaController::PeakMagnitude);
        QVERIFY(near(c->spectrum()->valueAt(1000), 1.0, 0.01));
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

        // Export.
        QTemporaryDir dir;
        QVERIFY(c->exportData(QUrl::fromLocalFile(dir.filePath(u"dsa.csv"_s))));
        QFile file(dir.filePath(u"dsa.csv"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(u"# Frequency span: 40000 Hz, lines: 400, window: None"_s));
        QVERIFY(text.contains(u"Frequency (Hz),Magnitude (dBVrms),Time (s),Amplitude (V)"_s));
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
        QVERIFY2(near(c->fundamentalPower(), -3.01, 0.15), qPrintable(QString::number(c->fundamentalPower())));
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
