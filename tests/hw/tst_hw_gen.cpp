// Function generator on the real myDAQ: streamed (default) and regenerated functions on one and
// two channels up to 200 kS/s without underflow, arbitrary Run Once, DC level, audio outputs,
// Run/Stop cycles and Stop latency, outputs back to 0 V. With the jumper ao0-ai0 the AO0 signal
// is measured on AI0 (amplitude, frequency, DC level, 0 V after Stop). Outputs stay within ±1 V.
#include "gencontroller.h"
#include "genchannel.h"
#include "hwfixture.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using mybench::instruments::GenChannel;
using mybench::instruments::GenController;

namespace {

struct Measured {
    double peak = 0, frequency = 0, mean = 0;
};

// AI0 at 100 kS/s for 0.1 s (a device task, beside the generator's AO task).
Measured measureAi0()
{
    Measured m;
    auto dev = mybench::DeviceHub::instance()->device();
    if (!dev)
        return m;
    mydaq::AiConfig c;
    c.channels = {{mydaq::AiChannel::AI0, 2.0}};
    c.sampleRate = 100000;
    c.samplesPerChannel = 10000;
    auto task = dev->createAiTask(c);
    task->start();
    const auto v = task->read(10000, 3000)[0];
    double sum = 0;
    int crossings = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        m.peak = std::max(m.peak, std::abs(v[i]));
    }
    m.mean = sum / double(v.size());
    for (size_t i = 1; i < v.size(); ++i)
        crossings += (v[i - 1] < m.mean && v[i] >= m.mean);
    m.frequency = crossings / 0.1;
    return m;
}

}  // namespace

class TestHwGen : public QObject
{
    Q_OBJECT
    std::unique_ptr<GenController> m_gen;

    // Runs for `ms` and checks the generator keeps running without an error.
    void runFor(int ms, const QString& what)
    {
        QVERIFY(hw::waitFor([&] { return m_gen->channel0()->generating() || m_gen->hasError(); }, 5000));
        QElapsedTimer clock;
        clock.start();
        hw::LatencyProbe probe;
        hw::waitFor([&] { return m_gen->hasError() || clock.elapsed() >= ms; }, ms + 1000);
        hw::report(what, u"strategy \"%1\", task rate %2 S/s, throughput %3 S/s, UI stall max %4 ms, error \"%5\""_s
                             .arg(m_gen->strategyText()).arg(m_gen->taskRate()).arg(m_gen->throughput(), 0, 'f', 0)
                             .arg(probe.maxStallMs(), 0, 'f', 1).arg(m_gen->errorText()));
        QVERIFY(probe.maxStallMs() < 250);   // a frozen UI; finer values are reported (machine noise: baseline)
        HW_VERIFY_NO_ERROR(*m_gen);
        QVERIFY(m_gen->isRunning());
    }

    void configureSine(GenChannel* ch, double hz, double vpp)
    {
        ch->setMode(GenChannel::Function);
        ch->setShape(GenChannel::Sine);
        ch->setFrequency(hz);
        ch->setAmplitude(vpp);
        ch->setOffset(0);
    }

private slots:
    void initTestCase()
    {
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }
    void init()
    {
        m_gen = std::make_unique<GenController>();
        m_gen->initialize();
        configureSine(m_gen->channel0(), 1000, 1.0);
    }
    void cleanup()
    {
        if (m_gen && !m_gen->isIdle())
            hw::stopAndWait(*m_gen);
        m_gen.reset();
    }

    void streamOneChannel()
    {
        m_gen->run();
        runFor(5000, u"Gen AO0 1 kHz sine, Auto"_s);
        QCOMPARE(m_gen->strategy(), GenController::Streaming);
        if (hw::jumper("ao0-ai0")) {
            const Measured m = measureAi0();
            hw::report(u"jumper AO0->AI0 sine"_s, u"peak %1 V, %2 Hz, mean %3 V"_s.arg(m.peak, 0, 'f', 4)
                                                      .arg(m.frequency).arg(m.mean, 0, 'f', 4));
            QVERIFY(std::abs(m.peak - 0.5) < 0.02);
            QVERIFY(std::abs(m.frequency - 1000) <= 20);
        }
        const qint64 stopMs = hw::stopAndWait(*m_gen);
        hw::report(u"Gen Stop"_s, u"%1 ms"_s.arg(stopMs));
        QVERIFY(stopMs >= 0 && stopMs < 1000);
        if (hw::jumper("ao0-ai0")) {
            const double v = hw::readAi0(10);
            hw::report(u"jumper AO0 after Stop"_s, u"%1 V"_s.arg(v, 0, 'f', 4));
            if (!hw::simulated())   // the simulator shows its test signal at 0 V
                QVERIFY(std::abs(v) < 0.01);
        }
    }

