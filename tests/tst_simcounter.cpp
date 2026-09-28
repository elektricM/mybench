// Simulated ctr0 (counter input, pulse train), FREQOUT, digital sources and line reservations.
#include "mydaq/sim_device.h"

#include <QElapsedTimer>
#include <QTest>

#include <atomic>
#include <cmath>
#include <thread>

using namespace mydaq;
using Measurement = CounterMeasurement;

namespace {

constexpr int kNoError = -1;

template <class F>
int errorOf(F&& f)
{
    try {
        f();
    } catch (const DeviceError& e) {
        return e.code();
    }
    return kNoError;
}

CounterInputConfig measure(Measurement m, double minValue = 1.0, double maxValue = 1e6)
{
    CounterInputConfig c;
    c.measurement = m;
    c.minValue = minValue;
    c.maxValue = maxValue;
    return c;
}

CounterInputConfig timing(Measurement m) { return measure(m, 1e-6, 1.0); }   // intervals of 1 us .. 1 s

SimDigitalSource clock(double frequency, double duty = 0.5, double delay = 0.0)
{
    SimDigitalSource s;
    s.kind = SimDigitalSource::Kind::Clock;
    s.frequency = frequency;
    s.dutyCycle = duty;
    s.delay = delay;
    return s;
}

SimDigitalSource level(bool high)
{
    SimDigitalSource s;
    s.kind = SimDigitalSource::Kind::Level;
    s.high = high;
    return s;
}

double scalar(SimDevice& dev, const CounterInputConfig& config)
{
    auto task = dev.createCounterInputTask(config);
    task->start();
    return task->readScalar(1000);
}

// Fraction of dioRead() polls during `ms` that saw `line` high.
double highFraction(SimDevice& dev, int line, int ms)
{
    QElapsedTimer timer;
    timer.start();
    int64_t high = 0;
    int64_t total = 0;
    while (timer.elapsed() < ms) {
        high += (dev.dioRead() >> line) & 1;
        ++total;
    }
    return double(high) / double(total);
}

bool near(double value, double expected, double relative) { return std::abs(value - expected) <= relative * std::abs(expected); }

}  // namespace

class TestSimCounter : public QObject {
    Q_OBJECT

private slots:
    void coercion()
    {
        SimDevice dev;
        QCOMPARE(dev.coerceCounterFrequency(1e6), 1e6);
        QCOMPARE(dev.coerceCounterFrequency(3e6), 100e6 / 33.0);
        QCOMPARE(dev.coerceCounterFrequency(40e6), 25e6);
        QCOMPARE(dev.coerceFrequencyOutput(1e6), 1e6);      // 10 MHz / 10
        QCOMPARE(dev.coerceFrequencyOutput(5e6), 5e6);      // 20 MHz / 4
        QCOMPARE(dev.coerceFrequencyOutput(50e3), 50e3);    // 100 kHz / 2
        QCOMPARE(dev.coerceFrequencyOutput(1000.0), 6250.0);   // slowest: 100 kHz / 16
        QCOMPARE(dev.coerceFrequencyOutput(1e9), 20e6);
        PulseOutputConfig p;
        p.frequency = 10e6;
        p.dutyCycle = 0.05;
        auto co = dev.createCounterOutputTask(p);
        QCOMPARE(co->actualFrequency(), 10e6);
        QCOMPARE(co->actualDutyCycle(), 0.2);   // high time is at least 2 ticks of 100 MHz
    }

