// Generation engine: ELVISmx buffer/sweep rules, strategy choice, and the Static / Regenerate /
// Stream strategies running against the simulator (AO 0 is wired to AI 0).
#include "genengine.h"

#include <mydaq/sim_device.h>

#include <QTest>

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

using namespace mybench::instruments::gen;
using namespace std::chrono_literals;

namespace {

// Host driven by the test thread.
class TestHost final : public EngineHost
{
public:
    bool stopRequested() const override { return m_stop; }
    bool sleepMs(int ms) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return !m_stop;
    }
    std::optional<EngineSettings> takeSettings() override
    {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_pending, std::nullopt);
    }
    void publishStatus(const EngineStatus& status) override
    {
        std::lock_guard lock(m_mutex);
        m_statuses.push_back(status);
    }
    void warn(const QString& message) override
    {
        std::lock_guard lock(m_mutex);
        m_warnings.append(message);
    }

    void post(const EngineSettings& settings)
    {
        std::lock_guard lock(m_mutex);
        m_pending = settings;
    }
    void stop() { m_stop = true; }
    EngineStatus last() const
    {
        std::lock_guard lock(m_mutex);
        return m_statuses.empty() ? EngineStatus{} : m_statuses.back();
    }
    std::vector<EngineStatus> statuses() const
    {
        std::lock_guard lock(m_mutex);
        return m_statuses;
    }
    QStringList warnings() const
    {
        std::lock_guard lock(m_mutex);
        return m_warnings;
    }

private:
    mutable std::mutex m_mutex;
    std::atomic<bool> m_stop = false;
    std::optional<EngineSettings> m_pending;
    std::vector<EngineStatus> m_statuses;
    QStringList m_warnings;
};

// Runs an engine in a thread.
struct Runner {
    Runner(mydaq::Device& device, const EngineSettings& settings, Engine::StreamTiming timing = {})
        : engine(device, host)
    {
        engine.setStreamTiming(timing);
        thread = std::thread([this, settings] {
            try {
                result = engine.run(settings);
            } catch (const mydaq::DeviceError& e) {
                error = e.message();
                code = e.code();
            }
            finished = true;
        });
    }
    ~Runner() { join(); }
    void join()
    {
        host.stop();
        if (thread.joinable())
            thread.join();
    }
    bool waitFinished(int ms)
    {
        for (int i = 0; i < ms / 5 && !finished; ++i)
            std::this_thread::sleep_for(5ms);
        if (finished && thread.joinable())
            thread.join();
        return finished;
    }
    bool waitStatus(const std::function<bool(const EngineStatus&)>& predicate, int ms = 2000)
    {
        for (int i = 0; i < ms / 5; ++i) {
            if (predicate(host.last()))
                return true;
            std::this_thread::sleep_for(5ms);
        }
        return false;
    }

    TestHost host;
    Engine engine;
    std::thread thread;
    std::atomic<bool> finished = false;
    EngineResult result;
    QString error;
    mydaq::DeviceError::Code code = mydaq::DeviceError::Generic;
};

EngineSettings functionOn(double frequency, double vpp = 2.0, int channel = 0)
{
    EngineSettings s;
    ChannelSettings& c = s.channels[size_t(channel)];
    c.mode = ChannelMode::Function;
    c.active = true;
    c.startId = 1;
    c.function.frequency = frequency;
    c.function.amplitude = vpp;
    return s;
}

std::vector<double> acquireAi0(mydaq::Device& device, double seconds, double rate = 200000)
{
    mydaq::AiConfig config;
    config.channels = {{mydaq::AiChannel::AI0, 10.0}};
    config.sampleRate = rate;
    config.mode = mydaq::SampleMode::Finite;
    config.samplesPerChannel = int64_t(seconds * rate);
    auto task = device.createAiTask(config);
    task->start();
    return task->read(config.samplesPerChannel, 5000)[0];
}

double estimateFrequency(const std::vector<double>& y, double rate)
{
    // Rising crossings of the mean with hysteresis.
    double mean = 0;
    for (double v : y)
        mean += v;
    mean /= double(y.size());
    int crossings = 0;
    size_t first = 0, last = 0;
    bool low = y[0] < mean;
    for (size_t i = 1; i < y.size(); ++i) {
        if (low && y[i] > mean + 0.05) {
            low = false;
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        } else if (!low && y[i] < mean - 0.05) {
            low = true;
        }
    }
    return crossings > 1 ? double(crossings - 1) * rate / double(last - first) : 0.0;
}

