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
            } else if (ranges[i].startsWith(u"200m"_s) && (function == DmmController::DcVoltage || function == DmmController::AcVoltage)) {
                // A floating high-impedance input drifts by tenths of a volt: the 200 mV ranges
                // may overload with open leads (seen on 036CD1C6 with DAQmx's settings too).
                continue;
            } else if (function == DmmController::AcVoltage) {
                // an open AC input picks up mains: ~0.2 V rms seen on the 2 V range
                QVERIFY(!m_dmm->overload());
                QVERIFY2(m_dmm->fullScaleFraction() < 0.25, qPrintable(m_dmm->readingText()));
            } else {
                // floating DC input / open current shunt: close to zero (5 % of full scale)
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

    // Autorange with a floating DC V input lands on a small range (200 mV, or 2 V: a floating
    // input overloads 200 mV now and then, and each range change costs ~2.6 s while the meter
    // re-settles, so readings are slow while it switches).
    void autorange()
    {
        m_dmm->setSelectedFunction(DmmController::DcVoltage);
        m_dmm->setAutoRange(true);
        m_dmm->run();
        QElapsedTimer clock;
        clock.start();
        QVERIFY(hw::waitFor([&] { return m_dmm->readingSerial() >= 2; }, 45000));
        hw::report(u"DMM DC V auto timing"_s, u"%1 readings after %2 s"_s.arg(m_dmm->readingSerial())
                                                   .arg(clock.elapsed() / 1000.0, 0, 'f', 1));
        hw::report(u"DMM DC V auto"_s, u"settled on %1 with \"%2 %3\" after %4 readings"_s
                                            .arg(m_dmm->activeRangeLabel(), m_dmm->readingText(), m_dmm->readingUnit())
                                            .arg(m_dmm->readingSerial()));
        HW_VERIFY_NO_ERROR(*m_dmm);
        if (!hw::simulated())
            QVERIFY2(m_dmm->activeRangeLabel() == u"200mV"_s || m_dmm->activeRangeLabel() == u"2V"_s,
                     qPrintable(m_dmm->activeRangeLabel()));
        QVERIFY(hw::stopAndWait(*m_dmm) >= 0);
    }

    // A floating DC V input used to hunt between 200 mV and 2 V (2 readings in 27 s: every range
    // change costs ~2.6 s). It must settle and then read at the meter's 2/s.
    void autorangeSteady()
    {
        m_dmm->setSelectedFunction(DmmController::DcVoltage);
        m_dmm->setAutoRange(true);
        QStringList trail;
        QElapsedTimer clock;
        connect(m_dmm.get(), &DmmController::readingChanged, this, [&] {
            trail << u"%1 s %2 %3"_s.arg(clock.elapsed() / 1000.0, 0, 'f', 1).arg(m_dmm->activeRangeLabel(), m_dmm->readingText());
        });
        clock.start();
        m_dmm->run();
        QVERIFY(hw::waitFor([&] { return clock.elapsed() >= 40000; }, 41000));
        QVERIFY(hw::stopAndWait(*m_dmm) >= 0);
        int changes = 0;
        for (int i = 1; i < trail.size(); ++i)
            changes += trail[i].section(u' ', 2, 2) != trail[i - 1].section(u' ', 2, 2);
        hw::report(u"DMM DC V auto steady"_s, u"%1 readings in 40 s, %2 range changes: %3"_s.arg(trail.size()).arg(changes)
                                                   .arg(trail.join(u"; "_s)));
        HW_VERIFY_NO_ERROR(*m_dmm);
        if (!hw::simulated()) {
            QVERIFY2(changes <= 6, qPrintable(QString::number(changes)));
            QVERIFY2(trail.size() >= 20, qPrintable(QString::number(trail.size())));
        }
    }

    // The 60 V range resolves 10 mV: an open input reads about -0.02 V, not "0.0".
    void range60VShowsTenMillivolts()
    {
        m_dmm->setSelectedFunction(DmmController::DcVoltage);
        m_dmm->setAutoRange(false);
        m_dmm->setRangeIndex(m_dmm->rangeLabels().indexOf(u"60V"_s));
        QVERIFY(single() >= 0);
        HW_VERIFY_NO_ERROR(*m_dmm);
        hw::report(u"DMM DC V 60 V"_s, u"\"%1 %2\" raw %3"_s.arg(m_dmm->readingText(), m_dmm->readingUnit()).arg(m_dmm->rawReading()));
        QVERIFY2(m_dmm->readingText().section(u'.', 1).size() == 2, qPrintable(m_dmm->readingText()));
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
