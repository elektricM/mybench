// BodeController against the simulated myDAQ: a fast sweep, markers, polarity, references,
// validation, pause/resume and export.
#include "bodecontroller.h"

#include <mybench/framework.h>

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace mybench;
using namespace mybench::instruments;
using namespace Qt::StringLiterals;

class TestBodeController : public QObject
{
    Q_OBJECT

    BodeController* bode() const
    {
        return qobject_cast<BodeController*>(InstrumentRegistry::instance()->controller(u"bode"_s));
    }

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_bodecontroller"_s);
        InstrumentController::setRestoreSavedSettings(false);
        QVERIFY(DeviceHub::instance()->isSimulated());
        registerBode(*InstrumentRegistry::instance());
        InstrumentRegistry::instance()->createAll();
        QVERIFY(bode());
    }

    void cleanupTestCase() { InstrumentRegistry::instance()->shutdown(); }

    void defaults()
    {
        BodeController* c = bode();
        QCOMPARE(c->startFrequency(), 100.0);
        QCOMPARE(c->stopFrequency(), 10000.0);
        QCOMPARE(c->stepsPerDecade(), 5);
        QCOMPARE(c->peakAmplitude(), 2.0);
        QCOMPARE(c->polarity(), BodeController::Normal);
        QCOMPARE(c->mapping(), BodeController::Logarithmic);
        QVERIFY(c->autoscale());
        QVERIFY(!c->cursorsOn());
        QCOMPARE(c->sweepMode(), BodeController::Standard);
        QCOMPARE(c->plannedPoints(), 11);
        QVERIFY(!c->supportsSingle());
        QCOMPARE(c->readoutGainDb(), -std::numeric_limits<double>::infinity());
        // Limits (ELVISmx): 1 Hz ... 20 kHz, 0 ... 5 V.
        c->setStopFrequency(1e6);
        QCOMPARE(c->stopFrequency(), 20000.0);
        c->setPeakAmplitude(7);
        QCOMPARE(c->peakAmplitude(), 5.0);
        c->resetSettings();
        QCOMPARE(c->stopFrequency(), 10000.0);
        QCOMPARE(c->peakAmplitude(), 2.0);
    }

    void startAboveStopIsRefused()
    {
        BodeController* c = bode();
        c->setStartFrequency(5000);
        c->setStopFrequency(1000);
        QVERIFY(!c->settingsValid());
        c->run();
        QVERIFY(!c->isRunning());
        QCOMPARE(c->errorText(), u"Start Frequency must not be higher than Stop Frequency."_s);
        c->resetSettings();
        c->clearError();
    }

    void fastSweep()
    {
        BodeController* c = bode();
        c->setSweepMode(BodeController::Fast);
        c->setStartFrequency(100);
        c->setStopFrequency(10000);
        c->setStepsPerDecade(5);
        c->run();
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 20000);
        QVERIFY2(!c->hasError(), qPrintable(c->errorText()));
        QCOMPARE(c->pointCount(), 11);
        QCOMPARE(c->totalPoints(), 11);
        QVERIFY(c->hasData());
        QCOMPARE(c->gain()->count(), 11);
        // Simulator RC low-pass at 1 kHz: -3 dB / -45 deg there, one -3 dB marker at ~1 kHz.
        c->setCursorsOn(true);
        c->setCursorFrequency(1000);
        QVERIFY(std::abs(c->readoutFrequency() - 1000) < 0.01);
        QVERIFY2(std::abs(c->readoutGainDb() + 3.01) < 0.05, qPrintable(QString::number(c->readoutGainDb())));
        QVERIFY2(std::abs(c->readoutPhase() + 45.0) < 0.5, qPrintable(QString::number(c->readoutPhase())));
        QCOMPARE(c->cutoffFrequencies().size(), 1);
        QVERIFY2(std::abs(c->cutoffFrequencies().first() / 1000 - 1) < 0.03,
                 qPrintable(QString::number(c->cutoffFrequencies().first())));
        QVERIFY(std::isnan(c->unityGainFrequency()));

        // Polarity is a display transform: 1 kHz reads +135 deg, gain unchanged.
        c->setPolarity(BodeController::Inverted);
        QVERIFY(std::abs(c->readoutPhase() - 135.0) < 0.5);
        QVERIFY(std::abs(c->readoutGainDb() + 3.01) < 0.05);
        c->setPolarity(BodeController::Normal);

        // Mapping Linear: the gain trace holds ratios.
        c->setMapping(BodeController::Linear);
        QVERIFY(std::abs(c->gain()->valueAt(1000) - 0.7071) < 0.01);
        c->setMapping(BodeController::Logarithmic);
        QVERIFY(std::abs(c->gain()->valueAt(1000) + 3.01) < 0.05);

        // Cursor stepping.
        const int index = c->cursorIndex();
        c->cursorRight();
        QCOMPARE(c->cursorIndex(), index + 1);
        c->cursorLeft();
        c->cursorLeft();
        QCOMPARE(c->cursorIndex(), index - 1);

        // Export.
        const QVariantMap metadata = c->exportMetadata();
        QVERIFY(metadata.value(u"Stimulus"_s).toString().contains(u"AI 1"_s));
        QTemporaryDir dir;
        QVERIFY(c->exportData(QUrl::fromLocalFile(dir.filePath(u"bode.csv"_s))));
        QFile file(dir.filePath(u"bode.csv"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(u"Frequency (Hz),Gain,Gain (dB),Phase (deg),Clipped"_s));
        QVERIFY(text.contains(u"# Peak amplitude: 2 V, Op-amp signal polarity: Normal"_s));

        // Keep as reference, then persistence of references.
        QVERIFY(c->keepAsReference(u"RC"_s));
        QCOMPARE(c->references().size(), 1);
        QCOMPARE(c->referenceGain(0)->count(), 11);
        const QJsonObject saved = c->saveSettings();
        c->clearReferences();
        QCOMPARE(c->references().size(), 0);
        c->restoreSettings(saved);
        QCOMPARE(c->references().size(), 1);
        QCOMPARE(c->references().first().toMap().value(u"name"_s).toString(), u"RC"_s);
        QCOMPARE(c->referenceGain(0)->count(), 11);
        c->clearReferences();
    }

    void remeasure()
    {
        BodeController* c = bode();
        QVERIFY(c->pointCount() == 11);
        c->setCursorsOn(true);
        c->setCursorFrequency(1000);
        c->remeasureCursorPoint();
        QVERIFY(c->isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 5000);
        QCOMPARE(c->pointCount(), 11);
        QVERIFY(std::abs(c->readoutGainDb() + 3.01) < 0.05);
    }

    void pauseAndResume()
    {
        BodeController* c = bode();
        c->setSweepMode(BodeController::Fast);
        c->setStartFrequency(100);
        c->setStopFrequency(10000);
        c->setStepsPerDecade(10);   // 21 points
        c->run();
        QTRY_VERIFY_WITH_TIMEOUT(c->pointCount() >= 3, 10000);
        c->pause(u"Bode Analyzer paused — AI in use by Test"_s);
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Paused, 5000);
        const int kept = c->pointCount();
        QVERIFY(kept < 21);
        c->resume();
        QTRY_VERIFY_WITH_TIMEOUT(c->state() == InstrumentController::Idle, 20000);
        QVERIFY2(!c->hasError(), qPrintable(c->errorText()));
        QCOMPARE(c->pointCount(), 21);
        // Strictly increasing frequencies: the resumed sweep continued where it stopped.
        const auto snapshot = c->gain()->snapshot();
        for (size_t i = 1; i < snapshot->size(); ++i)
            QVERIFY(snapshot->xAt(i) > snapshot->xAt(i - 1));
        c->resetSettings();
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestBodeController)
#include "tst_bodecontroller.moc"