// Silences AI 0's built-in test signal (shown whenever AO 0 rests at 0 V) so 0 V reads 0 V.
void quiet(mydaq::SimDevice& sim)
{
    mydaq::SimTestSignal silent;
    silent.amplitude = 0;
    silent.harmonics.clear();
    sim.setTestSignal(mydaq::AiChannel::AI0, silent);
}

double peakToPeak(const std::vector<double>& y)
{
    const auto [lo, hi] = std::minmax_element(y.begin(), y.end());
    return *hi - *lo;
}

// Device wrapper that refuses streaming AO tasks, or makes the first write of a stream underflow.
class FaultyDevice final : public mydaq::Device
{
public:
    explicit FaultyDevice(mydaq::Device& inner) : m_inner(inner) {}
    bool refuseStreaming = false;
    std::atomic<int> underflows = 0;   // number of streaming writes to fail

    mydaq::DeviceInfo info() const override { return m_inner.info(); }
    bool isConnected() const override { return m_inner.isConnected(); }
    double maxAiRate() const override { return m_inner.maxAiRate(); }
    double maxAoRate() const override { return m_inner.maxAoRate(); }
    double coerceAiRate(double r) const override { return m_inner.coerceAiRate(r); }
    double coerceAoRate(double r) const override { return m_inner.coerceAoRate(r); }
    std::unique_ptr<mydaq::AiTask> createAiTask(const mydaq::AiConfig& c) override { return m_inner.createAiTask(c); }
    std::unique_ptr<mydaq::AoTask> createAoTask(const mydaq::AoConfig& c) override
    {
        if (refuseStreaming && !c.regenerate && c.mode == mydaq::SampleMode::Continuous)
            throw mydaq::DeviceError(mydaq::DeviceError::NotSupported, QStringLiteral("no streaming"));
        auto task = m_inner.createAoTask(c);
        if (c.regenerate)
            return task;
        return std::make_unique<Task>(std::move(task), underflows);
    }
    double aiReadStatic(mydaq::AiChannel ch, double range) override { return m_inner.aiReadStatic(ch, range); }
    void aoWriteStatic(mydaq::AoChannel ch, double v) override { m_inner.aoWriteStatic(ch, v); }
    std::vector<mydaq::DmmRange> dmmRanges(mydaq::DmmFunction f) const override { return m_inner.dmmRanges(f); }
    mydaq::DmmReading dmmRead(mydaq::DmmFunction f, double r) override { return m_inner.dmmRead(f, r); }
    void dioSetDirection(uint8_t m, bool o) override { m_inner.dioSetDirection(m, o); }
    uint8_t dioDirection() const override { return m_inner.dioDirection(); }
    void dioWrite(uint8_t v, uint8_t m) override { m_inner.dioWrite(v, m); }
    uint8_t dioRead() override { return m_inner.dioRead(); }
    void reset() override { m_inner.reset(); }

private:
    class Task final : public mydaq::AoTask
    {
    public:
        Task(std::unique_ptr<mydaq::AoTask> inner, std::atomic<int>& underflows) : m_task(std::move(inner)), m_underflows(underflows) {}
        const mydaq::AoConfig& config() const override { return m_task->config(); }
        double sampleRate() const override { return m_task->sampleRate(); }
        void write(const std::vector<std::vector<double>>& data, int timeoutMs) override
        {
            if (m_task->isRunning() && m_underflows > 0) {
                --m_underflows;
                throw mydaq::DeviceError(mydaq::DeviceError::Underflow, QStringLiteral("ran dry"));
            }
            m_task->write(data, timeoutMs);
        }
        void start() override { m_task->start(); }
        void stop() override { m_task->stop(); }
        bool isRunning() const override { return m_task->isRunning(); }
        bool waitUntilDone(int ms) override { return m_task->waitUntilDone(ms); }
        int64_t spaceAvailable() const override { return m_task->spaceAvailable(); }

    private:
        std::unique_ptr<mydaq::AoTask> m_task;
        std::atomic<int>& m_underflows;
    };
    mydaq::Device& m_inner;
};

}  // namespace

