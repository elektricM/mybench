// DMM instrument on the real myDAQ with nothing on the DMM jacks: every function and range the
// open leads can check (V ≈ 0, A ≈ 0, Ω overload, diode OPEN, continuity open), autorange,
// reading rate, Single/Run/Stop timing. Diode and continuity (mode bytes captured from ELVISmx's
// DMM) can only be checked for their open-lead readings without wiring.
#include "dmmcontroller.h"
#include "hwfixture.h"

#include <cmath>

using namespace Qt::StringLiterals;
using mybench::instruments::DmmController;

class TestHwDmm : public QObject
{
    Q_OBJECT
    std::unique_ptr<DmmController> m_dmm;

    // One reading with Single; returns the elapsed ms (-1: none within the timeout).
    qint64 single(int timeoutMs = 15000)
    {
        const int before = m_dmm->readingSerial();
        QElapsedTimer clock;
        clock.start();
        m_dmm->runOnce();
        if (!hw::waitFor([&] { return m_dmm->readingSerial() > before && m_dmm->isIdle(); }, timeoutMs))
            return -1;
        return clock.elapsed();
    }

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_dmm = std::make_unique<DmmController>();
        m_dmm->initialize();
    }
    void cleanup() { m_dmm.reset(); }

    void openLeads_data()
    {
        QTest::addColumn<int>("function");
        QTest::newRow("DC V") << int(DmmController::DcVoltage);
        QTest::newRow("AC V") << int(DmmController::AcVoltage);
        QTest::newRow("DC A") << int(DmmController::DcCurrent);
        QTest::newRow("AC A") << int(DmmController::AcCurrent);
        QTest::newRow("Ohm") << int(DmmController::Resistance);
    }

    // Every fixed range with open leads.
    void openLeads()
    {
        QFETCH(int, function);
        m_dmm->setSelectedFunction(DmmController::MeterFunction(function));
        m_dmm->setAutoRange(false);
        const QStringList ranges = m_dmm->rangeLabels();
        QVERIFY(!ranges.isEmpty());
        for (int i = 0; i < ranges.size(); ++i) {
            m_dmm->setRangeIndex(i);
            const qint64 ms = single();
            HW_VERIFY_NO_ERROR(*m_dmm);
            QVERIFY2(ms >= 0, qPrintable(ranges[i]));
            hw::report(u"DMM %1 %2"_s.arg(m_dmm->functionName(), ranges[i]),
                       u"\"%1 %2\" raw %3, overload %4, %5 ms"_s.arg(m_dmm->readingText(), m_dmm->readingUnit())
                           .arg(m_dmm->rawReading()).arg(m_dmm->overload()).arg(ms));
            QCOMPARE(m_dmm->activeRangeLabel(), ranges[i]);
            if (hw::simulated())
                continue;
            if (function == DmmController::Resistance) {
                QVERIFY2(m_dmm->overload(), "open leads must overload every resistance range");
            } else {
                // floating voltage input / open current shunt: close to zero (5 % of full scale)
                QVERIFY(!m_dmm->overload());
                QVERIFY2(m_dmm->fullScaleFraction() < 0.05, qPrintable(m_dmm->readingText()));
            }
        }
    }

    void diodeOpen()
    {
        m_dmm->setSelectedFunction(DmmController::Diode);
        QVERIFY(single() >= 0);
        HW_VERIFY_NO_ERROR(*m_dmm);
        hw::report(u"DMM diode"_s, u"\"%1\" status %2 raw %3"_s.arg(m_dmm->readingText(), m_dmm->diodeStatus())
                                                             .arg(m_dmm->rawReading()));
        if (!hw::simulated())
            QCOMPARE(m_dmm->diodeStatus(), u"OPEN"_s);
    }

    void continuityOpen()
    {
        m_dmm->setSelectedFunction(DmmController::Continuity);
        QVERIFY(single() >= 0);
        HW_VERIFY_NO_ERROR(*m_dmm);
        hw::report(u"DMM continuity"_s, u"\"%1\" closed %2 raw %3"_s.arg(m_dmm->readingText())
                                                                  .arg(m_dmm->continuityClosed()).arg(m_dmm->rawReading()));
        if (!hw::simulated())
            QVERIFY(!m_dmm->continuityClosed());
    }

    // Autorange with a floating DC V input settles on the smallest range.
    void autorange()
    {
        m_dmm->setSelectedFunction(DmmController::DcVoltage);
        m_dmm->setAutoRange(true);
        m_dmm->run();
        QVERIFY(hw::waitFor([&] { return m_dmm->readingSerial() >= 4; }, 30000));
        hw::report(u"DMM DC V auto"_s, u"settled on %1 with \"%2 %3\" after %4 readings"_s
                                            .arg(m_dmm->activeRangeLabel(), m_dmm->readingText(), m_dmm->readingUnit())
                                            .arg(m_dmm->readingSerial()));
        HW_VERIFY_NO_ERROR(*m_dmm);
        if (!hw::simulated())
            QCOMPARE(m_dmm->activeRangeLabel(), u"200mV"_s);
        QVERIFY(hw::stopAndWait(*m_dmm) >= 0);
    }

    // Continuous readings: rate and Stop latency.
    void runRateAndStop()
    {
        m_dmm->setSelectedFunction(DmmController::DcVoltage);
        m_dmm->setAutoRange(false);
        m_dmm->setRangeIndex(2);   // 20 V
        m_dmm->run();
        QVERIFY(hw::waitFor([&] { return m_dmm->readingSerial() >= 1; }, 10000));
        const int first = m_dmm->readingSerial();
        QElapsedTimer clock;
        clock.start();
        hw::waitFor([&] { return clock.elapsed() >= 10000; }, 11000);
        const double rate = (m_dmm->readingSerial() - first) / (clock.elapsed() / 1000.0);
        const qint64 stopMs = hw::stopAndWait(*m_dmm);
        hw::report(u"DMM DC V 20 V run"_s, u"%1 readings/s, Stop took %2 ms"_s.arg(rate, 0, 'f', 2).arg(stopMs));
        HW_VERIFY_NO_ERROR(*m_dmm);
        QVERIFY(rate >= 1.0);
        QVERIFY(stopMs >= 0 && stopMs < 3000);
    }
};

QTEST_GUILESS_MAIN(TestHwDmm)
#include "tst_hw_dmm.moc"
