// Bode analyzer on the real myDAQ (AO 0 stimulus + AI 0/AI 1 at the same time, the AI+AO stream
// pair): a sweep completes with every point, in about the planned time, without error; Stop
// during a sweep is quick. Without wiring the gain values are noise (reported only).
#include "bodecontroller.h"
#include "hwfixture.h"

using namespace Qt::StringLiterals;
using mybench::InstrumentController;
using mybench::instruments::BodeController;

class TestHwBode : public QObject
{
    Q_OBJECT
    std::unique_ptr<BodeController> m_bode;

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_bode = std::make_unique<BodeController>();
        m_bode->initialize();
        m_bode->setPeakAmplitude(0.5);   // within ±1 V
    }
    void cleanup()
    {
        if (m_bode && !m_bode->isIdle())
            hw::stopAndWait(*m_bode);
        m_bode.reset();
    }

    void sweepCompletes()
    {
        m_bode->setStartFrequency(10);
        m_bode->setStopFrequency(10000);
        m_bode->setStepsPerDecade(5);
        const int planned = m_bode->plannedPoints();
        const double plannedSeconds = m_bode->plannedSeconds();
        QElapsedTimer clock;
        clock.start();
        m_bode->run();
        QVERIFY(hw::waitFor([&] { return m_bode->state() == InstrumentController::Idle && clock.elapsed() > 100; },
                            int(plannedSeconds * 3000 + 20000)));
        const double s = clock.elapsed() / 1000.0;
        hw::report(u"Bode 10 Hz-10 kHz, 5/decade"_s, u"%1/%2 points in %3 s (planned %4 s), error \"%5\", last gain %6 dB"_s
                                                          .arg(m_bode->pointCount()).arg(planned).arg(s, 0, 'f', 2)
                                                          .arg(plannedSeconds, 0, 'f', 2).arg(m_bode->errorText())
                                                          .arg(m_bode->readoutGainDb(), 0, 'f', 1));
        HW_VERIFY_NO_ERROR(*m_bode);
        QCOMPARE(m_bode->pointCount(), planned);
        QVERIFY(s < plannedSeconds * 2 + 3);
    }

    void stopDuringSweep()
    {
        m_bode->setStartFrequency(1);
        m_bode->setStopFrequency(20000);
        m_bode->run();
        QVERIFY(hw::waitFor([&] { return m_bode->pointCount() >= 2 || m_bode->hasError(); }, 20000));
        const qint64 ms = hw::stopAndWait(*m_bode);
        hw::report(u"Bode Stop during sweep"_s, u"%1 ms after %2 points"_s.arg(ms).arg(m_bode->pointCount()));
        HW_VERIFY_NO_ERROR(*m_bode);
        QVERIFY(ms >= 0 && ms < 2000);
    }
};

QTEST_GUILESS_MAIN(TestHwBode)
#include "tst_hw_bode.moc"
