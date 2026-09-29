// DmmController against the simulator (docs/instruments/dmm.md section 11 "Checks against the
// simulator"): readouts per range, autoranging, null offset, diode, continuity, log file.
#include "dmmcontroller.h"

#include <mybench/devicehub.h>

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using mybench::instruments::DmmController;

class TestDmmController : public QObject
{
    Q_OBJECT

    std::shared_ptr<mydaq::SimDevice> m_sim;
    std::unique_ptr<DmmController> m_dmm;

    // One displayed reading (Single), waits until the run is over.
    void measureOnce()
    {
        const int before = m_dmm->readingSerial();
        m_dmm->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_dmm->readingSerial() > before && m_dmm->isIdle(), 5000);
    }

private slots:
    void initTestCase()
    {
        mybench::InstrumentController::setRestoreSavedSettings(false);
        m_sim = std::dynamic_pointer_cast<mydaq::SimDevice>(mydaq::DeviceManager::instance().device(u"sim:0"_s));
        QVERIFY(m_sim);
        mybench::DeviceHub::instance()->select(u"sim:0"_s);
        QTRY_VERIFY(mybench::DeviceHub::instance()->isConnected());
        m_sim->setDmmReadTime(5, 5);
    }

    void init()
    {
        m_dmm = std::make_unique<DmmController>();
        m_dmm->initialize();
        m_sim->setDmmOpenCircuit(false);
        m_sim->setDmmValue(mydaq::DmmFunction::DcVoltage, 1.2345);
    }
    void cleanup() { m_dmm.reset(); }

    void defaults()
    {
        QCOMPARE(m_dmm->selectedFunction(), DmmController::DcVoltage);
        QVERIFY(!m_dmm->autoRange());
        QCOMPARE(m_dmm->rangeLabels(), (QStringList{u"200mV"_s, u"2V"_s, u"20V"_s, u"60V"_s}));
        QCOMPARE(m_dmm->selectedRangeLabel(), u"60V"_s);
        QVERIFY(!m_dmm->hasReading());
        QCOMPARE(m_dmm->readingText(), u"---"_s);
        QCOMPARE(m_dmm->fullScaleFraction(), -1.0);
    }

    void specifyRange60V()
    {
        measureOnce();
        QVERIFY2(m_dmm->readingText() == u"1.23"_s || m_dmm->readingText() == u"1.24"_s,   // 10 mV resolution
                 qPrintable(m_dmm->readingText()));
        QCOMPARE(m_dmm->readingUnit(), u"V"_s);
        QVERIFY(qAbs(m_dmm->fullScaleFraction() - 1.2345 / 60) < 0.001);
    }

    void autoSettles()
    {
        m_dmm->setAutoRange(true);
        measureOnce();
        QCOMPARE(m_dmm->activeRangeLabel(), u"2V"_s);
        QVERIFY2(m_dmm->readingText() == u"1.235"_s || m_dmm->readingText() == u"1.234"_s
                     || m_dmm->readingText() == u"1.236"_s,
                 qPrintable(m_dmm->readingText()));
    }

    // Audit dmm #1: a read still in flight when the function changes must not reach the new one.
    void staleReadingDroppedOnFunctionChange()
    {
        m_sim->setDmmReadTime(250, 250);
        QStringList units;
        connect(m_dmm.get(), &DmmController::readingChanged, this, [&] {
            if (m_dmm->hasReading())
                units << m_dmm->readingUnit();
        });
        m_dmm->run();
        QTest::qWait(60);   // the first DC read is in flight
        m_dmm->setSelectedFunction(DmmController::Resistance);
        QVERIFY(!m_dmm->hasReading());
        QTest::qWait(900);
        m_dmm->stop();
        QTRY_VERIFY_WITH_TIMEOUT(m_dmm->isIdle(), 5000);
        m_sim->setDmmReadTime(5, 5);
        QVERIFY(!units.isEmpty());
        for (const QString& unit : std::as_const(units))
            QVERIFY2(unit != u"V"_s, qPrintable(units.join(u',')));
    }

    // Audit dmm #2: a device error leaves `---`, not the last value.
    void errorClearsReadout()
    {
        m_dmm->run();
        QTRY_VERIFY_WITH_TIMEOUT(m_dmm->hasReading(), 5000);
        m_sim->setConnected(false);
        QTRY_VERIFY_WITH_TIMEOUT(m_dmm->isIdle(), 5000);
        QVERIFY(m_dmm->hasError() || !mybench::DeviceHub::instance()->isConnected());
        QVERIFY(!m_dmm->hasReading());
        QCOMPARE(m_dmm->readingText(), u"---"_s);
        m_sim->setConnected(true);
        QTRY_VERIFY_WITH_TIMEOUT(mybench::DeviceHub::instance()->isConnected(), 5000);
    }

    // Audit dmm #6: nothing to export right after a function change.
    void noExportAfterFunctionChange()
    {
        measureOnce();
        QVERIFY(m_dmm->hasData());
        m_dmm->setSelectedFunction(DmmController::Resistance);
        QVERIFY(!m_dmm->hasData());
    }

    void resistanceAuto()
    {
        m_dmm->setAutoRange(true);
        m_dmm->setSelectedFunction(DmmController::Resistance);
        QCOMPARE(m_dmm->selectedRangeLabel(), u"20MΩ"_s);   // back to the largest range
        measureOnce();
        QCOMPARE(m_dmm->activeRangeLabel(), u"20kΩ"_s);
        QVERIFY2(qAbs(m_dmm->reading() - 4700) < 15, qPrintable(m_dmm->readingText()));
        QCOMPARE(m_dmm->readingText().size(), 4);   // "4.70": 2 decimals on 20kΩ
        QCOMPARE(m_dmm->readingUnit(), u"kΩ"_s);
        m_sim->setDmmOpenCircuit(true);
        measureOnce();
        QVERIFY(m_dmm->overload());
        QCOMPARE(m_dmm->readingText(), u"+Over"_s);
        QCOMPARE(m_dmm->fullScaleFraction(), 1.0);
    }

    void overloadSpecifyRange()
    {
        m_dmm->setRangeIndex(0);   // 200mV
        m_sim->setDmmValue(mydaq::DmmFunction::DcVoltage, -1.0);
        measureOnce();
        QVERIFY(m_dmm->overload());
        QCOMPARE(m_dmm->readingText(), u"+Over"_s);   // ELVISmx: +Over whatever the sign
        m_dmm->setSignedOverload(true);
        measureOnce();
        QCOMPARE(m_dmm->readingText(), u"-Over"_s);
        QCOMPARE(m_dmm->statOverloads(), 2);
        QCOMPARE(m_dmm->statCount(), 0);
    }

    void nullOffset()
    {
        m_dmm->setRangeIndex(1);   // 2V
        m_dmm->setNullOffset(true);
        QVERIFY(m_dmm->nullArmed());
        measureOnce();
        QVERIFY(!m_dmm->nullArmed());
        QVERIFY(qAbs(m_dmm->nullValue() - 1.2345) < 0.002);
        QCOMPARE(m_dmm->readingText(), u"0.000"_s);
        m_sim->setDmmValue(mydaq::DmmFunction::DcVoltage, 1.3);
        measureOnce();
        QVERIFY(qAbs(m_dmm->reading() - 0.0655) < 0.003);
        // A function change turns Null Offset off (ELVISmx).
        m_dmm->setSelectedFunction(DmmController::AcVoltage);
        QVERIFY(!m_dmm->nullOffset());
        QCOMPARE(m_dmm->selectedRangeLabel(), u"20V"_s);
        QCOMPARE(m_dmm->readingText(), u"---"_s);
    }

    void diode()
    {
        m_dmm->setSelectedFunction(DmmController::Diode);
        QCOMPARE(m_dmm->rangeLabels(), QStringList{u"2V"_s});
        measureOnce();
        QVERIFY(qAbs(m_dmm->reading() - 0.650) < 0.0015);   // ±1 count of simulated noise
        QCOMPARE(m_dmm->readingText().size(), 5);           // "0.650": 3 decimals on 2V
        QCOMPARE(m_dmm->diodeStatus(), u"GOOD"_s);
        m_sim->setDmmOpenCircuit(true);
        measureOnce();
        QCOMPARE(m_dmm->readingText(), u"OPEN"_s);
        QCOMPARE(m_dmm->diodeStatus(), u"OPEN"_s);
    }

    void continuity()
    {
        m_dmm->setSelectedFunction(DmmController::Continuity);
        QVERIFY(m_dmm->effectiveAutoRange());   // forced Auto
        QVERIFY(!m_dmm->rangeSelectable());
        measureOnce();
        QVERIFY(m_dmm->reading() > 0.65 && m_dmm->reading() < 0.95);
        QVERIFY(m_dmm->continuityClosed());
        m_sim->setDmmOpenCircuit(true);
        measureOnce();
        QVERIFY(!m_dmm->continuityClosed());
        QCOMPARE(m_dmm->readingText(), u"+Over"_s);
    }

    void holdFreezesReadout()
    {
        m_dmm->setRangeIndex(1);
        measureOnce();
        const QString held = m_dmm->readingText();
        m_dmm->setHold(true);
        m_sim->setDmmValue(mydaq::DmmFunction::DcVoltage, 0.5);
        measureOnce();
        QCOMPARE(m_dmm->readingText(), held);
        QCOMPARE(m_dmm->statCount(), 2);   // statistics continue
        m_dmm->setHold(false);
        measureOnce();
        QVERIFY(qAbs(m_dmm->reading() - 0.5) < 0.0015);
    }

    void continuousPacing()
    {
        m_dmm->run();
        QTest::qWait(1100);
        m_dmm->stop();
        QTRY_VERIFY(m_dmm->isIdle());
        // At most one displayed reading per 200 ms.
        QVERIFY2(m_dmm->statCount() >= 3 && m_dmm->statCount() <= 6, qPrintable(QString::number(m_dmm->statCount())));
        QVERIFY(m_dmm->trend()->count() == m_dmm->statCount());
    }

    // A range change costs the real meter ~2.6 s, so Run Continuously displays the reading taken on
    // the old range at once (first on 60 V) and takes the next on the range that fits; it never
    // repeats a reading first (that doubled the cost of every change).
    void autoShowsEveryReadingWithoutRepeat()
    {
        QTemporaryDir dir;
        m_dmm->setAutoRange(true);
        m_dmm->setLogFile(QUrl::fromLocalFile(dir.filePath(u"auto.tsv"_s)));
        m_dmm->setRecording(true);
        m_dmm->run();
        QTRY_VERIFY_WITH_TIMEOUT(m_dmm->logCount() >= 3, 5000);
        m_dmm->stop();
        QTRY_VERIFY(m_dmm->isIdle());
        m_dmm->setRecording(false);
        QFile file(dir.filePath(u"auto.tsv"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QStringList ranges;
        for (const QByteArray& line : file.readAll().split('\n')) {
            if (!line.isEmpty() && !line.startsWith('#') && !line.startsWith("Time"))
                ranges << QString::fromUtf8(line).split(u'\t').value(3);
        }
        QVERIFY(ranges.size() >= 3);
        QCOMPARE(ranges[0], u"60V"_s);   // shown although taken on the range that is then left
        QCOMPARE(ranges[1], u"2V"_s);
        QCOMPARE(ranges[2], u"2V"_s);
    }

    void logging()
    {
        QTemporaryDir dir;
        const QString streamed = dir.filePath(u"stream.tsv"_s);
        m_dmm->setRangeIndex(1);
        m_dmm->setLogFile(QUrl::fromLocalFile(streamed));
        m_dmm->setRecording(true);
        measureOnce();
        measureOnce();
        m_dmm->setRecording(false);
        QCOMPARE(m_dmm->logCount(), 2);

        const QString saved = dir.filePath(u"saved.csv"_s);
        QVERIFY(m_dmm->saveLog(QUrl::fromLocalFile(saved)));
        for (const QString& path : {streamed, saved}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QStringList lines;
            for (const QByteArray& line : file.readAll().split('\n')) {
                if (!line.isEmpty() && !line.startsWith('#'))
                    lines << QString::fromUtf8(line);
            }
            QCOMPARE(lines.size(), 3);   // header + 2 readings
            const QChar separator = path.endsWith(u".csv"_s) ? u',' : u'\t';
            QVERIFY(lines[0].startsWith(u"Time (ISO 8601)"_s + separator));
            QCOMPARE(lines[1].split(separator).value(2), u"DC Voltage"_s);
            QCOMPARE(lines[1].split(separator).value(3), u"2V"_s);
        }
        m_dmm->clearLog();
        QCOMPARE(m_dmm->logCount(), 0);
    }

    void exportTable()
    {
        measureOnce();
        measureOnce();
        QVERIFY(m_dmm->hasData());
        const QVariantMap metadata = m_dmm->exportMetadata();
        QCOMPARE(metadata.value(u"Function"_s).toString(), u"DC Voltage"_s);
        QCOMPARE(metadata.value(u"Range"_s).toString(), u"60V"_s);
    }
};

QTEST_MAIN(TestDmmController)
#include "tst_dmmcontroller.moc"