class TestGenEngine : public QObject
{
    Q_OBJECT

private slots:
    void fgenBufferPlan_data()
    {
        QTest::addColumn<double>("frequency");
        QTest::addColumn<double>("rate");
        QTest::addColumn<int>("length");
        QTest::newRow("1 Hz") << 1.0 << 10000.0 << 10000;
        QTest::newRow("20 Hz") << 20.0 << 200000.0 << 10000;
        QTest::newRow("100 Hz") << 100.0 << 200000.0 << 2000;
        QTest::newRow("1 kHz") << 1000.0 << 200000.0 << 200;
        QTest::newRow("20 kHz") << 20000.0 << 200000.0 << 10;
    }
    void fgenBufferPlan()
    {
        QFETCH(double, frequency);
        QFETCH(double, rate);
        QFETCH(int, length);
        const FgenPlan plan = planFgenBuffer(frequency, 200000, nullptr);
        QCOMPARE(plan.rate, rate);
        QCOMPARE(plan.length, size_t(length));
        QCOMPARE(plan.frequency, frequency);
    }

    void fgenCoercedFrequency()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        const FgenPlan plan = planFgenBuffer(3.0, sim.maxAoRate(), [&](double r) { return sim.coerceAoRate(r); });
        QCOMPARE(plan.length, size_t(10000));
        QCOMPARE(plan.rate, sim.coerceAoRate(30000));
        QCOMPARE(plan.frequency, plan.rate / 10000.0);   // the actual frequency, shown to the user
        // Never above the maximum rate (round(Rmax / f) may round up).
        const FgenPlan high = planFgenBuffer(16000, 200000, nullptr);
        QVERIFY(high.rate <= 200000);
        QCOMPARE(high.length, size_t(12));
    }

    void sweepPlan()
    {
        SweepSettings s;   // 100 -> 1000 step 100, 1000 ms
        SweepPlan p = makeSweep(s);
        QCOMPARE(p.frequencies.size(), size_t(10));
        QCOMPARE(p.frequencies.back(), 1000.0);
        QCOMPARE(p.stepDuration, 1.0);
        s.stop = 950;
        QCOMPARE(makeSweep(s).frequencies.back(), 900.0);   // Stop only when it falls on a step
        s.start = 1000;
        s.stop = 100;
        p = makeSweep(s);
        QCOMPARE(p.frequencies.front(), 1000.0);
        QCOMPARE(p.frequencies.back(), 100.0);
        s.start = s.stop = 500;
        QCOMPARE(makeSweep(s).frequencies.size(), size_t(1));
        s.intervalMs = 300;
        QCOMPARE(makeSweep(s).stepDuration, 0.5);    // ELVISmx: whole 250 ms blocks
        s.intervalMs = 1;
        QCOMPARE(makeSweep(s).stepDuration, 0.25);
        s.blockTiming = false;
        s.intervalMs = 300;
        QCOMPARE(makeSweep(s).stepDuration, 0.3);
    }

    void dds()
    {
        for (double f : {0.2, 1.0, 100.0, 1234.5678, 20000.0}) {
            const uint64_t inc = phaseIncrement(f, 200000);
            QVERIFY(std::abs(ddsFrequency(inc, 200000) - f) / f < 1e-12);
        }
    }

    void strategyChoice()
    {
        EngineSettings s = functionOn(100);
        QCOMPARE(chooseStrategy(s, StrategyKind::None), StrategyKind::Regenerate);
        QCOMPARE(chooseStrategy(s, StrategyKind::Stream), StrategyKind::Stream);   // no downgrade
        s.preference = EnginePreference::Stream;
        QCOMPARE(chooseStrategy(s, StrategyKind::None), StrategyKind::Stream);
        s.preference = EnginePreference::Regenerate;
        QCOMPARE(chooseStrategy(s, StrategyKind::Stream), StrategyKind::Regenerate);
        s.preference = EnginePreference::Auto;

        EngineSettings two = functionOn(100);
        two.channels[1] = functionOn(100, 1, 1).channels[1];
        QCOMPARE(chooseStrategy(two, StrategyKind::None), StrategyKind::Regenerate);   // same frequency
        two.channels[1].function.frequency = 300;
        QCOMPARE(chooseStrategy(two, StrategyKind::None), StrategyKind::Stream);

        EngineSettings sweep = functionOn(100);
        sweep.channels[0].sweep = true;
        QCOMPARE(chooseStrategy(sweep, StrategyKind::None), StrategyKind::Stream);

        EngineSettings dc;
        dc.channels[0].mode = ChannelMode::Dc;
        dc.channels[0].active = true;
        QCOMPARE(chooseStrategy(dc, StrategyKind::None), StrategyKind::Static);
        dc.channels[0].active = false;
        QCOMPARE(chooseStrategy(dc, StrategyKind::None), StrategyKind::None);

        EngineSettings arb;
        arb.channels[0].mode = ChannelMode::Arbitrary;
        arb.channels[0].active = true;
        arb.channels[0].arb.waveform = std::make_shared<const std::vector<double>>(100, 1.0);
        QCOMPARE(chooseStrategy(arb, StrategyKind::None), StrategyKind::Regenerate);
        arb.channels[1] = arb.channels[0];
        arb.channels[1].arb.waveform = std::make_shared<const std::vector<double>>(50, 1.0);
        QCOMPARE(chooseStrategy(arb, StrategyKind::None), StrategyKind::Stream);   // different lengths
        arb.channels[1] = functionOn(10, 1, 1).channels[1];
        QCOMPARE(chooseStrategy(arb, StrategyKind::None), StrategyKind::Stream);   // Function + Arbitrary
        arb.channels[0].arb.once = true;
        arb.channels[1].mode = ChannelMode::Dc;
        QCOMPARE(chooseStrategy(arb, StrategyKind::None), StrategyKind::Stream);   // Run Once + DC
        QCOMPARE(chooseStrategy(arb, StrategyKind::None, false), StrategyKind::Stream);
    }

    void regenerateFunction()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        Runner run(sim, functionOn(1000, 2.0));
        QVERIFY(run.waitStatus([](const EngineStatus& s) { return s.channels[0].state == ChannelState::Running; }));
        const EngineStatus status = run.host.last();
        QCOMPARE(status.strategy, StrategyKind::Regenerate);
        QCOMPARE(status.taskRate, 200000.0);
        QCOMPARE(status.taskChannels, 1);   // ELVISmx: only the generating channel
        QCOMPARE(status.channels[0].frequency, 1000.0);
        const auto y = acquireAi0(sim, 0.02);
        QVERIFY(std::abs(peakToPeak(y) - 2.0) < 0.05);
        QVERIFY(std::abs(estimateFrequency(y, 200000) - 1000) < 20);
        run.join();
        QVERIFY(run.error.isEmpty());
        QVERIFY(std::abs(sim.aiReadStatic(mydaq::AiChannel::AI0, 10)) < 0.01);   // safe state 0 V
    }

    void streamLiveChanges()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        EngineSettings s = functionOn(1000, 2.0);
        s.channels[1] = functionOn(300, 1.0, 1).channels[1];
        Runner run(sim, s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return st.strategy == StrategyKind::Stream
                                                                   && st.channels[1].state == ChannelState::Running; }));
        QCOMPARE(run.host.last().taskChannels, 2);
        auto y = acquireAi0(sim, 0.05);
        QVERIFY(std::abs(estimateFrequency(y, 200000) - 1000) < 30);
        // Live frequency change: phase-continuous, no gaps.
        s.channels[0].function.frequency = 2000;
        run.host.post(s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return std::abs(st.channels[0].frequency - 2000) < 1e-6; }));
        std::this_thread::sleep_for(50ms);
        y = acquireAi0(sim, 0.05);
        QVERIFY(std::abs(estimateFrequency(y, 200000) - 2000) < 60);
        double maxStep = 0;
        for (size_t i = 1; i < y.size(); ++i)
            maxStep = std::max(maxStep, std::abs(y[i] - y[i - 1]));
        QVERIFY2(maxStep < 0.2, qPrintable(QString::number(maxStep)));   // 2 Vpp at 2 kHz: <= 0.07 V/sample + noise
        // Stop channel 1 alone: the stream goes on (no downgrade).
        s.channels[1].active = false;
        run.host.post(s);
        std::this_thread::sleep_for(100ms);
        QCOMPARE(run.host.last().strategy, StrategyKind::Stream);
        QVERIFY(!run.finished);
        run.join();
        QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
    }

    void sweepFinishes()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        EngineSettings s = functionOn(100);
        s.channels[0].sweep = true;
        s.channels[0].sweepSettings = {100, 300, 100, 60, false};
        const auto t0 = std::chrono::steady_clock::now();
        Runner run(sim, s);
        QVERIFY(run.waitFinished(3000));
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        QVERIFY(run.error.isEmpty());
        QVERIFY2(elapsed > 0.17 && elapsed < 1.0, qPrintable(QString::number(elapsed)));
        int maxStep = 0;
        bool finished = false;
        for (const EngineStatus& st : run.host.statuses()) {
            maxStep = std::max(maxStep, st.channels[0].sweepStep);
            finished = finished || st.channels[0].sweepFinished;
            if (st.channels[0].state == ChannelState::Sweeping)
                QCOMPARE(st.channels[0].sweepSteps, 3);
        }
        QCOMPARE(maxStep, 3);
        QVERIFY(!run.result.done[0]);
        QVERIFY(std::abs(sim.aiReadStatic(mydaq::AiChannel::AI0, 10)) < 0.01);
    }

    void arbitraryRunOnceHolds()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        EngineSettings s;
        ChannelSettings& c = s.channels[0];
        c.mode = ChannelMode::Arbitrary;
        c.active = true;
        c.startId = 7;
        std::vector<double> ramp(100);
        for (size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = double(i + 1) / 100.0;
        c.arb.waveform = std::make_shared<const std::vector<double>>(ramp);
        c.arb.updateRate = 1000;
        c.arb.once = true;
        c.arb.gain = 2.0;
        for (EnginePreference preference : {EnginePreference::Auto, EnginePreference::Stream}) {
            s.preference = preference;
            Runner run(sim, s);
            QVERIFY(run.waitFinished(3000));
            QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
            QVERIFY(run.result.done[0]);
            QCOMPARE(run.result.held[0], 2.0);
            QCOMPARE(run.host.last().channels[0].state, ChannelState::Done);
            QVERIFY(std::abs(sim.aiReadStatic(mydaq::AiChannel::AI0, 10) - 2.0) < 0.01);   // holds
        }
    }

    void staticDcLevel()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        EngineSettings s;
        s.channels[0].mode = ChannelMode::Dc;
        s.channels[0].active = true;
        s.channels[0].dcLevel = 2.5;
        Runner run(sim, s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return st.strategy == StrategyKind::Static; }));
        QVERIFY(std::abs(sim.aiReadStatic(mydaq::AiChannel::AI0, 10) - 2.5) < 0.01);
        s.channels[0].dcLevel = -1.25;
        run.host.post(s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return st.channels[0].level == -1.25; }));
        QVERIFY(std::abs(sim.aiReadStatic(mydaq::AiChannel::AI0, 10) + 1.25) < 0.01);
        // A function on AO 1 upgrades to a timed task; the DC level stays.
        s.channels[1] = functionOn(100, 1, 1).channels[1];
        run.host.post(s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return st.strategy == StrategyKind::Regenerate; }));
        const auto y = acquireAi0(sim, 0.01);
        QVERIFY(std::abs(y[100] + 1.25) < 0.01);
        run.join();
        QVERIFY(std::abs(sim.aiReadStatic(mydaq::AiChannel::AI0, 10)) < 0.01);
    }

    void streamingNotSupportedFallsBack()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        FaultyDevice device(sim);
        device.refuseStreaming = true;
        EngineSettings s = functionOn(1000);
        s.preference = EnginePreference::Stream;
        Runner run(device, s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return st.strategy == StrategyKind::Regenerate; }));
        QCOMPARE(run.host.warnings().size(), 1);
        run.join();

        EngineSettings sweep = functionOn(100);
        sweep.channels[0].sweep = true;
        Runner failing(device, sweep);
        QVERIFY(failing.waitFinished(2000));
        QCOMPARE(failing.code, mydaq::DeviceError::NotSupported);
    }

    void underflowFallback()
    {
        mydaq::SimDevice sim;
        quiet(sim);
        FaultyDevice device(sim);
        device.underflows = 1;
        EngineSettings s = functionOn(1000);
        s.preference = EnginePreference::Stream;
        Runner run(device, s);
        QVERIFY(run.waitStatus([](const EngineStatus& st) { return st.taskRate == 100000.0; }));
        QVERIFY(run.host.warnings().front().contains(u"continuing at 100 kS/s"));
        QVERIFY(!run.finished);
        device.underflows = 1;   // second underflow at 100 kS/s: error
        QVERIFY(run.waitFinished(2000));
        QCOMPARE(run.code, mydaq::DeviceError::Underflow);
    }
};

QTEST_GUILESS_MAIN(TestGenEngine)
#include "tst_genengine.moc"
