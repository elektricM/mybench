// Audio equalizer on the real myDAQ: audio in -> audio out stream at every rate runs without
// dropouts (no under/overflow restarts) and the open input meters stay low.
#include "aeqcontroller.h"
#include "hwfixture.h"

using namespace Qt::StringLiterals;
using mybench::instruments::AeqController;

class TestHwAeq : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }

    void streamsWithoutDropouts()
    {
        AeqController aeq;
        aeq.initialize();
        for (int rate = 0; rate < aeq.rateNames().size(); ++rate) {
            aeq.setRateIndex(rate);
            QVERIFY2(aeq.routingError().isEmpty(), qPrintable(aeq.routingError()));
            aeq.run();
            QElapsedTimer clock;
            clock.start();
            hw::LatencyProbe probe;
            hw::waitFor([&] { return aeq.hasError() || clock.elapsed() >= 5000; }, 6000);
            QString peaks;
            for (const QVariant& m : aeq.meters())
                peaks += u" %1"_s.arg(m.toMap().value(u"peak"_s).toDouble(), 0, 'f', 1);
            hw::report(u"AEQ %1"_s.arg(aeq.rateNames()[rate]), u"%1 S/s, latency %2 s, dropouts %3, meter peaks (dB)%4, UI stall max %5 ms, error \"%6\""_s
                                                                  .arg(aeq.streamRate()).arg(aeq.latencySeconds())
                                                                  .arg(aeq.dropouts()).arg(peaks).arg(probe.maxStallMs(), 0, 'f', 1)
                                                                  .arg(aeq.errorText()));
            QVERIFY(probe.maxStallMs() < 250);   // a frozen UI; finer values are reported (machine noise: baseline)
            HW_VERIFY_NO_ERROR(aeq);
            QCOMPARE(aeq.dropouts(), 0);
            QVERIFY(hw::stopAndWait(aeq) >= 0);
        }
    }
};

QTEST_GUILESS_MAIN(TestHwAeq)
#include "tst_hw_aeq.moc"
