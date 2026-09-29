// Resource exclusivity across instruments (docs/device.md): the myDAQ has one analog-input task
// (AI 0/AI 1 and the audio inputs share the converters) and one analog-output task. Instruments
// that need the same resource never fail with "The device rejected the settings": the arbiter
// pauses the holder with a reason, and combinations that can never work are prevented in the UI
// (Scope and Audio Equalizer move the other side to the same kind).
#include "aeqcontroller.h"
#include "bodecontroller.h"
#include "dsacontroller.h"
#include "gencontroller.h"
#include "logcontroller.h"
#include "scopecontroller.h"

#include <mybench/devicehub.h>
#include <mybench/notifications.h>
#include <mybench/resourcearbiter.h>

#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace mybench;
using namespace mybench::instruments;

namespace {

void noRejection(const InstrumentController& c)
{
    QVERIFY2(!c.hasError(), qPrintable(c.title() + u": "_s + c.errorText()));
    QVERIFY(!c.errorText().contains(u"rejected"_s));
}

}  // namespace

class TestExclusivity : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        InstrumentController::setRestoreSavedSettings(false);
        DeviceHub::instance()->select(u"sim:0"_s);
    }

    // Scope on AI 1 only, Logger on AI 0: one AI task, so the later one pauses the earlier with a
    // reason; Resume swaps them back.
    void scopeAi1AndLoggerAi0()
    {
        ScopeController scope;
        scope.initialize();
        LogController log;
        log.initialize();
        QTemporaryDir dir;
        log.setFolder(dir.path());
        log.setAi0Enabled(true);
        log.setAi1Enabled(false);
        log.setDmmEnabled(false);
        scope.channel0()->setEnabled(false);
        scope.channel1()->setEnabled(true);
        scope.channel1()->setSource(ScopeChannel::AI1);

        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(scope.hasRecord(), 5000);
        log.run();
        QTRY_VERIFY_WITH_TIMEOUT(log.isRunning() && !log.isPaused() && scope.isPaused(), 8000);
        QVERIFY(scope.pauseReason().contains(log.title()));
        noRejection(scope);
        noRejection(log);
        QCOMPARE(ResourceArbiter::instance()->holder(ResourceArbiter::AnalogInput), &log);

        scope.resume();
        QTRY_VERIFY_WITH_TIMEOUT(scope.isRunning() && !scope.isPaused() && log.isPaused(), 8000);
        QVERIFY(log.pauseReason().contains(scope.title()));
        noRejection(scope);
        noRejection(log);
        scope.stop();
        QTRY_VERIFY_WITH_TIMEOUT(scope.isIdle(), 3000);
        log.stop();
        QTRY_VERIFY_WITH_TIMEOUT(log.isIdle(), 3000);
    }

    // AI vs audio inputs: the Spectrum Analyzer on AudioInput Left and the Scope on AI 0 conflict
    // for the converters exactly like two AI instruments.
    void dsaAudioAndScopeAi()
    {
        ScopeController scope;
        scope.initialize();
        DsaController dsa;
        dsa.initialize();
        dsa.setSourceChannel(2);   // AudioInput Left
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(scope.hasRecord(), 5000);
        dsa.run();
        QTRY_VERIFY_WITH_TIMEOUT(dsa.isRunning() && scope.isPaused(), 8000);
        noRejection(scope);
        noRejection(dsa);
        dsa.stop();
        QTRY_VERIFY_WITH_TIMEOUT(dsa.isIdle(), 3000);
        scope.stop();
        QTRY_VERIFY_WITH_TIMEOUT(scope.isIdle(), 3000);
    }

    // The Scope keeps both channels on one kind of input.
    void scopeNeverMixesKinds()
    {
        ScopeController scope;
        scope.initialize();
        QVERIFY(scope.setChannelEnabled(1, true));
        QVERIFY(scope.setChannelSource(0, ScopeChannel::AudioLeft));
        QVERIFY(scope.channel1()->audio());
        QVERIFY(scope.setChannelSource(1, ScopeChannel::AI1));
        QVERIFY(!scope.channel0()->audio());
        QVERIFY(scope.validationMessage().isEmpty());
    }

    // The Audio Equalizer follows the same rule for its inputs and outputs, and no combination of
    // the four selectors ever ends in a device rejection.
    void aeqFollowsOneKind()
    {
        AeqController eq;
        eq.initialize();
        eq.setInputLeft(AeqController::SourceAudioInLeft);
        eq.setInputRight(AeqController::SourceAudioInRight);
        eq.chooseInput(0, AeqController::SourceAI0);
        QCOMPARE(eq.inputRight(), AeqController::SourceAI1);
        eq.chooseInput(1, AeqController::SourceAudioInRight);
        QCOMPARE(eq.inputLeft(), AeqController::SourceAudioInLeft);
        eq.chooseOutput(0, AeqController::SinkAO0);
        QCOMPARE(eq.outputRight(), AeqController::SinkAO1);
        eq.chooseOutput(1, AeqController::SinkAudioOutRight);
        QCOMPARE(eq.outputLeft(), AeqController::SinkAudioOutLeft);
        // A side set to None does not drag the other.
        eq.chooseInput(1, AeqController::SourceNone);
        eq.chooseInput(0, AeqController::SourceAI1);
        QCOMPARE(eq.inputRight(), AeqController::SourceNone);
        for (int l = 0; l < 5; ++l) {
            for (int r = 0; r < 5; ++r) {
                eq.chooseInput(0, l);
                eq.chooseInput(1, r);
                QVERIFY2(!eq.routingError().contains(u"share the converters"_s), qPrintable(eq.routingError()));
            }
        }
    }

    // Saved settings (session restore writes the properties one at a time) are restored exactly:
    // the follow rule belongs to the UI selection only, no notice, no rewriting.
    void aeqRestoreDoesNotRewrite()
    {
        AeqController saved;
        saved.initialize();
        saved.chooseInput(0, AeqController::SourceAI0);
        saved.chooseInput(1, AeqController::SourceAI1);
        saved.chooseOutput(0, AeqController::SinkAO0);
        saved.chooseOutput(1, AeqController::SinkAO1);
        const QJsonObject settings = saved.saveSettings();
        AeqController fresh;
        fresh.initialize();
        QCOMPARE(fresh.inputLeft(), AeqController::SourceAudioInLeft);   // the defaults are audio
        QSignalSpy toasts(Notifications::instance(), &Notifications::countChanged);
        fresh.restoreSettings(settings);
        QCOMPARE(fresh.inputLeft(), AeqController::SourceAI0);
        QCOMPARE(fresh.inputRight(), AeqController::SourceAI1);
        QCOMPARE(fresh.outputLeft(), AeqController::SinkAO0);
        QCOMPARE(fresh.outputRight(), AeqController::SinkAO1);
        QVERIFY(fresh.routingError().isEmpty());
        QCOMPARE(toasts.count(), 0);
    }

    // Instruments that need different resources run side by side: nothing is paused.
    void compatibleInstrumentsDoNotPause()
    {
        ScopeController scope;
        scope.initialize();
        GenController gen;
        gen.initialize();
        LogController log;
        log.initialize();
        QTemporaryDir dir;
        log.setFolder(dir.path());
        log.setAi0Enabled(false);
        log.setAi1Enabled(false);
        log.setDmmEnabled(true);   // the DMM only: no analog input
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(scope.hasRecord(), 5000);
        gen.run();
        QTRY_VERIFY_WITH_TIMEOUT(gen.isRunning(), 5000);
        log.run();
        QTRY_VERIFY_WITH_TIMEOUT(log.isRunning(), 5000);
        QTest::qWait(300);
        QVERIFY(scope.isRunning() && !scope.isPaused());
        QVERIFY(gen.isRunning() && !gen.isPaused());
        QVERIFY(log.isRunning() && !log.isPaused());
        noRejection(scope);
        noRejection(gen);
        noRejection(log);
        log.stop();
        gen.stop();
        scope.stop();
        QTRY_VERIFY_WITH_TIMEOUT(log.isIdle() && gen.isIdle() && scope.isIdle(), 5000);
    }

    void aeqRunMovesAiHolder()
    {
        ScopeController scope;
        scope.initialize();
        AeqController eq;
        eq.initialize();
        eq.setInputLeft(AeqController::SourceAI0);
        eq.setInputRight(AeqController::SourceAI1);
        eq.setOutputLeft(AeqController::SinkAO0);
        eq.setOutputRight(AeqController::SinkAO1);
        scope.run();
        QTRY_VERIFY_WITH_TIMEOUT(scope.hasRecord(), 5000);
        eq.run();
        QTRY_VERIFY_WITH_TIMEOUT(eq.isRunning() && scope.isPaused(), 8000);
        noRejection(scope);
        noRejection(eq);
        eq.stop();
        QTRY_VERIFY_WITH_TIMEOUT(eq.isIdle(), 3000);
        scope.stop();
        QTRY_VERIFY_WITH_TIMEOUT(scope.isIdle(), 3000);
    }

    // AO: the Generator and the Bode Analyzer share the single AO task.
    void generatorAndBode()
    {
        GenController gen;
        gen.initialize();
        BodeController bode;
        bode.initialize();
        gen.run();
        QTRY_VERIFY_WITH_TIMEOUT(gen.isRunning(), 5000);
        bode.run();
        QTRY_VERIFY_WITH_TIMEOUT(bode.isRunning() && gen.isPaused(), 8000);
        noRejection(gen);
        noRejection(bode);
        QVERIFY(gen.pauseReason().contains(bode.title()));
        bode.stop();
        QTRY_VERIFY_WITH_TIMEOUT(bode.isIdle(), 5000);
        gen.stop();
        QTRY_VERIFY_WITH_TIMEOUT(gen.isIdle(), 3000);
    }
};

QTEST_MAIN(TestExclusivity)
#include "tst_exclusivity.moc"
