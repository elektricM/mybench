// Data logger on the real myDAQ: AI 0 + AI 1 (+ DMM) logged at the default rate; the samples on
// the chart match rate x time (no lost buffers), the DMM column fills, recording to a file works.
#include "hwfixture.h"
#include "logcontroller.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using mybench::instruments::LogController;

class TestHwLog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }

    void logsWithoutLoss()
    {
        LogController log;
        log.initialize();
        log.setAi0Enabled(true);
        log.setAi1Enabled(true);
        log.setDmmEnabled(true);
        QTemporaryDir dir;
        log.setFolder(dir.path());
        log.setRecordOnRun(true);
        log.run();
        QVERIFY(hw::waitFor([&] { return log.runSeconds() >= 6 || log.hasError(); }, 15000));
        const double seconds = log.runSeconds();
        const double expected = log.coercedRate() * seconds;
        const qint64 ai0 = log.ai0()->count();
        hw::report(u"Logger AI0+AI1+DMM"_s, u"%1 S/s for %2 s: AI0 %3 samples (expect %4), DMM %5 points, %6 rows to %7, error \"%8\""_s
                                                .arg(log.coercedRate()).arg(seconds, 0, 'f', 2).arg(ai0).arg(expected, 0, 'f', 0)
                                                .arg(log.dmm()->count()).arg(log.recordRows()).arg(log.recordFile())
                                                .arg(log.errorText()));
        HW_VERIFY_NO_ERROR(log);
        QVERIFY(std::abs(double(ai0) - expected) < 0.02 * expected + 2 * log.coercedRate() * 0.1);
        QVERIFY(log.dmm()->count() >= 1);
        QVERIFY(log.recordRows() > 0);
        QVERIFY(hw::stopAndWait(log) >= 0);
    }
};

QTEST_GUILESS_MAIN(TestHwLog)
#include "tst_hw_log.moc"
