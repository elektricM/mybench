// Device-level hardware facts through the app's device layer (DeviceHub + USB backend): readiness,
// noise floor of the open inputs, reservation errors and recovery, abort cycles, DMM during
// streaming, reset, AO -> AI loopback (jumper ao0-ai0).
#include "hwfixture.h"

#include <QSignalSpy>

#include <cmath>
#include <numeric>

using namespace mydaq;
using namespace Qt::StringLiterals;

namespace {

double mean(const std::vector<double>& v)
{
    return std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
}

double stddev(const std::vector<double>& v)
{
    const double m = mean(v);
    double q = 0;
    for (double x : v)
        q += (x - m) * (x - m);
    return std::sqrt(q / double(v.size() - 1));
}

}  // namespace

class TestHwDevice : public QObject
{
    Q_OBJECT
    DevicePtr m_dev;

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        m_dev = hw::selectDevice();
        QVERIFY(m_dev);
    }

    void info()
    {
        const DeviceInfo i = m_dev->info();
        hw::report(u"info"_s, u"%1, serial %2, max AI %3 S/s, max AO %4 S/s"_s.arg(i.productType, i.serialNumber)
                                  .arg(m_dev->maxAiRate()).arg(m_dev->maxAoRate()));
        QCOMPARE(m_dev->maxAiRate(), 200000.0);
        QCOMPARE(m_dev->maxAoRate(), 200000.0);
        QVERIFY(mybench::DeviceHub::instance()->isConnected());
    }

    // Open inputs: AI stays in range; the AC-coupled audio input is quiet.
    void openInputNoise()
    {
        AiConfig c;
        c.channels = {{AiChannel::AudioInLeft, 2.0}, {AiChannel::AudioInRight, 2.0}};
        c.sampleRate = 48000;
        c.samplesPerChannel = 4800;
        auto task = m_dev->createAiTask(c);
        task->start();
        const auto d = task->read(4800, 3000);
        task.reset();
        hw::report(u"audio in noise"_s, u"L sd %1 mV, R sd %2 mV"_s.arg(stddev(d[0]) * 1e3, 0, 'f', 3)
                                            .arg(stddev(d[1]) * 1e3, 0, 'f', 3));
        if (!hw::simulated()) {
            QVERIFY(stddev(d[0]) < 5e-3);
            QVERIFY(stddev(d[1]) < 5e-3);
        }
        for (double range : {10.0, 2.0}) {
            AiConfig a;
            a.channels = {{AiChannel::AI0, range}, {AiChannel::AI1, range}};
            a.sampleRate = 100000;
            a.samplesPerChannel = 10000;
            auto t = m_dev->createAiTask(a);
            t->start();
            const auto v = t->read(10000, 3000);
            hw::report(u"AI open ±%1 V"_s.arg(range), u"AI0 mean %1 V sd %2 mV, AI1 mean %3 V sd %4 mV"_s
                                                          .arg(mean(v[0]), 0, 'f', 4).arg(stddev(v[0]) * 1e3, 0, 'f', 2)
                                                          .arg(mean(v[1]), 0, 'f', 4).arg(stddev(v[1]) * 1e3, 0, 'f', 2));
            QVERIFY(std::abs(mean(v[0])) < range * 1.06);
            QVERIFY(std::abs(mean(v[1])) < range * 1.06);
        }
    }

    // Finite acquisition timing: 1000 samples at 1 kS/s take about one second.
    void finiteTiming()
    {
        AiConfig a;
        a.channels = {{AiChannel::AI0, 10.0}};
        a.sampleRate = 1000;
        a.samplesPerChannel = 1000;
        auto t = m_dev->createAiTask(a);
        QElapsedTimer clock;
        clock.start();
        t->start();
        const auto v = t->read(1000, 5000);
        const double s = clock.elapsed() / 1000.0;
        hw::report(u"AI finite 1000 @ 1 kS/s"_s, u"%1 s"_s.arg(s, 0, 'f', 3));
        QCOMPARE(v[0].size(), size_t(1000));
        QVERIFY(s > 0.95 && s < 1.5);
    }

    // A second AI task is refused while one exists; afterwards everything works again.
    void reservationAndRecovery()
    {
        AiConfig a;
        a.channels = {{AiChannel::AI0, 10.0}};
        a.sampleRate = 10000;
        a.mode = SampleMode::Continuous;
        auto t = m_dev->createAiTask(a);
        bool busy = false;
        try {
            m_dev->createAiTask(a);
        } catch (const DeviceError& e) {
            busy = e.code() == DeviceError::ResourceBusy;
        }
        QVERIFY(busy);
        busy = false;
        try {
            m_dev->aiReadStatic(AiChannel::AI0, 10.0);
        } catch (const DeviceError& e) {
            busy = e.code() == DeviceError::ResourceBusy;
        }
        QVERIFY(busy);
        t.reset();
        QVERIFY(!std::isnan(hw::readAi0(1)));
    }

    // Start/abort cycles at the maximum rate leave the device usable.
    void abortCycles()
    {
        AiConfig a;
        a.channels = {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}};
        a.sampleRate = 200000;
        a.mode = SampleMode::Continuous;
        for (int i = 0; i < 5; ++i) {
            auto t = m_dev->createAiTask(a);
            t->start();
            QTest::qWait(40 * i);
            t.reset();   // destroy while streaming
        }
        AoConfig o;
        o.channels = {{AoChannel::AO0, 10.0}};
        o.sampleRate = 100000;
        o.mode = SampleMode::Continuous;
        o.regenerate = false;
        for (int i = 0; i < 3; ++i) {
            auto t = m_dev->createAoTask(o);
            t->write({std::vector<double>(20000, 0.0)});
            t->start();
            QTest::qWait(50 * i);
            t.reset();
        }
        a.mode = SampleMode::Finite;
        a.samplesPerChannel = 1000;
        auto t = m_dev->createAiTask(a);
        t->start();
        QCOMPARE(t->read(1000, 2000)[0].size(), size_t(1000));
        t.reset();
        m_dev->aoWriteStatic(AoChannel::AO0, 0.0);
    }

    // The DMM works while AI streams at the maximum rate.
    void dmmDuringStreaming()
    {
        AiConfig a;
        a.channels = {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}};
        a.sampleRate = 200000;
        a.mode = SampleMode::Continuous;
        a.samplesPerChannel = 20000;
        auto t = m_dev->createAiTask(a);
        t->start();
        std::atomic<bool> done{false};
        DmmReading r;
        QString error;
        std::thread th([&] {
            try {
                r = m_dev->dmmRead(DmmFunction::DcVoltage, 20.0);
            } catch (const DeviceError& e) {
                error = e.message();
            }
            done = true;
        });
        int64_t got = 0;
        QElapsedTimer clock;
        clock.start();
        while (!done && clock.elapsed() < 8000)
            got += int64_t(t->read(20000, 2000)[0].size());
        th.join();
        const double rate = double(got) / (clock.elapsed() / 1000.0);
        hw::report(u"DMM during 2x200k AI"_s, u"DMM %1 V (%2), AI %3 S/s/ch"_s.arg(r.value).arg(error.isEmpty() ? u"ok"_s : error)
                                                  .arg(rate, 0, 'f', 0));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        if (!hw::simulated())
            QVERIFY(rate > 190000);
    }

    // DeviceHub reset: outputs to 0 V, DIO to inputs, device usable afterwards.
    void reset()
    {
        auto* hub = mybench::DeviceHub::instance();
        m_dev->dioSetDirection(0x01, true);
        QSignalSpy spy(hub, &mybench::DeviceHub::resetFinished);
        QElapsedTimer clock;
        clock.start();
        hub->reset();
        QVERIFY(spy.wait(10000));
        hw::report(u"reset"_s, u"%1 s, ok %2"_s.arg(clock.elapsed() / 1000.0, 0, 'f', 2).arg(spy.at(0).at(0).toBool()));
        QVERIFY(spy.at(0).at(0).toBool());
        QCOMPARE(m_dev->dioDirection(), uint8_t(0));
        QVERIFY(!std::isnan(hw::readAi0(1)));
    }

    // Jumper AO 0 -> AI 0+: static AO values arrive on AI0.
    void aoToAiStatic()
    {
        HW_REQUIRE_JUMPER("ao0-ai0");
        for (double v : {0.5, -0.5, 0.25, 0.0}) {
            m_dev->aoWriteStatic(AoChannel::AO0, v);
            QTest::qWait(20);
            const double got = hw::readAi0(10);
            hw::report(u"jumper AO0->AI0"_s, u"AO0 %1 V -> AI0 %2 V"_s.arg(v).arg(got, 0, 'f', 4));
            if (!hw::simulated() || v != 0.0)   // the simulator shows its test signal at 0 V
                QVERIFY(std::abs(got - v) < 0.01);
        }
    }

    void cleanupTestCase()
    {
        if (m_dev) {
            m_dev->aoWriteStatic(AoChannel::AO0, 0.0);
            m_dev->aoWriteStatic(AoChannel::AO1, 0.0);
            m_dev->dioSetDirection(0xff, false);
        }
    }
};

QTEST_GUILESS_MAIN(TestHwDevice)
#include "tst_hw_device.moc"
