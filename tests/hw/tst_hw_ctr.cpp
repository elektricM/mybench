// Counter/Timer instrument on the real myDAQ: FREQOUT (seen toggling on DIO 4, every FREQOUT
// choice the panel offers), pulse train (DIO 3 toggles, live update, finite Done), edge count and
// frequency with open inputs, capability greying of the modes the native driver cannot do yet.
// With the jumper dio4-dio1 the counter measures FREQOUT: frequency and period exact to 0.1 %.
#include "ctrcontroller.h"
#include "hwfixture.h"

#include <QThread>

#include <cmath>

using namespace Qt::StringLiterals;
using mybench::instruments::CtrController;

namespace {

// Samples the DIO levels: which of `line`'s levels were seen.
QString toggles(int line, int reads = 60)
{
    bool seen[2] = {false, false};
    auto dev = mybench::DeviceHub::instance()->device();
    for (int i = 0; i < reads && dev; ++i) {
        seen[(dev->dioRead() >> line) & 1] = true;
        QThread::msleep(7);
    }
    return QStringLiteral("%1%2").arg(seen[0] ? "low" : "").arg(seen[1] ? (seen[0] ? "+high" : "high") : "");
}

}  // namespace

class TestHwCtr : public QObject
{
    Q_OBJECT
    std::unique_ptr<CtrController> m_ctr;

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_ctr = std::make_unique<CtrController>();
        m_ctr->initialize();
    }
    void cleanup()
    {
        if (m_ctr && !m_ctr->isIdle())
            hw::stopAndWait(*m_ctr);
        m_ctr.reset();
    }

    // Every FREQOUT choice of the panel: the device must produce what the panel shows.
    void freqOutChoices()
    {
        m_ctr->setCounterFunction(CtrController::CounterOff);
        m_ctr->setFreqOutEnabled(true);
        QStringList wrong;
        for (int i = 0; i < m_ctr->freqOutChoices().size(); ++i) {
            m_ctr->selectFreqOut(i);
            const double shown = m_ctr->coercedFreqOut();
            m_ctr->run();
            QVERIFY(hw::waitFor([&] { return m_ctr->freqOutActual() > 0 || m_ctr->hasError() || m_ctr->isIdle(); }, 3000));
            const double actual = m_ctr->freqOutActual();
            const QString dio4 = shown <= 100e3 ? toggles(4, 30) : u"(too fast to sample)"_s;
            if (actual != shown || m_ctr->hasError())
                wrong << u"%1 -> %2 (%3)"_s.arg(m_ctr->freqOutChoices()[i]).arg(actual).arg(m_ctr->errorText());
            if (shown <= 100e3 && i % 4 == 0)
                hw::report(u"FREQOUT %1"_s.arg(m_ctr->freqOutChoices()[i]), u"actual %1 Hz, DIO4 %2"_s.arg(actual).arg(dio4));
            if (shown <= 100e3 && !hw::simulated())
                QCOMPARE(dio4, u"low+high"_s);
            hw::stopAndWait(*m_ctr);
        }
        hw::report(u"FREQOUT choices"_s, u"%1 of %2 not produced as shown: %3"_s.arg(wrong.size())
                                             .arg(m_ctr->freqOutChoices().size()).arg(wrong.join(u"; "_s)));
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(u"; "_s)));
    }

    void pulseTrain()
    {
        m_ctr->setCounterFunction(CtrController::PulseTrain);
        m_ctr->setPulseFrequency(5);
        m_ctr->setDutyCycle(0.5);
        m_ctr->run();
        QVERIFY(hw::waitFor([&] { return m_ctr->outputFrequency() == 5.0 || m_ctr->hasError(); }, 3000));
        HW_VERIFY_NO_ERROR(*m_ctr);
        const QString dio3 = toggles(3, 60);
        hw::report(u"pulse train 5 Hz"_s, u"actual %1 Hz duty %2, DIO3 %3"_s.arg(m_ctr->outputFrequency())
                                              .arg(m_ctr->outputDutyCycle()).arg(dio3));
        if (!hw::simulated())
            QCOMPARE(dio3, u"low+high"_s);
        m_ctr->setPulseFrequency(10);   // live update
        QVERIFY(hw::waitFor([&] { return m_ctr->outputFrequency() == 10.0 || m_ctr->hasError(); }, 3000));
        HW_VERIFY_NO_ERROR(*m_ctr);
        QVERIFY(m_ctr->isRunning());
        QVERIFY(hw::stopAndWait(*m_ctr) >= 0);
    }

    void finitePulses()
    {
        m_ctr->setCounterFunction(CtrController::PulseTrain);
        m_ctr->setPulseGeneration(CtrController::FinitePulses);
        m_ctr->setPulseCount(100);
        m_ctr->setPulseFrequency(1000);
        QElapsedTimer clock;
        clock.start();
        m_ctr->run();
        QVERIFY(hw::waitFor([&] { return m_ctr->isRunning(); }, 2000));
        QVERIFY(hw::waitFor([&] { return m_ctr->isIdle(); }, 5000));
        hw::report(u"finite 100 pulses @ 1 kHz"_s, u"state \"%1\" after %2 ms"_s.arg(m_ctr->outputState()).arg(clock.elapsed()));
        HW_VERIFY_NO_ERROR(*m_ctr);
        QCOMPARE(m_ctr->outputState(), u"Done"_s);
    }

    void countEdgesOpen()
    {
        m_ctr->setMeasurement(CtrController::CountEdges);
        m_ctr->run();
        QVERIFY(hw::waitFor([&] { return m_ctr->statCount() >= 3 || m_ctr->hasError(); }, 5000));
        hw::report(u"count edges, open DIO0"_s, u"value %1 after %2 updates"_s.arg(m_ctr->value()).arg(m_ctr->statCount()));
        HW_VERIFY_NO_ERROR(*m_ctr);
        if (!hw::simulated())
            QCOMPARE(m_ctr->value(), 0.0);
        QVERIFY(hw::stopAndWait(*m_ctr) >= 0);
    }

    // No signal on DIO 1: no value, no error, Stop still quick.
    void frequencyWithoutSignal()
    {
        HW_REQUIRE_REAL_DEVICE();
        if (hw::jumper("dio4-dio1"))
            QSKIP("DIO 1 is jumpered");
        m_ctr->setMeasurement(CtrController::Frequency);
        m_ctr->run();
        QTest::qWait(3000);
        const qint64 stopMs = hw::stopAndWait(*m_ctr);
        hw::report(u"frequency, open DIO1"_s, u"%1 values, status \"%2\", error \"%3\", Stop %4 ms"_s
                                                 .arg(m_ctr->statCount()).arg(m_ctr->statusText(), m_ctr->errorText()).arg(stopMs));
        HW_VERIFY_NO_ERROR(*m_ctr);
        QCOMPARE(m_ctr->statCount(), 0.0);
        QVERIFY(stopMs >= 0 && stopMs < 3000);
    }

    // The panel greys what the backend cannot do (Device::unsupportedFeatures).
    void capabilities()
    {
        QStringList greyed;
        for (int m = CtrController::CountEdges; m <= CtrController::AngularEncoder; ++m)
            if (!m_ctr->measurementSupported(m))
                greyed << m_ctr->measurementNames().value(m);
        hw::report(u"counter capabilities"_s, u"greyed: %1"_s.arg(greyed.join(u", "_s)));
        QVERIFY(m_ctr->measurementSupported(CtrController::CountEdges));
        QVERIFY(m_ctr->measurementSupported(CtrController::Frequency));
        QVERIFY(m_ctr->measurementSupported(CtrController::Period));
        for (int m = CtrController::CountEdges; m <= CtrController::AngularEncoder; ++m) {
            if (m_ctr->measurementSupported(m))
                continue;
            m_ctr->setMeasurement(CtrController::Measurement(m));
            m_ctr->run();   // refused up front with a warning, never a device error
            QVERIFY(m_ctr->isIdle());
            QVERIFY(!m_ctr->hasError());
        }
    }

    void measureFreqOut()
    {
        HW_REQUIRE_JUMPER("dio4-dio1");
        for (auto [measurement, name] : {std::pair{CtrController::Frequency, "frequency"}, std::pair{CtrController::Period, "period"},
                                         std::pair{CtrController::PulseWidth, "pulse width"}, std::pair{CtrController::SemiPeriod, "semi-period"}}) {
            m_ctr->setMeasurement(measurement);
            m_ctr->setFreqOutEnabled(true);
            m_ctr->setFreqOutFrequency(10000);
            m_ctr->run();
            QVERIFY(hw::waitFor([&] { return m_ctr->statCount() >= 5 || m_ctr->hasError(); }, 5000));
            HW_VERIFY_NO_ERROR(*m_ctr);
            const double f = m_ctr->freqOutActual();
            const double expected = measurement == CtrController::Frequency ? f
                                    : measurement == CtrController::Period  ? 1.0 / f
                                                                            : 0.5 / f;   // 50 % duty
            hw::report(u"jumper DIO4->DIO1 %1"_s.arg(QLatin1String(name)),
                       u"FREQOUT %1 Hz -> %2 (mean %3, sd %4)"_s.arg(f).arg(m_ctr->valueText())
                           .arg(m_ctr->statMean()).arg(m_ctr->statDeviation()));
            QVERIFY(std::abs(m_ctr->value() / expected - 1.0) < 1e-3);
            QVERIFY(hw::stopAndWait(*m_ctr) >= 0);
        }
    }
};

QTEST_GUILESS_MAIN(TestHwCtr)
#include "tst_hw_ctr.moc"