    // Both channels streamed at the maximum rate (2 x 200 kS/s): no underflow.
    void streamTwoChannelsMaxRate()
    {
        configureSine(m_gen->channel1(), 2000, 1.0);
        m_gen->setOutput(0, true);
        m_gen->setOutput(1, true);
        runFor(10000, u"Gen AO0+AO1 streamed"_s);
        QCOMPARE(m_gen->strategy(), GenController::Streaming);
        QCOMPARE(m_gen->taskRate(), 200000.0);
    }

    void regenerate()
    {
        m_gen->setEngineMode(GenController::Regenerate);   // ELVISmx's looped buffer
        m_gen->run();
        runFor(5000, u"Gen AO0 regenerated"_s);
        QCOMPARE(m_gen->strategy(), GenController::RegeneratedBuffer);
        if (hw::jumper("ao0-ai0")) {
            const Measured m = measureAi0();
            hw::report(u"jumper AO0->AI0 regenerated"_s, u"peak %1 V, %2 Hz"_s.arg(m.peak, 0, 'f', 4).arg(m.frequency));
            QVERIFY(std::abs(m.peak - 0.5) < 0.02);
        }
    }

    void dcLevel()
    {
        GenChannel* a = m_gen->channel0();
        a->setMode(GenChannel::Dc);
        a->setDcLevel(0.5);
        m_gen->run();
        QVERIFY(hw::waitFor([&] { return a->generating() || m_gen->hasError(); }, 5000));
        HW_VERIFY_NO_ERROR(*m_gen);
        hw::report(u"Gen DC level"_s, u"strategy \"%1\""_s.arg(m_gen->strategyText()));
        if (hw::jumper("ao0-ai0")) {
            QTest::qWait(100);
            const double v = hw::readAi0(10);
            hw::report(u"jumper AO0 DC 0.5 V"_s, u"AI0 %1 V"_s.arg(v, 0, 'f', 4));
            QVERIFY(std::abs(v - 0.5) < 0.01);
        }
        QVERIFY(hw::stopAndWait(*m_gen) >= 0);
    }

    void arbitraryRunOnce()
    {
        m_gen->channel0()->setMode(GenChannel::Arbitrary);
        m_gen->loadLibraryWaveform(0, 1);   // sine document, 1000 points at 100 kS/s
        m_gen->channel0()->setGain(0.5);
        QVERIFY(m_gen->channel0()->hasWaveform());
        QElapsedTimer clock;
        clock.start();
        m_gen->runOnce();
        QVERIFY(hw::waitFor([&] { return m_gen->isIdle() && clock.elapsed() > 50; }, 5000));
        hw::report(u"Gen arbitrary Run Once"_s, u"%1 points, idle after %2 ms, error \"%3\""_s
                                                    .arg(m_gen->channel0()->waveformPoints()).arg(clock.elapsed())
                                                    .arg(m_gen->errorText()));
        HW_VERIFY_NO_ERROR(*m_gen);
    }

    void audioOutputs()
    {
        m_gen->setOutputPair(GenController::AudioOutputs);
        configureSine(m_gen->channel0(), 1000, 1.0);
        configureSine(m_gen->channel1(), 440, 1.0);
        m_gen->setOutput(0, true);
        m_gen->setOutput(1, true);
        runFor(3000, u"Gen audio out L+R"_s);
    }

    void runStopCycles()
    {
        qint64 worst = 0;
        for (int i = 0; i < 5; ++i) {
            m_gen->run();
            QVERIFY(hw::waitFor([&] { return m_gen->channel0()->generating() || m_gen->hasError(); }, 5000));
            HW_VERIFY_NO_ERROR(*m_gen);
            QTest::qWait(100 + 50 * i);
            const qint64 ms = hw::stopAndWait(*m_gen);
            QVERIFY(ms >= 0);
            worst = std::max(worst, ms);
        }
        hw::report(u"Gen 5 Run/Stop cycles"_s, u"worst Stop %1 ms"_s.arg(worst));
        QVERIFY(worst < 1000);
    }
};

QTEST_GUILESS_MAIN(TestHwGen)
#include "tst_hw_gen.moc"
