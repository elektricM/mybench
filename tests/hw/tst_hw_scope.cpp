// Oscilloscope on the real myDAQ: every Time/Div (planned rate and record length delivered, record
// cadence), AI0+AI1 and audio inputs, open-input levels, Edge trigger that must not fire on an
// open input, Single/Run/Stop timing. With the jumper ao0-ai0 the scope triggers on the
// generator's square wave (AO 0) at the requested level and position.
#include "gencontroller.h"
#include "genchannel.h"
#include "hwfixture.h"
#include "scopecontroller.h"
#include "scopeobjects.h"

#include <cmath>

using namespace Qt::StringLiterals;
using mybench::instruments::GenChannel;
using mybench::instruments::GenController;
using mybench::instruments::ScopeChannel;
using mybench::instruments::ScopeController;

class TestHwScope : public QObject
{
    Q_OBJECT
    std::unique_ptr<ScopeController> m_scope;

    int waitRecords(int n, int timeoutMs)
    {
        const int start = m_scope->acquisitionCount();
        hw::waitFor([&] { return m_scope->acquisitionCount() >= start + n || m_scope->hasError(); }, timeoutMs);
        return m_scope->acquisitionCount() - start;
    }

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_scope = std::make_unique<ScopeController>();
        m_scope->initialize();
    }
    void cleanup()
    {
        if (m_scope && !m_scope->isIdle())
            hw::stopAndWait(*m_scope);
        m_scope.reset();
    }

    // Each Time/Div with both AI channels: the record has the planned rate and length, records
    // keep coming (no stall), no error.
    void timeBases()
    {
        m_scope->setChannelEnabled(1, true);
        m_scope->run();
        for (double tdiv : m_scope->timeDivValues()) {
            m_scope->setTimePerDiv(tdiv);
            const double screen = 10 * tdiv;
            QVERIFY(hw::waitFor([&] {
                return m_scope->hasError() || (m_scope->hasRecord() && m_scope->recordRate() == m_scope->plannedRate()
                                               && m_scope->recordLength() == m_scope->plannedRecordLength());
            }, int(4000 + 3 * screen * 1000)));
            HW_VERIFY_NO_ERROR(*m_scope);
            QElapsedTimer clock;
            clock.start();
            hw::LatencyProbe probe;
            const int n = waitRecords(3, int(3000 + 4 * screen * 1000));
            const double perRecord = clock.elapsed() / 1000.0 / std::max(1, n);
            hw::report(u"Scope %1 AI0+AI1"_s.arg(m_scope->timeDivLabel()),
                       u"%1 S/s x %2, %3 s/record (screen %4 s), CH0 rms %5 V, UI stall max %6 ms"_s.arg(m_scope->recordRate())
                           .arg(m_scope->recordLength()).arg(perRecord, 0, 'f', 3).arg(screen)
                           .arg(m_scope->channel0()->rms(), 0, 'f', 4).arg(probe.maxStallMs(), 0, 'f', 1));
            QVERIFY(probe.maxStallMs() < 250);   // a frozen UI; finer values are reported (machine noise: baseline)
            QCOMPARE(n, 3);
            QVERIFY(perRecord < screen * 1.5 + 0.25);   // no stutter beyond the acquisition itself
        }
        QVERIFY(hw::stopAndWait(*m_scope) >= 0);
    }

    void audioInputs()
    {
        QVERIFY(m_scope->setChannelSource(0, ScopeChannel::AudioLeft));
        QVERIFY(m_scope->setChannelSource(1, ScopeChannel::AudioRight));
        m_scope->setChannelEnabled(1, true);
        m_scope->setTimePerDiv(1e-3);
        m_scope->run();
        QCOMPARE(waitRecords(3, 5000), 3);
        HW_VERIFY_NO_ERROR(*m_scope);
        hw::report(u"Scope audio L+R (open)"_s, u"%1 S/s, rms L %2 mV, R %3 mV"_s.arg(m_scope->recordRate())
                                                   .arg(m_scope->channel0()->rms() * 1e3, 0, 'f', 3)
                                                   .arg(m_scope->channel1()->rms() * 1e3, 0, 'f', 3));
        if (!hw::simulated()) {
            QVERIFY(m_scope->channel0()->rms() < 5e-3);
            QVERIFY(m_scope->channel1()->rms() < 5e-3);
        }
        QVERIFY(hw::stopAndWait(*m_scope) >= 0);
    }

    // An Edge trigger far outside the open input's level never fires: Timeout, no record.
    void edgeTriggerDoesNotFireOnOpenInput()
    {
        HW_REQUIRE_REAL_DEVICE();
        if (hw::jumper("ao0-ai0"))
            QSKIP("AI 0 is jumpered");
        m_scope->setTimePerDiv(1e-3);
        m_scope->setTriggerType(ScopeController::Edge);
        m_scope->setTriggerLevel(8.0);
        m_scope->channel0()->setVoltsPerDiv(2.0);
        m_scope->run();
        QVERIFY(hw::waitFor([&] { return m_scope->timedOut() || m_scope->hasError(); }, 8000));
        HW_VERIFY_NO_ERROR(*m_scope);
        hw::report(u"Scope Edge 8 V on open AI0"_s, u"timed out %1, waiting %2, record %3"_s.arg(m_scope->timedOut())
                                                        .arg(m_scope->waitingForTrigger()).arg(m_scope->hasRecord()));
        QVERIFY(!m_scope->hasRecord());
        QVERIFY(hw::stopAndWait(*m_scope) >= 0);
    }

    void singleAndStopTiming()
    {
        m_scope->setTimePerDiv(20e-3);   // 0.2 s screen
        QElapsedTimer clock;
        clock.start();
        m_scope->runOnce();
        QVERIFY(hw::waitFor([&] { return m_scope->isIdle() && m_scope->hasRecord(); }, 5000));
        const qint64 singleMs = clock.elapsed();
        m_scope->setTimePerDiv(200e-3);   // 2 s screen
        m_scope->run();
        QVERIFY(hw::waitFor([&] { return m_scope->state() == ScopeController::Running; }, 3000));
        QTest::qWait(500);
        const qint64 stopMs = hw::stopAndWait(*m_scope);
        hw::report(u"Scope timing"_s, u"Single at 20 ms/div: %1 ms; Stop during a 2 s record: %2 ms"_s.arg(singleMs).arg(stopMs));
        HW_VERIFY_NO_ERROR(*m_scope);
        QVERIFY(singleMs < 1500);
        QVERIFY(stopMs >= 0 && stopMs < 500);
    }

    // Jumper AO 0 -> AI 0+: trigger on the generator's 1 kHz square at 0 V, rising.
    void triggerOnGenerator()
    {
        HW_REQUIRE_JUMPER("ao0-ai0");
        GenController gen;
        gen.initialize();
        GenChannel* a = gen.channel0();
        a->setMode(GenChannel::Function);
        a->setShape(GenChannel::Square);
        a->setFrequency(1000);
        a->setAmplitude(1.0);
        a->setOffset(0);
        gen.run();
        QVERIFY(hw::waitFor([&] { return a->generating() || gen.hasError(); }, 5000));
        HW_VERIFY_NO_ERROR(gen);
        m_scope->setTimePerDiv(200e-6);
        m_scope->setTriggerType(ScopeController::Edge);
        m_scope->setTriggerLevel(0.0);
        m_scope->channel0()->setVoltsPerDiv(0.2);
        m_scope->run();
        QCOMPARE(waitRecords(5, 5000), 5);
        HW_VERIFY_NO_ERROR(*m_scope);
        const double t50 = 0.5 * m_scope->screenTime();
        hw::report(u"jumper Scope trigger on AO0 square"_s,
                   u"trigger at %1 s (expect %2), Vpp %3 V, f %4 Hz"_s.arg(m_scope->triggerTime()).arg(t50)
                       .arg(m_scope->channel0()->peakToPeak(), 0, 'f', 3).arg(m_scope->channel0()->frequency(), 0, 'f', 1));
        QVERIFY(std::abs(m_scope->triggerTime() - t50) < 2.0 / m_scope->recordRate());
        QVERIFY(std::abs(m_scope->channel0()->peakToPeak() - 1.0) < 0.05);
        QVERIFY(std::abs(m_scope->channel0()->frequency() - 1000) < 30);   // two periods on screen
        QVERIFY(hw::stopAndWait(*m_scope) >= 0);
        QVERIFY(hw::stopAndWait(gen) >= 0);
    }
};

QTEST_GUILESS_MAIN(TestHwScope)
#include "tst_hw_scope.moc"