    void reservations()
    {
        SimDevice dev;
        auto freq = dev.createCounterInputTask(measure(Measurement::Frequency));
        QCOMPARE(freq->dioLinesUsed(), uint8_t(0x02));
        QCOMPARE(errorOf([&] { dev.createCounterInputTask(measure(Measurement::CountEdges)); }), int(DeviceError::ResourceBusy));
        QCOMPARE(errorOf([&] { dev.createCounterOutputTask({}); }), int(DeviceError::ResourceBusy));
        QCOMPARE(errorOf([&] { dev.dioSetDirection(0x02, true); }), int(DeviceError::ResourceBusy));
        QCOMPARE(errorOf([&] { dev.dioSetDirection(0x20, true); }), kNoError);
        dev.dioWrite(0xFF);   // reserved lines are left alone like any non-output line
        QCOMPARE(dev.dioDirection(), uint8_t(0x20));
        freq.reset();

        dev.dioSetDirection(0x01, true);   // DIO0 as a digital output: no edge counting on it
        QCOMPARE(errorOf([&] { dev.createCounterInputTask(measure(Measurement::CountEdges)); }), int(DeviceError::ResourceBusy));
        dev.dioSetDirection(0x01, false);

        CounterInputConfig edges = measure(Measurement::CountEdges);
        edges.direction = CounterInputConfig::Direction::External;
        edges.resetOnDio1 = true;
        QCOMPARE(dev.createCounterInputTask(edges)->dioLinesUsed(), uint8_t(0x07));
        CounterInputConfig encoder = measure(Measurement::AngularEncoder);
        QCOMPARE(dev.createCounterInputTask(encoder)->dioLinesUsed(), uint8_t(0x05));
        encoder.zIndexEnabled = true;
        QCOMPARE(dev.createCounterInputTask(encoder)->dioLinesUsed(), uint8_t(0x07));
        QCOMPARE(dev.createCounterInputTask(timing(Measurement::TwoEdgeSeparation))->dioLinesUsed(), uint8_t(0x06));

        auto co = dev.createCounterOutputTask({});
        QCOMPARE(errorOf([&] { dev.dioSetDirection(0x08, true); }), int(DeviceError::ResourceBusy));
        co.reset();
        QCOMPARE(errorOf([&] { dev.dioSetDirection(0x08, true); }), kNoError);
        dev.dioSetDirection(0x08, false);

        dev.dioSetDirection(0x10, true);
        QCOMPARE(errorOf([&] { dev.frequencyOutputStart(1e6); }), int(DeviceError::ResourceBusy));
        dev.dioSetDirection(0x10, false);
        dev.frequencyOutputStart(1e6);
        QCOMPARE(errorOf([&] { dev.dioSetDirection(0x10, true); }), int(DeviceError::ResourceBusy));
        dev.frequencyOutputStop();
        QCOMPARE(errorOf([&] { dev.dioSetDirection(0x10, true); }), kNoError);
    }

    void invalidConfigs()
    {
        SimDevice dev;
        const auto code = [&](const CounterInputConfig& c) { return errorOf([&] { dev.createCounterInputTask(c); }); };
        QCOMPARE(code(measure(Measurement::Frequency, 10.0, 5.0)), int(DeviceError::InvalidConfig));
        QCOMPARE(code(measure(Measurement::Period, 0.0, 1.0)), int(DeviceError::InvalidConfig));
        CounterInputConfig buffered = measure(Measurement::CountEdges);
        buffered.buffered = true;
        QCOMPARE(code(buffered), int(DeviceError::NotSupported));   // no sample clock on the myDAQ counter
        buffered.measurement = Measurement::LinearEncoder;
        QCOMPARE(code(buffered), int(DeviceError::NotSupported));
        CounterInputConfig angular = measure(Measurement::AngularEncoder);
        angular.pulsesPerRevolution = 0;
        QCOMPARE(code(angular), int(DeviceError::InvalidConfig));

        const auto pulse = [&](double f, double duty, SampleMode mode, int64_t count, double delay) {
            PulseOutputConfig p;
            p.frequency = f;
            p.dutyCycle = duty;
            p.mode = mode;
            p.pulseCount = count;
            p.initialDelay = delay;
            return errorOf([&] { dev.createCounterOutputTask(p); });
        };
        QCOMPARE(pulse(1000.0, 0.0, SampleMode::Continuous, 1, 0.0), int(DeviceError::InvalidConfig));
        QCOMPARE(pulse(1000.0, 1.0, SampleMode::Continuous, 1, 0.0), int(DeviceError::InvalidConfig));
        QCOMPARE(pulse(30e6, 0.5, SampleMode::Continuous, 1, 0.0), int(DeviceError::InvalidConfig));
        QCOMPARE(pulse(1000.0, 0.5, SampleMode::Finite, 0, 0.0), int(DeviceError::InvalidConfig));
        QCOMPARE(pulse(1000.0, 0.5, SampleMode::Continuous, 1, -1.0), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { dev.frequencyOutputStart(0.0); }), int(DeviceError::InvalidConfig));

