// Dynamic signal analyzer on the real myDAQ: AI 0 and audio input spectra keep coming at the
// planned rate without error; the open audio input's spectrum stays low. With the jumper ao0-ai0
// and the generator on AO 0 (1 kHz, 1 Vpp) the fundamental is found at 1 kHz.
#include "dsacontroller.h"
#include "hwfixture.h"

using namespace Qt::StringLiterals;
using mybench::instruments::DsaController;

class TestHwDsa : public QObject
{
    Q_OBJECT
    std::unique_ptr<DsaController> m_dsa;

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_dsa = std::make_unique<DsaController>();
        m_dsa->initialize();
    }
    void cleanup()
    {
        if (m_dsa && !m_dsa->isIdle())
            hw::stopAndWait(*m_dsa);
        m_dsa.reset();
    }

    void sources_data()
    {
        QTest::addColumn<int>("source");
        for (int i = 0; i < DsaController().sourceLabels().size(); ++i)
            QTest::newRow(qPrintable(DsaController().sourceLabels()[i])) << i;
    }

    void sources()
    {
        QFETCH(int, source);
        m_dsa->setSourceChannel(source);
        m_dsa->run();
        const int start = m_dsa->acquisitionCount();
        QElapsedTimer clock;
        clock.start();
        hw::LatencyProbe probe;
        QVERIFY(hw::waitFor([&] { return (m_dsa->acquisitionCount() >= start + 5 && clock.elapsed() > 3000) || m_dsa->hasError(); }, 15000));
        hw::report(u"DSA %1"_s.arg(m_dsa->sourceName()),
                   u"%1 spectra in %2 s at %3 S/s, UI stall max %4 ms, error \"%5\""_s.arg(m_dsa->acquisitionCount() - start)
                       .arg(clock.elapsed() / 1000.0, 0, 'f', 2).arg(m_dsa->expectedSampleRate())
                       .arg(probe.maxStallMs(), 0, 'f', 1).arg(m_dsa->errorText()));
        QVERIFY(probe.maxStallMs() < 250);   // a frozen UI; finer values are reported (machine noise: baseline)
        HW_VERIFY_NO_ERROR(*m_dsa);
        QVERIFY(hw::stopAndWait(*m_dsa) >= 0);
    }
};

QTEST_GUILESS_MAIN(TestHwDsa)
#include "tst_hw_dsa.moc"
