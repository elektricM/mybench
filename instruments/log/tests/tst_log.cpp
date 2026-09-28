// Data Logger: file writer and naming, DMM autoranging, statistics, and the controller recording
// AI 0 / AI 1 / DMM from the simulator into files (pause, stop-after limit).
#include "logcontroller.h"
#include "logfile.h"

#include <mybench/devicehub.h>
#include <mybench/instrumentregistry.h>

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace mybench;
using namespace mybench::instruments;
using namespace Qt::StringLiterals;

namespace {

struct LogFile {
    QStringList comments;
    QStringList header;
    QList<QStringList> rows;
};

LogFile readLog(const QString& path, QChar delimiter = u'\t')
{
    LogFile log;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return log;
    bool headerRead = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).chopped(1);
        if (line.startsWith(u"# "_s))
            log.comments << line.mid(2);
        else if (!headerRead) {
            log.header = line.split(delimiter);
            headerRead = true;
        } else
            log.rows << line.split(delimiter);
    }
    return log;
}

}  // namespace

class TstLog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        InstrumentController::setRestoreSavedSettings(false);
        registerLog(*InstrumentRegistry::instance());   // titles for the file headers
        DeviceHub::instance()->select(u"sim:0"_s);
        QVERIFY(DeviceHub::instance()->isConnected());
    }

    void naming()
    {
        QTemporaryDir dir;
        const QDateTime when(QDate(2026, 9, 28), QTime(10, 15, 0));
        using W = logger::LogFileWriter;
        QCOMPARE(QFileInfo(W::uniquePath(dir.path(), u"log"_s, true, u"tsv"_s, when)).fileName(),
                 u"log_20260928_101500.tsv"_s);
        const QString plain = W::uniquePath(dir.path(), u"run 1"_s, false, u"csv"_s, when);
        QCOMPARE(QFileInfo(plain).fileName(), u"run 1.csv"_s);
        QFile taken(plain);
        QVERIFY(taken.open(QIODevice::WriteOnly));
        taken.close();
        QCOMPARE(QFileInfo(W::uniquePath(dir.path(), u"run 1"_s, false, u"csv"_s, when)).fileName(), u"run 1_2.csv"_s);
        QCOMPARE(QFileInfo(W::uniquePath(dir.path(), u"a/b:c"_s, false, u"tsv"_s, when)).fileName(), u"a_b_c.tsv"_s);
        QCOMPARE(QFileInfo(W::uniquePath(dir.path(), u"  "_s, false, u"tsv"_s, when)).fileName(), u"log.tsv"_s);
    }

    void writer()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"sub/x.tsv"_s);
        {
            logger::LogFileWriter w;
            QVERIFY(w.open(path, {u"MYBENCH - Data Logger"_s, u"Sample rate: 10 S/s"_s}, {u"Time (s)"_s, u"AI 0 (V)"_s}, '\t'));
            w.addNumber(0);
            w.addNumber(1.25);
            w.endRow();
            w.addNumber(0.1);
            w.addNumber(qQNaN());
            w.endRow();
            QVERIFY(w.flush());
            QCOMPARE(w.rows(), qint64(2));
            // Crash safety: everything flushed is in the file while it is still open.
            const LogFile partial = readLog(path);
            QCOMPARE(partial.rows.size(), 2);
        }
        const LogFile log = readLog(path);
        QCOMPARE(log.comments, (QStringList{u"MYBENCH - Data Logger"_s, u"Sample rate: 10 S/s"_s}));
        QCOMPARE(log.header, (QStringList{u"Time (s)"_s, u"AI 0 (V)"_s}));
        QCOMPARE(log.rows[0], (QStringList{u"0"_s, u"1.25"_s}));
        QCOMPARE(log.rows[1], (QStringList{u"0.1"_s, QString()}));
        logger::LogFileWriter again;
        QVERIFY(!again.open(path, {}, {u"x"_s}, '\t'));   // never overwrites
    }

    void autoRange()
    {
        const auto& ranges = logger::dmmFunctions()[0].ranges;   // 60, 20, 2, 0.2 V
        QCOMPARE(logger::autoRange(ranges, 0, 1.2345, false), 2);
        QCOMPARE(logger::autoRange(ranges, 0, 0.1, false), 3);
        QCOMPARE(logger::autoRange(ranges, 0, 25, false), 0);
        QCOMPARE(logger::autoRange(ranges, 3, 0.0, true), 2);    // overload: one range up
        QCOMPARE(logger::autoRange(ranges, 0, 70, true), 0);
        QCOMPARE(logger::autoRange(ranges, 2, 1.9, false), 2);   // stays (no hysteresis loop)
    }

    void statistics()
    {
        logger::RunningStats s;
        for (double v : {1.0, 2.0, 3.0, qInf()})
            s.add(v);
        QCOMPARE(s.count, qint64(3));
        QCOMPARE(s.mean, 2.0);
        QCOMPARE(s.standardDeviation(), 1.0);
        QCOMPARE(s.last, 3.0);
    }

    void recordsAnalog()
    {
        QTemporaryDir dir;
        LogController ctl;
        ctl.initialize();
        QCOMPARE(ctl.samplingRate(), 10.0);   // ELVISmx default
        QVERIFY(ctl.resources().testFlag(ResourceArbiter::AnalogInput));
        QVERIFY(!ctl.resources().testFlag(ResourceArbiter::Multimeter));
        ctl.setFolder(dir.path());
        ctl.setSamplingRate(200);
        ctl.setAi1Enabled(true);
        ctl.startRecording();   // starts the run too
        QTRY_VERIFY_WITH_TIMEOUT(ctl.recording(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(ctl.recordRows() >= 100, 5000);
        const QString path = ctl.recordFile();
        ctl.setRecordPaused(true);
        QTRY_VERIFY_WITH_TIMEOUT(ctl.recordPaused(), 2000);
        QTest::qWait(300);
        ctl.setRecordPaused(false);
        QTRY_VERIFY_WITH_TIMEOUT(!ctl.recordPaused(), 2000);
        QTest::qWait(300);
        ctl.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(!ctl.recording(), 2000);
        QVERIFY(ctl.isRunning());   // monitoring goes on
        QVERIFY(ctl.channelStats().size() == 2);
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);

        const LogFile log = readLog(path);
        QVERIFY(log.comments.first().contains(u"Data Logger"_s));
        QVERIFY(log.comments.contains(u"Sample rate: 200 S/s"_s));
        QVERIFY(log.comments.contains(u"Channels: AI 0, AI 1"_s));
        QCOMPARE(log.header, (QStringList{u"Time (s)"_s, u"AI 0 (V)"_s, u"AI 1 (V)"_s}));
        QVERIFY(log.rows.size() >= 100);
        QCOMPARE(log.rows[0][0], u"0"_s);
        QCOMPARE(log.rows[1][0], u"0.005"_s);
        // The pause shows as a gap in the time column.
        double largestStep = 0;
        for (qsizetype i = 1; i < log.rows.size(); ++i)
            largestStep = std::max(largestStep, log.rows[i][0].toDouble() - log.rows[i - 1][0].toDouble());
        QVERIFY(largestStep > 0.1);
        for (const QStringList& row : log.rows) {
            QCOMPARE(row.size(), 3);
            QVERIFY(std::abs(row[1].toDouble()) < 1.2);
        }
    }

    void recordsDmmAndLimit()
    {
        QTemporaryDir dir;
        LogController ctl;
        ctl.initialize();
        ctl.setFolder(dir.path());
        ctl.setBaseName(u"mixed"_s);
        ctl.setAppendTimestamp(false);
        ctl.setFileFormat(LogController::CommaSeparated);
        ctl.setTimestampColumn(true);
        ctl.setDmmEnabled(true);
        ctl.setSamplingRate(100);
        ctl.setRecordOnRun(true);
        ctl.setStopAfterEnabled(true);
        ctl.setStopAfterMinutes(0.02);   // 1.2 s
        QVERIFY(ctl.resources().testFlag(ResourceArbiter::Multimeter));
        ctl.run();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.recording(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 6000);   // ended by the duration limit
        const LogFile log = readLog(dir.filePath(u"mixed.csv"_s), u',');
        QCOMPARE(log.header, (QStringList{u"Time (s)"_s, u"Timestamp"_s, u"AI 0 (V)"_s, u"DMM time (s)"_s,
                                          u"DMM DC Voltage (V)"_s}));
        QVERIFY(log.rows.size() >= 110 && log.rows.size() <= 130);
        int dmmValues = 0;
        for (const QStringList& row : log.rows) {
            QCOMPARE(row.size(), 5);
            QVERIFY(QDateTime::fromString(row[1], Qt::ISODateWithMs).isValid());
            if (!row[4].isEmpty()) {
                ++dmmValues;
                QVERIFY(std::abs(row[4].toDouble() - 1.2345) < 0.01);   // autoranged to 2 V
            }
        }
        QVERIFY(dmmValues >= 4);
    }

    void dmmOnly()
    {
        QTemporaryDir dir;
        LogController ctl;
        ctl.initialize();
        ctl.setFolder(dir.path());
        ctl.setAi0Enabled(false);
        ctl.setDmmEnabled(true);
        ctl.setDmmFunction(4);   // resistance
        QVERIFY(!ctl.resources().testFlag(ResourceArbiter::AnalogInput));
        ctl.startRecording();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.recordRows() >= 3, 5000);
        const QString path = ctl.recordFile();
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
        const LogFile log = readLog(path);
        QCOMPARE(log.header, (QStringList{u"Time (s)"_s, u"DMM Resistance (Ω)"_s}));
        QVERIFY(!log.rows.isEmpty());
        QVERIFY(std::abs(log.rows.last()[1].toDouble() - 4700) < 20);
    }

    void refusesWithoutChannels()
    {
        LogController ctl;
        ctl.initialize();
        ctl.setAi0Enabled(false);
        ctl.run();
        QVERIFY(ctl.isIdle());
        QVERIFY(ctl.hasError());
    }
};

QTEST_MAIN(TstLog)
#include "tst_log.moc"