        auto onDemand = dev.createCounterInputTask(measure(Measurement::Frequency));
        QCOMPARE(errorOf([&] { onDemand->readScalar(10); }), int(DeviceError::Generic));   // not started
        onDemand->start();
        QCOMPARE(errorOf([&] { onDemand->readBuffered(1, 10); }), int(DeviceError::InvalidConfig));
    }

    void timedMeasurements()
    {
        SimDevice dev;
        const double f = 1234.5;
        dev.setDigitalSource(1, clock(f, 0.3));
        QCOMPARE(dev.digitalSource(1).kind, SimDigitalSource::Kind::Clock);
        QVERIFY(near(scalar(dev, measure(Measurement::Frequency)), f, 1e-4));
        QVERIFY(near(scalar(dev, timing(Measurement::Period)), 1.0 / f, 1e-4));
        CounterInputConfig width = timing(Measurement::PulseWidth);
        QVERIFY(near(scalar(dev, width), 0.3 / f, 1e-4));   // high time
        width.edge = Edge::Falling;
        QVERIFY(near(scalar(dev, width), 0.7 / f, 1e-4));   // low time
        const double semi = scalar(dev, timing(Measurement::SemiPeriod));
        QVERIFY(near(semi, 0.3 / f, 1e-4) || near(semi, 0.7 / f, 1e-4));
        // 10 us resolution when the expected range needs the 100 kHz timebase.
        const double coarse = scalar(dev, measure(Measurement::Period, 1.0, 1000.0));
        QVERIFY(std::abs(coarse - 1.0 / f) <= 1.01e-5);
    }

    void twoEdgeSeparation()
    {
        SimDevice dev;
        dev.setDigitalSource(2, clock(1000.0, 0.5, 0.0));      // first edge
        dev.setDigitalSource(1, clock(1000.0, 0.5, 0.2e-3));   // second edge, 0.2 ms later
        CounterInputConfig c = timing(Measurement::TwoEdgeSeparation);
        c.edge = Edge::Rising;
        c.secondEdge = Edge::Rising;
        QVERIFY(near(scalar(dev, c), 0.2e-3, 1e-4));
        c.secondEdge = Edge::Falling;
        QVERIFY(near(scalar(dev, c), 0.7e-3, 1e-4));
    }

    void timeoutWithoutEdges()
    {
        SimDevice dev;
        dev.setDigitalSource(1, level(false));
        auto task = dev.createCounterInputTask(measure(Measurement::Frequency));
        task->start();
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(errorOf([&] { task->readScalar(50); }), int(DeviceError::Timeout));
        QVERIFY(timer.elapsed() >= 45);
    }

    void bufferedMeasurements()
    {
        SimDevice dev;
        dev.setDigitalSource(1, clock(10e3, 0.5));
        CounterInputConfig c = measure(Measurement::Frequency, 100.0, 1e6);
        c.buffered = true;
        c.mode = SampleMode::Finite;
        c.samplesPerChannel = 200;
        auto task = dev.createCounterInputTask(c);
        task->start();
        QElapsedTimer timer;
        timer.start();
        const std::vector<double> values = task->readBuffered(200, 2000);   // one value per period
        QVERIFY2(timer.elapsed() >= 15 && timer.elapsed() < 300, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(values.size(), size_t(200));
        for (double v : values)
            QVERIFY(near(v, 10e3, 2e-4));   // +-1 tick of 100 MHz in 10000
        QVERIFY(!task->isRunning());
        QCOMPARE(errorOf([&] { task->readBuffered(1, 100); }), int(DeviceError::InvalidConfig));
        task.reset();

        // Continuous: the buffer holds max(samplesPerChannel, 10000) values.
        dev.setDigitalSource(1, clock(100e3, 0.5));
        c.mode = SampleMode::Continuous;
        c.samplesPerChannel = 1000;
        task = dev.createCounterInputTask(c);
        task->start();
        QCOMPARE(task->readBuffered(100, 1000).size(), size_t(100));
        QThread::msleep(150);   // 15000 values pile up
        QCOMPARE(errorOf([&] { task->readBuffered(10, 100); }), int(DeviceError::Overflow));
        QVERIFY(!task->isRunning());
    }

    void countEdges()
    {
        SimDevice dev;
        dev.setDigitalSource(0, clock(1000.0, 0.5));
        const auto run = [&](const CounterInputConfig& c, int ms, double& elapsedMs) {
            auto task = dev.createCounterInputTask(c);
            QElapsedTimer timer;
            timer.start();
            task->start();
            QThread::msleep(ms);
            const double v = task->readScalar(100);
            elapsedMs = double(timer.nsecsElapsed()) / 1e6;
            return v;
        };
        double elapsed = 0.0;
        const double up = run(measure(Measurement::CountEdges), 60, elapsed);
        QVERIFY2(up >= 58.0 && up <= elapsed + 1.0, qPrintable(QString::number(up)));

        CounterInputConfig down = measure(Measurement::CountEdges);
        down.direction = CounterInputConfig::Direction::Down;
        down.initialCount = 1000;
        const double left = run(down, 40, elapsed);
        QVERIFY(left <= 962.0 && left >= 1000.0 - elapsed - 1.0);

        CounterInputConfig external = measure(Measurement::CountEdges);
        external.direction = CounterInputConfig::Direction::External;
        dev.setDigitalSource(2, level(false));   // DIO2 low: count down, wrapping below 0
        const double wrapped = run(external, 30, elapsed);
        QVERIFY(wrapped > 4294967296.0 - elapsed - 2.0 && wrapped <= 4294967296.0 - 28.0);
        dev.setDigitalSource(2, level(true));
        const double upExternal = run(external, 30, elapsed);
        QVERIFY(upExternal >= 28.0 && upExternal <= elapsed + 1.0);

        CounterInputConfig reset = measure(Measurement::CountEdges);
        reset.resetOnDio1 = true;
        dev.setDigitalSource(1, clock(20.0, 0.5));   // count reset every 50 ms
        QVERIFY(run(reset, 120, elapsed) <= 51.0);
    }

    void encoders()
    {
        SimDevice dev;
        dev.setEncoder({true, 1000.0, 100});   // 1000 cycles/s, 10 revolutions/s
        QVERIFY(dev.encoder().enabled);
        // Returns the task's value and the encoder travel (cycles) around the measurement.
        const auto run = [&](const CounterInputConfig& c, double& travel) {
            auto task = dev.createCounterInputTask(c);
            const double p0 = dev.encoderPosition();
            task->start();
            QThread::msleep(60);
            const double v = task->readScalar(100);
            travel = dev.encoderPosition() - p0;
            return v;
        };
        double travel = 0.0;
        CounterInputConfig linear = measure(Measurement::LinearEncoder);
        linear.distancePerPulse = 0.5;   // X4: 4 counts per pulse
        const double position = run(linear, travel);
        QVERIFY2(position / 0.5 <= travel + 0.25 && position / 0.5 >= travel - 0.5,
                 qPrintable(QStringLiteral("%1 %2").arg(position).arg(travel)));

        linear.decoding = CounterInputConfig::Decoding::X1;
        linear.distancePerPulse = 1.0;
        const double x1 = run(linear, travel);
        QVERIFY(x1 <= travel + 1.0 && x1 >= travel - 1.5);

        linear.decoding = CounterInputConfig::Decoding::TwoPulse;   // A counts up, B down: ~0 in quadrature
        QVERIFY(std::abs(run(linear, travel)) <= 1.0);

        dev.setEncoder({true, -500.0, 100});   // backwards
        CounterInputConfig angular = measure(Measurement::AngularEncoder);
        angular.decoding = CounterInputConfig::Decoding::X2;
        angular.pulsesPerRevolution = 100;
        const double degrees = run(angular, travel);   // 3.6 degrees per cycle
        QVERIFY(travel < 0.0);
        QVERIFY2(std::abs(degrees - 3.6 * travel) <= 3.6, qPrintable(QStringLiteral("%1 %2").arg(degrees).arg(travel)));

        // Z index reload: position returns to zIndexValue once per revolution (100 cycles).
        dev.setEncoder({true, 1000.0, 100});
        linear.decoding = CounterInputConfig::Decoding::X4;
        linear.zIndexEnabled = true;
        linear.zIndexValue = 0.0;
        auto task = dev.createCounterInputTask(linear);
        task->start();
        QThread::msleep(250);   // 2.5 revolutions
        const double indexed = task->readScalar(100);
        QVERIFY2(indexed >= -0.5 && indexed <= 100.5, qPrintable(QString::number(indexed)));
        task.reset();

        // Disabling the encoder gives DIO0..2 back to their sources.
        dev.setDigitalSource(0, level(true));
        QVERIFY(highFraction(dev, 0, 20) < 0.9);   // still the encoder's A
        dev.setEncoder({false, 1000.0, 100});
        QVERIFY(dev.dioRead() & 0x01);
    }

    void pulseTrainOutput()
    {
        SimDevice dev;
        PulseOutputConfig p;
        p.frequency = 1000.0;
        p.dutyCycle = 0.25;
        auto co = dev.createCounterOutputTask(p);
        QVERIFY(!(dev.dioRead() & 0x08));   // idle low
        co->start();
        QVERIFY(co->isRunning());
        QVERIFY(std::abs(highFraction(dev, 3, 60) - 0.25) < 0.05);
        co->update(2000.0, 0.5);   // live, from the next period boundary
        QCOMPARE(co->actualFrequency(), 2000.0);
        QCOMPARE(co->config().frequency, 2000.0);
        QThread::msleep(2);
        QVERIFY(std::abs(highFraction(dev, 3, 60) - 0.5) < 0.05);
        co->stop();
        QVERIFY(!co->isRunning());
        QVERIFY(!(dev.dioRead() & 0x08));
        co.reset();

        PulseOutputConfig burst;
        burst.frequency = 1000.0;
        burst.dutyCycle = 0.5;
        burst.idleHigh = true;
        burst.mode = SampleMode::Finite;
        burst.pulseCount = 20;
        burst.initialDelay = 0.03;
        co = dev.createCounterOutputTask(burst);
        QVERIFY(dev.dioRead() & 0x08);   // idle high
        QElapsedTimer timer;
        timer.start();
        co->start();
        QVERIFY(!co->waitUntilDone(5));
        // Still in the 30 ms initial delay: only samples taken before 25 ms after start() count
        // (under load the polling thread may be descheduled past the end of the delay).
        int64_t samples = 0, highSamples = 0;
        while (timer.elapsed() < 25) {
            const bool level = (dev.dioRead() >> 3) & 1;
            if (timer.elapsed() < 25) {
                highSamples += level;
                ++samples;
            }
        }
        QVERIFY(samples == 0 || highSamples == samples);
        QCOMPARE(errorOf([&] { co->update(500.0, 0.5); }), int(DeviceError::NotSupported));
        QVERIFY(co->waitUntilDone(1000));
        // 30 ms delay + 20 pulses at 1 kHz; the upper bound only guards against a hang (load).
        QVERIFY2(timer.elapsed() >= 48 && timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(!co->isRunning());
        QVERIFY(dev.dioRead() & 0x08);   // back to idle high
    }

    void frequencyOutputLoopback()
    {
        SimDevice dev;
        QVERIFY(!dev.frequencyOutputRunning());
        QCOMPARE(dev.frequencyOutputStart(1e6), 1e6);
        QVERIFY(dev.frequencyOutputRunning());
        QCOMPARE(dev.frequencyOutputStart(6000.0), 6250.0);    // slow enough to watch with dioRead()
        QVERIFY(std::abs(highFraction(dev, 4, 30) - 0.5) < 0.05);

        SimDigitalSource wire;
        wire.kind = SimDigitalSource::Kind::FrequencyOutput;   // FREQOUT wired to DIO1
        dev.setDigitalSource(1, wire);
        QCOMPARE(dev.frequencyOutputStart(50e3), 50e3);         // retune while running
        QVERIFY(near(scalar(dev, measure(Measurement::Frequency, 1000.0, 1e7)), 50e3, 1e-3));   // +-1 tick in 2000
        CounterInputConfig c = measure(Measurement::Frequency, 1000.0, 1e7);
        c.buffered = true;
        c.mode = SampleMode::Finite;
        c.samplesPerChannel = 500;
        auto task = dev.createCounterInputTask(c);
        task->start();
        for (double v : task->readBuffered(500, 1000))
            QVERIFY(near(v, 50e3, 1e-3));
        task.reset();

        dev.frequencyOutputStop();
        QVERIFY(!dev.frequencyOutputRunning());
        task = dev.createCounterInputTask(measure(Measurement::Frequency));
        task->start();
        QCOMPARE(errorOf([&] { task->readScalar(30); }), int(DeviceError::Timeout));   // wire is low now
    }

    void digitalSources()
    {
        SimDevice dev;
        dev.setDigitalSource(5, level(true));
        QVERIFY(dev.dioRead() & 0x20);
        dev.setDigitalSource(5, level(false));
        QVERIFY(!(dev.dioRead() & 0x20));
        dev.setDigitalSource(5, {});   // floating again: bit 5 of the 2 Hz counter (16 s high)
        QCOMPARE(dev.digitalSource(5).kind, SimDigitalSource::Kind::Floating);
        QCOMPARE(errorOf([&] { dev.setDigitalSource(8, level(true)); }), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { dev.setDigitalSource(1, clock(0.0)); }), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { dev.setEncoder({true, 10.0, 0}); }), int(DeviceError::InvalidConfig));
        // An output line reads its latch, whatever drives it from outside.
        dev.dioSetDirection(0x20, true);
        dev.dioWrite(0x20);
        dev.setDigitalSource(5, level(false));
        QVERIFY(dev.dioRead() & 0x20);
    }

    void resetStopsCounterAndFreqOut()
    {
        SimDevice dev;
        auto co = dev.createCounterOutputTask({});
        co->start();
        dev.frequencyOutputStart(1e6);
        dev.reset();
        QVERIFY(!co->isRunning());
        QVERIFY(!dev.frequencyOutputRunning());
        QVERIFY(!(dev.dioRead() & 0x08));
        co.reset();

        dev.setDigitalSource(1, level(false));
        auto ci = dev.createCounterInputTask(measure(Measurement::Frequency));
        ci->start();
        std::atomic<int> code{-2};
        std::thread reader([&] { code = errorOf([&] { ci->readScalar(5000); }); });
        QThread::msleep(50);
        QElapsedTimer timer;
        timer.start();
        dev.reset();
        reader.join();
        QVERIFY(timer.elapsed() < 500);
        QCOMPARE(code.load(), int(DeviceError::Generic));
        QVERIFY(!ci->isRunning());
    }
};

QTEST_GUILESS_MAIN(TestSimCounter)
#include "tst_simcounter.moc"
