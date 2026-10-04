// Data Logger: file writer and naming, DMM autoranging, statistics, and the controller recording
// AI 0 / AI 1 / DMM from the simulator into files (pause, stop-after limit).
#include "logcontroller.h"
#include "logfile.h"

#include <mybench/devicehub.h>
#include <mybench/instrumentregistry.h>

#include <QJsonObject>
#include <QRegularExpression>
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

    // Round-2 UI: one Format choice (Tab / CSV / ELVISmx text) replaces Format + Layout. Old
    // settings with layout = ELVISmx restore as the ELVISmx format; choosing Tab/CSV goes back
    // to the native layout.
    void fileTypeMergesLayout()
    {
        LogController ctl;
        ctl.initialize();
        QCOMPARE(ctl.fileType(), LogController::TabFile);
        QJsonObject old;
        old.insert(u"fileLayout"_s, u"ElvismxLayout"_s);
        old.insert(u"fileFormat"_s, u"CommaSeparated"_s);
        ctl.restoreSettings(old);
        QCOMPARE(ctl.fileType(), LogController::ElvismxFile);
        ctl.setFileType(LogController::CsvFile);
        QCOMPARE(ctl.fileLayout(), LogController::NativeLayout);
        QCOMPARE(ctl.fileFormat(), LogController::CommaSeparated);
        ctl.setFileType(LogController::TabFile);
        QCOMPARE(ctl.fileFormat(), LogController::TabSeparated);
        QVERIFY(!ctl.saveSettings().contains(u"fileType"_s));
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
        // Smallest first: 0.2, 2, 20, 60 V; the DMM instrument's rule (dmm.md 4.2): the next
        // range comes directly from the reading, 2000-count limits at 0.9995 x range.
        const auto& ranges = logger::dmmFunctions()[0].ranges;
        QCOMPARE(logger::dmmFunctions()[0].ranges.front().max, 0.2);
        QCOMPARE(logger::autoRange(ranges, 3, 1.2345, false), 1);   // 1.23 V -> 2 V
        QCOMPARE(logger::autoRange(ranges, 3, 0.1, false), 0);          // straight down, no stepping
        QCOMPARE(logger::autoRange(ranges, 0, 25, false), 3);           // straight up
        QCOMPARE(logger::autoRange(ranges, 0, 1.999, false), 2);        // at the 2 V limit
        QCOMPARE(logger::autoRange(ranges, 1, 1.998, false), 1);
        QCOMPARE(logger::autoRange(ranges, 1, 0.0, true), 2);           // overload: one range up
        QCOMPARE(logger::autoRange(ranges, 3, 70, true), 3);
        // Hysteresis: a floating input around 0.2 V must not hunt between 200 mV and 2 V.
        QCOMPARE(logger::autoRange(ranges, 1, 0.19, false), 1);
        QCOMPARE(logger::autoRange(ranges, 1, 0.17, false), 0);
        QCOMPARE(logger::autoRange(ranges, 0, 0.2005, true), 1);
        QCOMPARE(logger::autoRange(ranges, 3, 0.19, false), 0);         // from 60 V: straight to 200 mV
        // Resistance: 8.26 k read on the 20 M range goes to 20 k at once.
        const auto& ohms = logger::dmmFunctions()[4].ranges;
        QCOMPARE(logger::autoRange(ohms, 5, 8260, false), 2);
    }

    // A floating input reads ~0 V on 2 V but overloads 200 mV: the ranger must not hunt (each change
    // costs the real meter ~2.6 s; the Data Logger's DMM column got 2 points in 7 s).
    void autoRangerDoesNotHunt()
    {
        const auto& ranges = logger::dmmFunctions()[0].ranges;   // 0.2, 2, 20, 60 V
        logger::AutoRanger ranger;
        int index = 3, changes = 0;
        for (int i = 0; i < 200; ++i) {
            const bool over = ranges[size_t(index)].max <= 0.2;
            const int next = ranger.next(ranges, index, over ? 0.2 : 0.0002, over);
            changes += next != index;
            index = next;
        }
        QVERIFY2(changes <= 10, qPrintable(QString::number(changes)));
        QCOMPARE(index, 1);
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
        QCOMPARE(ctl.samplingRate(), 1000.0);   // ELVISmx default (observed)
        QCOMPARE(ctl.chartCapacity(), u"5 h 33 min"_s);
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
        // Deterministic: assert on the logged sample times, never on rows per wall-clock second.
        QVERIFY(log.rows.size() >= 10);
        double previous = -1;
        for (const QStringList& row : log.rows) {
            const double t = row[0].toDouble();
            QVERIFY2(t > previous, "time column must increase");
            if (previous >= 0)
                QVERIFY2(std::abs((t - previous) - 0.010) < 1e-4, qPrintable(u"spacing %1 s"_s.arg(t - previous)));
            previous = t;
        }
        QVERIFY2(previous <= 1.2 + 0.3, qPrintable(u"last sample at %1 s: the 1.2 s limit was not honoured"_s.arg(previous)));
        int dmmValues = 0;
        for (const QStringList& row : log.rows) {
            QCOMPARE(row.size(), 5);
            QVERIFY(QDateTime::fromString(row[1], Qt::ISODateWithMs).isValid());
            if (!row[4].isEmpty()) {
                ++dmmValues;
                QVERIFY(std::abs(row[4].toDouble() - 1.2345) < 0.01);   // autoranged to 2 V
            }
        }
        QVERIFY(dmmValues >= 1);
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

    void elvismxCellFormats()
    {
        QCOMPARE(logger::elvismxValue(5.174413), u"5.174413E+0"_s);
        QCOMPARE(logger::elvismxValue(-0.0122074), u"-1.220740E-2"_s);
        QCOMPARE(logger::elvismxValue(0.0), u"0.000000E+0"_s);
        QCOMPARE(logger::elvismxValue(1.5e10), u"1.500000E+10"_s);
        QCOMPARE(logger::elvismxValue(qQNaN()), QString());
        const qint64 us = QDateTime(QDate(2026, 9, 28), QTime(10, 15, 0, 1)).toMSecsSinceEpoch() * 1000 + 42;
        QCOMPARE(logger::elvismxTime(us), u"9/28/2026  10:15:00.001042"_s);   // two spaces, six digits
        QCOMPARE(logger::elvismxHeader(u"myDAQ1"_s, {u"ai0"_s, u"ai1"_s}),
                 (QStringList{u"Time"_s, u"myDAQ1/ai0"_s, u"Time"_s, u"myDAQ1/ai1"_s}));
    }

    void elvismxLayoutFile()
    {
        QTemporaryDir dir;
        LogController ctl;
        ctl.initialize();
        ctl.setFolder(dir.path());
        ctl.setBaseName(u"compat"_s);
        ctl.setAppendTimestamp(false);
        ctl.setFileLayout(LogController::ElvismxLayout);
        ctl.setAppendToExisting(true);
        ctl.setAi1Enabled(true);
        ctl.setSamplingRate(200);
        QCOMPARE(ctl.fileExtension(), u"txt"_s);
        const QString path = dir.filePath(u"compat.txt"_s);
        for (int session = 0; session < 2; ++session) {   // the second Log press appends
            ctl.startRecording();
            QTRY_VERIFY_WITH_TIMEOUT(ctl.recording(), 3000);
            QTest::qWait(300);
            ctl.stop();
            QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 2000);
            QCOMPARE(ctl.nextFilePath(), path);
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.contains("\r\n"));
        QVERIFY(!bytes.contains("#"));
        const QStringList lines = QString::fromLatin1(bytes).split(u"\r\n"_s, Qt::SkipEmptyParts);
        QCOMPARE(lines[0], u"Time\tmyDAQ1/ai0\tTime\tmyDAQ1/ai1"_s);
        const QRegularExpression cell(uR"(^\d{1,2}/\d{1,2}/\d{4}  \d\d:\d\d:\d\d\.\d{6}\t-?\d\.\d{6}E[+-]\d+\t\d{1,2}/\d{1,2}/\d{4}  \d\d:\d\d:\d\d\.\d{6}\t-?\d\.\d{6}E[+-]\d+$)"_s);
        qsizetype headers = 0;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (lines[i] == lines[0])
                ++headers;
            else
                QVERIFY2(cell.match(lines[i]).hasMatch(), qPrintable(lines[i]));
        }
        QCOMPARE(headers, qsizetype(2));   // header repeated per Log press
        // The samples are 5 ms apart: compare the first two time cells.
        const QStringList a = lines[1].split(u'\t'), b = lines[2].split(u'\t');
        const QDateTime ta = QDateTime::fromString(a[0].simplified().section(u'.', 0, 0), u"M/d/yyyy HH:mm:ss"_s);
        QVERIFY(ta.isValid());
        QVERIFY(a[0].mid(a[0].indexOf(u'.') + 1) != b[0].mid(b[0].indexOf(u'.') + 1));
    }

    void dmmSlowerAiKeepsEveryReading()
    {
        // AI at 1 S/s is far below the DMM rate: every DMM reading must still reach the file.
        QTemporaryDir dir;
        LogController ctl;
        ctl.initialize();
        ctl.setFolder(dir.path());
        ctl.setSamplingRate(1);
        ctl.setDmmEnabled(true);
        ctl.startRecording();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.recording(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(ctl.dmm()->count() >= 12, 15000);
        const QString path = ctl.recordFile();
        ctl.stop();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.isIdle(), 3000);
        const qsizetype readings = ctl.dmm()->count();
        const LogFile log = readLog(path);
        QCOMPARE(log.header, (QStringList{u"Time (s)"_s, u"AI 0 (V)"_s, u"DMM time (s)"_s, u"DMM DC Voltage (V)"_s}));
        qsizetype dmmCells = 0;
        for (const QStringList& row : log.rows) {
            QCOMPARE(row.size(), 4);
            if (!row[3].isEmpty())
                ++dmmCells;
        }
        // The chart may hold one reading the file did not get (Stop between reading and queueing).
        QVERIFY2(dmmCells >= readings - 1, qPrintable(u"%1 in the file, %2 read"_s.arg(dmmCells).arg(readings)));
    }

    void refusesWithoutChannels()
    {
        LogController ctl;
        ctl.initialize();
        ctl.setAi0Enabled(false);
        ctl.run();
        QVERIFY(ctl.isIdle());
        QVERIFY(!ctl.hasError());                // a refused run, not a device rejection
        QVERIFY(!ctl.inputWarning().isEmpty());
    }
};

QTEST_MAIN(TstLog)
#include "tst_log.moc"
