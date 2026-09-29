// Digital Reader/Writer on the real myDAQ: every line as output (read back through the port),
// every line as input (open lines read low), ramp timing, lines back to inputs on Stop. With the
// jumper dio4-dio1, DIO 1 and DIO 4 are never both outputs; DIO 4 -> DIO 1 is tested as a loopback.
#include "diocontroller.h"
#include "hwfixture.h"

using namespace Qt::StringLiterals;
using mybench::instruments::DioController;

class TestHwDio : public QObject
{
    Q_OBJECT
    std::unique_ptr<DioController> m_dio;

    static int outputLines() { return hw::jumper("dio4-dio1") ? 0xED : 0xFF; }   // never DIO 1 + DIO 4

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_dio = std::make_unique<DioController>();
        m_dio->initialize();
    }
    void cleanup()
    {
        if (m_dio && !m_dio->isIdle())
            hw::stopAndWait(*m_dio);
        m_dio.reset();
        mybench::DeviceHub::instance()->device()->dioSetDirection(0xff, false);
    }

    void allOutputsReadBack()
    {
        const int mask = outputLines();
        m_dio->setOutputMask(mask);
        m_dio->run();
        QVERIFY(hw::waitFor([&] { return m_dio->drivenMask() == mask || m_dio->hasError(); }, 3000));
        HW_VERIFY_NO_ERROR(*m_dio);
        int bad = 0;
        for (int value : {0x55, 0xAA, 0x0F, 0xF0, 0xFF, 0x00}) {
            m_dio->setManualValue(value & ((1 << m_dio->outputWidth()) - 1));
            QVERIFY(hw::waitFor([&] { return m_dio->hasWritten() && m_dio->writtenValue() == m_dio->manualValue(); }, 2000));
            const bool ok = hw::waitFor([&] { return (m_dio->lineStates() & mask) == (m_dio->writtenLevels() & mask); }, 2000);
            bad += !ok;
            hw::report(u"DIO out mask 0x%1"_s.arg(mask, 2, 16, QLatin1Char('0')),
                       u"wrote 0x%1 read 0x%2"_s.arg(m_dio->writtenLevels(), 2, 16, QLatin1Char('0'))
                           .arg(m_dio->lineStates() & mask, 2, 16, QLatin1Char('0')));
        }
        QCOMPARE(bad, 0);
        QCOMPARE(m_dio->readbackErrors(), 0);
        QVERIFY(hw::stopAndWait(*m_dio) >= 0);
        auto dev = mybench::DeviceHub::instance()->device();
        QCOMPARE(int(dev->dioDirection()), mask);   // ELVISmx: Stop keeps driving the outputs
        m_dio->stop();                              // a second Stop releases them
        QVERIFY(hw::waitFor([&] { return dev->dioDirection() == 0; }, 2000));
    }

    void allInputsOpen()
    {
        m_dio->setReadPreset(int(mybench::instruments::dio::Lines0to7));
        m_dio->run();
        QVERIFY(hw::waitFor([&] { return m_dio->hasReading() || m_dio->hasError(); }, 3000));
        HW_VERIFY_NO_ERROR(*m_dio);
        hw::report(u"DIO all inputs (open)"_s, u"0x%1"_s.arg(m_dio->lineStates(), 2, 16, QLatin1Char('0')));
        if (!hw::simulated())
            QCOMPARE(m_dio->lineStates(), 0);
        QVERIFY(hw::stopAndWait(*m_dio) >= 0);
    }

    // Ramp at the default 100 ms step: about 10 values per second.
    void rampTiming()
    {
        m_dio->setPattern(DioController::Ramp);
        m_dio->run();
        QVERIFY(hw::waitFor([&] { return m_dio->hasWritten(); }, 3000));
        const int first = m_dio->sequenceStep();
        QElapsedTimer clock;
        clock.start();
        int steps = 0, last = first;
        hw::waitFor([&] {
            if (m_dio->sequenceStep() != last) {
                ++steps;
                last = m_dio->sequenceStep();
            }
            return clock.elapsed() >= 3000;
        }, 4000);
        hw::report(u"DIO ramp 100 ms"_s, u"%1 steps in 3 s"_s.arg(steps));
        HW_VERIFY_NO_ERROR(*m_dio);
        QVERIFY(steps >= 25 && steps <= 32);
        QVERIFY(hw::stopAndWait(*m_dio) >= 0);
    }

    void loopbackDio4ToDio1()
    {
        HW_REQUIRE_JUMPER("dio4-dio1");
        m_dio->setOutputMask(0x10);   // DIO 4 out; DIO 1 stays an input
        QCOMPARE(m_dio->outputMask() & 0x02, 0);
        m_dio->run();
        QVERIFY(hw::waitFor([&] { return m_dio->drivenMask() == 0x10; }, 3000));
        for (int level : {1, 0, 1, 0}) {
            m_dio->setManualValue(level);
            QVERIFY(hw::waitFor([&] { return ((m_dio->lineStates() >> 1) & 1) == level; }, 2000));
            hw::report(u"jumper DIO4->DIO1"_s, u"DIO4 %1 -> DIO1 %2"_s.arg(level).arg((m_dio->lineStates() >> 1) & 1));
        }
        QVERIFY(hw::stopAndWait(*m_dio) >= 0);
    }
};

QTEST_GUILESS_MAIN(TestHwDio)
#include "tst_hw_dio.moc"
