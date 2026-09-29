#include "mydaq/sim_device.h"

#include "mydaq/dsp/measure.h"
#include "mydaq/dsp/waveform.h"

#include <QElapsedTimer>
#include <QTest>

#include <atomic>
#include <cmath>
#include <numbers>
#include <thread>

using namespace mydaq;

namespace {

constexpr int kNoError = -1;

// Runs f and returns the DeviceError code it threw, or kNoError.
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

AiConfig aiConfig(std::vector<AiChannelConfig> channels, double rate, SampleMode mode, int64_t samples)
{
    AiConfig c;
    c.channels = std::move(channels);
    c.sampleRate = rate;
    c.mode = mode;
    c.samplesPerChannel = samples;
    return c;
}

AoConfig aoConfig(std::vector<AoChannelConfig> channels, double rate, SampleMode mode, bool regenerate)
{
    AoConfig c;
    c.channels = std::move(channels);
    c.sampleRate = rate;
    c.mode = mode;
    c.regenerate = regenerate;
    return c;
}

// Starts a regenerating task on `channel` playing a sine of `frequency` (whole periods per buffer).
std::unique_ptr<AoTask> playSine(SimDevice& dev, AoChannel channel, double frequency, double amplitude, double offset = 0.0)
{
    const dsp::BufferPlan plan = dsp::planPeriodicBuffer(frequency, dev.maxAoRate(),
                                                         [&](double r) { return dev.coerceAoRate(r); });
    auto task = dev.createAoTask(aoConfig({{channel, isAudio(channel) ? 2.0 : 10.0}}, plan.sampleRate,
                                          SampleMode::Continuous, true));
    task->write({dsp::generatePeriodic({dsp::Waveform::Sine, frequency, amplitude, offset, 0.0, 0.5}, plan)});
    task->start();
    return task;
}

std::vector<std::vector<double>> acquire(SimDevice& dev, std::vector<AiChannelConfig> channels, double rate, int64_t n)
{
    auto task = dev.createAiTask(aiConfig(std::move(channels), rate, SampleMode::Finite, n));
    task->start();
    return task->read(n, 5000);
}

std::vector<double> tail(const std::vector<double>& x, size_t skip) { return {x.begin() + ptrdiff_t(skip), x.end()}; }

}  // namespace

class TestSimDevice : public QObject {
    Q_OBJECT

private slots:
    void infoAndRates()
    {
        SimDevice dev;
        const DeviceInfo info = dev.info();
        QCOMPARE(info.id, QStringLiteral("sim:0"));
        QCOMPARE(info.productType, QStringLiteral("NI myDAQ"));
        QVERIFY(info.simulated);
        QVERIFY(dev.isConnected());
        QCOMPARE(dev.maxAiRate(), 200e3);
        QCOMPARE(dev.coerceAiRate(200e3), 200e3);
        QCOMPARE(dev.coerceAiRate(1e6), 200e3);
        QCOMPARE(dev.coerceAiRate(48000.0), 100e6 / 2083.0);
        QCOMPARE(dev.coerceAoRate(1000.0), 1000.0);
        auto task = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}}, 48000.0, SampleMode::Finite, 10));
        QCOMPARE(task->sampleRate(), 100e6 / 2083.0);
        QCOMPARE(task->channelSkew(), 0.0);   // one channel: nothing to skew
        task.reset();
        // AI0/AI1 are multiplexed at 400 kS/s aggregate: the second channel converts 2.5 us later.
        QCOMPARE(dev.channelSkew(), 2.5e-6);
        task = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}}, 1000.0, SampleMode::Finite, 10));
        QCOMPARE(task->channelSkew(), 2.5e-6);
    }

    // The skew is part of the signal model: the same sine on both inputs shows a phase lead of
    // 360 * f * skew on AI1.
    void channelSkewInSignalModel()
    {
        SimDevice dev;
        const SimTestSignal sine{dsp::Waveform::Sine, 10e3, 1.0, 0.0, 0.5, {}};
        dev.setTestSignal(AiChannel::AI0, sine);
        dev.setTestSignal(AiChannel::AI1, sine);
        const double fs = 200e3;
        const auto data = acquire(dev, {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}}, fs, 2000);
        const double lead = std::remainder(dsp::fitSine(data[1], fs, 10e3).phase - dsp::fitSine(data[0], fs, 10e3).phase,
                                           2.0 * std::numbers::pi) * 180.0 / std::numbers::pi;
        QVERIFY2(std::abs(lead - 360.0 * 10e3 * 2.5e-6) < 0.1, qPrintable(QString::number(lead)));
        dev.setChannelSkew(0.0);
        const auto simultaneous = acquire(dev, {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}}, fs, 2000);
        QVERIFY(std::abs(dsp::fitSine(simultaneous[1], fs, 10e3).phase - dsp::fitSine(simultaneous[0], fs, 10e3).phase) < 1e-3);
    }

    void aiReservationAndConfig()
    {
        SimDevice dev;
        const AiChannelConfig ai0{AiChannel::AI0, 10.0};
        const AiChannelConfig ai1{AiChannel::AI1, 2.0};
        auto task = dev.createAiTask(aiConfig({ai0, ai1}, 1000.0, SampleMode::Finite, 100));
        QCOMPARE(errorOf([&] { dev.createAiTask(aiConfig({ai0}, 1000.0, SampleMode::Finite, 100)); }),
                 int(DeviceError::ResourceBusy));
        QCOMPARE(errorOf([&] { dev.aiReadStatic(AiChannel::AI0, 10.0); }), int(DeviceError::ResourceBusy));
        task.reset();
        QCOMPARE(errorOf([&] { dev.aiReadStatic(AiChannel::AI0, 10.0); }), kNoError);

        const auto invalid = [&](const AiConfig& c) {
            return errorOf([&] { dev.createAiTask(c); }) == int(DeviceError::InvalidConfig);
        };
        const AiChannelConfig left{AiChannel::AudioInLeft, 2.0};
        QVERIFY(invalid(aiConfig({ai0, left}, 1000.0, SampleMode::Finite, 100)));   // AI + audio
        QVERIFY(invalid(aiConfig({ai0, ai0}, 1000.0, SampleMode::Finite, 100)));    // duplicate
        QVERIFY(invalid(aiConfig({ai0, ai1, {AiChannel::AI1, 10.0}}, 1000.0, SampleMode::Finite, 100)));
        QVERIFY(invalid(aiConfig({}, 1000.0, SampleMode::Finite, 100)));
        QVERIFY(invalid(aiConfig({{AiChannel::AI0, 5.0}}, 1000.0, SampleMode::Finite, 100)));
        QVERIFY(invalid(aiConfig({{AiChannel::AudioInLeft, 10.0}}, 1000.0, SampleMode::Finite, 100)));
        QVERIFY(invalid(aiConfig({ai0}, 250e3, SampleMode::Finite, 100)));
        QVERIFY(invalid(aiConfig({ai0}, 0.0, SampleMode::Finite, 100)));
        QVERIFY(invalid(aiConfig({ai0}, 1000.0, SampleMode::Finite, 0)));
        QVERIFY(invalid(aiConfig({ai0}, 1000.0, SampleMode::Continuous, -5)));
        QCOMPARE(errorOf([&] { dev.aiReadStatic(AiChannel::AI0, 3.0); }), int(DeviceError::InvalidConfig));
        // Audio channels together are fine, and the reservation was released by the failures.
        QCOMPARE(errorOf([&] { dev.createAiTask(aiConfig({left, {AiChannel::AudioInRight, 2.0}}, 48000.0,
                                                         SampleMode::Finite, 100)); }),
                 kNoError);
    }

    void aoReservationAndConfig()
    {
        SimDevice dev;
        auto task = dev.createAoTask(aoConfig({{AoChannel::AO0, 10.0}}, 1000.0, SampleMode::Finite, false));
        QCOMPARE(errorOf([&] { dev.createAoTask(aoConfig({{AoChannel::AO1, 10.0}}, 1000.0, SampleMode::Finite, false)); }),
                 int(DeviceError::ResourceBusy));
        QCOMPARE(errorOf([&] { dev.aoWriteStatic(AoChannel::AO1, 1.0); }), int(DeviceError::ResourceBusy));
        QCOMPARE(errorOf([&] { task->write({{1.0, 2.0}, {1.0, 2.0}}); }), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { task->write({{1.0, 12.0}}); }), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { task->write({{}}); }), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { task->start(); }), int(DeviceError::InvalidConfig));   // nothing written
        task.reset();
        QCOMPARE(errorOf([&] { dev.aoWriteStatic(AoChannel::AO1, 1.0); }), kNoError);
        QCOMPARE(errorOf([&] { dev.aoWriteStatic(AoChannel::AO1, 10.5); }), int(DeviceError::InvalidConfig));
        QCOMPARE(errorOf([&] { dev.aoWriteStatic(AoChannel::AudioOutLeft, 2.5); }), int(DeviceError::InvalidConfig));

        const auto invalid = [&](const AoConfig& c) {
            return errorOf([&] { dev.createAoTask(c); }) == int(DeviceError::InvalidConfig);
        };
        QVERIFY(invalid(aoConfig({{AoChannel::AO0, 10.0}, {AoChannel::AudioOutLeft, 2.0}}, 1000.0, SampleMode::Finite, false)));
        QVERIFY(invalid(aoConfig({{AoChannel::AO0, 10.0}, {AoChannel::AO0, 10.0}}, 1000.0, SampleMode::Finite, false)));
        QVERIFY(invalid(aoConfig({{AoChannel::AO0, 5.0}}, 1000.0, SampleMode::Finite, false)));
        QVERIFY(invalid(aoConfig({{AoChannel::AudioOutLeft, 10.0}}, 1000.0, SampleMode::Finite, false)));
        QVERIFY(invalid(aoConfig({{AoChannel::AO0, 10.0}}, 400e3, SampleMode::Continuous, true)));
    }

    // AO0/AO1 also have a +-2 V range: values beyond it are rejected, and its 16-bit steps are 5x finer.
    void aoTwoVoltRange()
    {
        SimDevice dev;
        dev.setNoiseRms(0.0);
        auto ao = dev.createAoTask(aoConfig({{AoChannel::AO0, 2.0}}, 1000.0, SampleMode::Finite, false));
        QCOMPARE(errorOf([&] { ao->write({{2.5}}); }), int(DeviceError::InvalidConfig));
        ao->write({{0.123456}});
        ao->start();
        QVERIFY(ao->waitUntilDone(1000));
        const double fine = dev.aiReadStatic(AiChannel::AI0, 2.0);
        const double aoLsb2 = 2.0 * 1.05 * 2.0 / 65536.0;
        const double aiLsb2 = aoLsb2;
        QVERIFY2(std::abs(fine - 0.123456) <= 0.5 * aoLsb2 + 0.5 * aiLsb2 + 1e-12, qPrintable(QString::number(fine, 'g', 10)));
        ao.reset();
        // A static write uses the +-10 V range: its step is 5x coarser.
        dev.aoWriteStatic(AoChannel::AO0, 0.123456);
        const double coarse = dev.aiReadStatic(AiChannel::AI0, 2.0);
        const double aoLsb10 = 2.0 * 1.05 * 10.0 / 65536.0;
        QVERIFY(std::abs(coarse - 0.123456) <= 0.5 * aoLsb10 + 0.5 * aiLsb2 + 1e-12);
    }

    void finiteReadTiming()
    {
        SimDevice dev;
        auto task = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}}, 10e3, SampleMode::Finite, 2000));
        QVERIFY(!task->isRunning());
        QElapsedTimer timer;
        timer.start();
        task->start();
        QVERIFY(task->isRunning());
        const auto data = task->read(2000, 5000);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed >= 190 && elapsed < 450, qPrintable(QString::number(elapsed)));   // 2000 / 10 kS/s
        QCOMPARE(data.size(), size_t(2));
        QCOMPARE(data[0].size(), size_t(2000));
        QVERIFY(!task->isRunning());
        QCOMPARE(task->available(), int64_t(0));
        QCOMPARE(errorOf([&] { task->read(1, 100); }), int(DeviceError::InvalidConfig));   // nothing remains
        // A new start() begins a new acquisition; partial reads are allowed.
        task->start();
        QCOMPARE(task->read(500, 5000)[1].size(), size_t(500));
        QCOMPARE(task->read(1500, 5000)[0].size(), size_t(1500));
    }

    void readTimeout()
    {
        SimDevice dev;
        auto task = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}}, 1000.0, SampleMode::Finite, 1000));
        task->start();
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(errorOf([&] { task->read(1000, 50); }), int(DeviceError::Timeout));
        QVERIFY(timer.elapsed() >= 45 && timer.elapsed() < 400);
        QVERIFY(task->read(-1, 0).front().size() > 0);   // non-blocking read of what exists
    }

    void continuousReadAndOverflow()
    {
        SimDevice dev;
        auto task = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}}, 10e3, SampleMode::Continuous, 1000));
        task->start();
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 3; ++i)
            QCOMPARE(task->read(1000, 2000)[0].size(), size_t(1000));
        QVERIFY2(timer.elapsed() >= 280 && timer.elapsed() < 600, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(task->isRunning());
        QCOMPARE(errorOf([&] { task->read(20000, 100); }), int(DeviceError::InvalidConfig));   // > 1 s buffer
        // Buffer = max(samplesPerChannel, 1 s) = 10000 samples: falling 1.15 s behind overflows.
        QThread::msleep(1150);
        QCOMPARE(task->available(), int64_t(10000));
        QCOMPARE(errorOf([&] { task->read(1000, 1000); }), int(DeviceError::Overflow));
        QVERIFY(!task->isRunning());
        task->start();
        QCOMPARE(task->read(100, 1000)[0].size(), size_t(100));
    }

    void builtInTestSignals()
    {
        SimDevice dev;
        const double fs = 100e3;
        const auto data = acquire(dev, {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 2.0}}, fs, 10000);
        const dsp::Tone t = dsp::extractSingleTone(data[0], fs);
        QVERIFY(std::abs(t.frequency - 1000.0) < 0.1);
        QVERIFY(std::abs(t.amplitude - 1.0) < 0.005);
        const dsp::HarmonicAnalysis h = dsp::analyzeHarmonics(data[0], fs);
        QVERIFY2(std::abs(h.thd * 100.0 - 1.0) < 0.05, qPrintable(QString::number(h.thd * 100.0)));
        QVERIFY(std::abs(dsp::frequency(data[1], fs) - 200.0) < 1.0);
        QVERIFY(std::abs(dsp::dutyCycle(data[1]) - 0.2) < 0.01);
        QVERIFY(std::abs(dsp::peakToPeak(data[1]) - 1.0) < 0.02);

        SimTestSignal s;
        s.waveform = dsp::Waveform::Triangle;
        s.frequency = 50.0;
        s.amplitude = 3.0;
        s.offset = 1.0;
        dev.setTestSignal(AiChannel::AI1, s);
        QCOMPARE(dev.testSignal(AiChannel::AI1).frequency, 50.0);
        const auto tri = acquire(dev, {{AiChannel::AI1, 10.0}}, 100e3, 10000);
        QVERIFY(std::abs(dsp::maximum(tri[0]) - 4.0) < 0.01);
        QVERIFY(std::abs(dsp::minimum(tri[0]) + 2.0) < 0.01);
    }

    void aoRegenerateLoopback()
    {
        SimDevice dev;
        auto ao = playSine(dev, AoChannel::AO0, 500.0, 1.0, 0.2);
        QVERIFY(ao->isRunning());
        const double fs = 100e3;
        const auto data = acquire(dev, {{AiChannel::AI0, 10.0}}, fs, 5000);
        const dsp::Tone t = dsp::extractSingleTone(data[0], fs);
        QVERIFY(std::abs(t.frequency - 500.0) < 0.05);
        QVERIFY(std::abs(t.amplitude - 1.0) < 0.003);
        QVERIFY(std::abs(t.offset - 0.2) < 0.003);
        QCOMPARE(errorOf([&] { ao->write({{0.0, 1.0}}); }), int(DeviceError::NotSupported));
        ao->stop();
        QVERIFY(!ao->isRunning());
        QCOMPARE(errorOf([&] { ao->write({{0.0, 1.0}}); }), kNoError);
    }

    // AO 0 -> AI 1 through the RC low-pass: -3 dB and -45 degrees at the 1 kHz corner.
    void rcLowPassAtCutoff()
    {
        SimDevice dev;
        auto ao = playSine(dev, AoChannel::AO0, 1000.0, 2.0);
        const double fs = 200e3;
        const auto data = acquire(dev, {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}}, fs, 20000);
        const dsp::Tone in = dsp::fitSine(tail(data[0], 2000), fs, 1000.0);
        const dsp::Tone out = dsp::fitSine(tail(data[1], 2000), fs, 1000.0);
        const double gainDb = 20.0 * std::log10(out.amplitude / in.amplitude);
        // AI1 is converted channelSkew() after AI0: remove that delay like a Bode analyzer does.
        const double skew = 2.0 * std::numbers::pi * 1000.0 * dev.channelSkew();
        const double phaseDeg = std::remainder(out.phase - in.phase - skew, 2.0 * std::numbers::pi) * 180.0 / std::numbers::pi;
        QVERIFY2(std::abs(gainDb + 3.0103) < 0.1, qPrintable(QString::number(gainDb)));
        QVERIFY2(std::abs(phaseDeg + 45.0) < 2.0, qPrintable(QString::number(phaseDeg)));

        // Well below the corner the response follows the input.
        dev.setRcCutoff(20e3);
        QCOMPARE(dev.rcCutoff(), 20e3);
        const auto flat = acquire(dev, {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}}, fs, 4000);
        const double ratio = dsp::fitSine(tail(flat[1], 1000), fs, 1000.0).amplitude
            / dsp::fitSine(tail(flat[0], 1000), fs, 1000.0).amplitude;
        QVERIFY(std::abs(ratio - 1.0 / std::sqrt(1.0 + 1.0 / 400.0)) < 0.005);
    }

    void staticLoopbackAndQuantization()
    {
        SimDevice dev;
        dev.setNoiseRms(0.0);
        QCOMPARE(dev.noiseRms(), 0.0);
        dev.aoWriteStatic(AoChannel::AO0, 3.0);
        QThread::msleep(20);   // RC settles (tau = 0.16 ms)
        const double lsb10 = 2.0 * 1.05 * 10.0 / 65536.0;
        const double ai0 = dev.aiReadStatic(AiChannel::AI0, 10.0);
        QVERIFY(std::abs(ai0 - 3.0) <= lsb10);   // AO and AI quantization, half a step each
        QVERIFY(std::abs(ai0 / lsb10 - std::round(ai0 / lsb10)) < 1e-6);   // 16-bit codes
        QVERIFY(std::abs(dev.aiReadStatic(AiChannel::AI1, 10.0) - 3.0) <= lsb10);
        // The +-2 V range clips a little above 2 V.
        const double clipped = dev.aiReadStatic(AiChannel::AI0, 2.0);
        QVERIFY(clipped > 2.09 && clipped < 2.1);
        // Back to 0 V: AI 0 shows its test signal again (1 V sine + 1 % third harmonic).
        dev.aoWriteStatic(AoChannel::AO0, 0.0);
        double peak = 0.0;
        for (int i = 0; i < 50; ++i) {
            peak = std::max(peak, std::abs(dev.aiReadStatic(AiChannel::AI0, 10.0)));
            QThread::usleep(97);
        }
        QVERIFY(peak > 0.3 && peak < 1.02);
    }

    void audioLoopback()
    {
        SimDevice dev;
        auto ao = playSine(dev, AoChannel::AudioOutLeft, 1000.0, 1.0);
        const double fs = dev.coerceAiRate(48000.0);
        const auto data = acquire(dev, {{AiChannel::AudioInLeft, 2.0}, {AiChannel::AudioInRight, 2.0}}, fs, 4800);
        const dsp::Tone left = dsp::fitSine(data[0], fs, 1000.0);
        QVERIFY(std::abs(left.amplitude - 1.0) < 0.005);
        const dsp::Tone right = dsp::extractSingleTone(data[1], fs);   // undriven: 1 kHz test tone
        QVERIFY(std::abs(right.frequency - 1000.0) < 0.2);
        QVERIFY(std::abs(right.amplitude - 0.5) < 0.005);
        QVERIFY(std::abs(right.offset) < 0.002);
    }

    void aoStreaming()
    {
        SimDevice dev;
        auto ao = dev.createAoTask(aoConfig({{AoChannel::AO0, 10.0}}, 10e3, SampleMode::Continuous, false));
        QCOMPARE(ao->spaceAvailable(), int64_t(10000));   // 1 s buffer
        ao->write({std::vector<double>(3000, 1.0)});
        QCOMPARE(ao->spaceAvailable(), int64_t(7000));
        ao->start();
        // 10000 more samples: 7000 fit, the rest waits for ~0.3 s of generation.
        QElapsedTimer timer;
        timer.start();
        ao->write({std::vector<double>(10000, 2.0)}, 2000);
        // write() must have blocked for the generation of ~3000 samples (300 ms at 10 kS/s) and
        // returned, well before its 2 s timeout, as soon as the last chunk fitted. The upper bound
        // is generous: a loaded CI runner may schedule the wake-up late.
        const qint64 returned = timer.elapsed();
        QVERIFY2(returned >= 250 && returned < 1800, qPrintable(QString::number(returned)));
        // The buffer was full when write() returned; from then on the simulator drains it at
        // 10 samples/ms. Bound the free space by what can have drained since (plus 50 ms of
        // slack for the wake-up), so a late scheduler does not fail a test about blocking.
        const int64_t space = ao->spaceAvailable();
        const qint64 sinceReturn = timer.elapsed() - returned;
        QVERIFY2(space < 500 + 10 * (sinceReturn + 50),
                 qPrintable(QString("space %1 after %2 ms").arg(space).arg(sinceReturn)));
        QCOMPARE(errorOf([&] { ao->write({std::vector<double>(5000, 2.0)}, 50); }), int(DeviceError::Timeout));
        ao->stop();

        // Underflow: a 50 ms pre-fill runs dry.
        ao->write({std::vector<double>(500, 0.5)});
        ao->start();
        QThread::msleep(100);
        QCOMPARE(errorOf([&] { ao->write({std::vector<double>(100, 0.5)}); }), int(DeviceError::Underflow));
        QVERIFY(!ao->isRunning());
        ao->stop();
        QCOMPARE(errorOf([&] { ao->write({std::vector<double>(100, 0.5)}); }), kNoError);

        // waitUntilDone() ends a stream once every sample was generated.
        ao->start();
        QVERIFY(ao->waitUntilDone(1000));
        QVERIFY(!ao->isRunning());
    }

    void aoFiniteHoldsLastValue()
    {
        SimDevice dev;
        dev.setNoiseRms(0.0);
        auto ao = dev.createAoTask(aoConfig({{AoChannel::AO0, 10.0}}, 10e3, SampleMode::Finite, false));
        std::vector<double> ramp(1000);
        for (size_t k = 0; k < ramp.size(); ++k)
            ramp[k] = 1.5 * double(k + 1) / double(ramp.size());
        ao->write({ramp});
        ao->start();
        QVERIFY(ao->isRunning());
        QVERIFY(!ao->waitUntilDone(10));
        QVERIFY(ao->waitUntilDone(1000));
        QVERIFY(!ao->isRunning());
        QVERIFY(std::abs(dev.aiReadStatic(AiChannel::AI0, 2.0) - 1.5) < 1e-3);
        QCOMPARE(errorOf([&] { dev.aoWriteStatic(AoChannel::AO0, 0.0); }), int(DeviceError::ResourceBusy));
        ao->start();   // finite tasks can be replayed
        QVERIFY(ao->isRunning());
        QVERIFY(ao->waitUntilDone(1000));
    }

    void dio()
    {
        SimDevice dev;
        QCOMPARE(dev.dioDirection(), uint8_t(0));
        dev.dioSetDirection(0xF0, true);
        QCOMPARE(dev.dioDirection(), uint8_t(0xF0));
        dev.dioWrite(0xA5);
        QCOMPARE(uint8_t(dev.dioRead() & 0xF0), uint8_t(0xA0));   // inputs ignore writes
        dev.dioWrite(0x40, 0x40);
        dev.dioWrite(0x00, 0x20);
        QCOMPARE(uint8_t(dev.dioRead() & 0xF0), uint8_t(0xC0));
        // Inputs show a counter incrementing at 2 Hz.
        const uint8_t before = dev.dioRead() & 0x0F;
        QThread::msleep(600);
        const uint8_t after = dev.dioRead() & 0x0F;
        QVERIFY(after != before);
        dev.dioSetDirection(0x80, false);
        QCOMPARE(dev.dioDirection(), uint8_t(0x70));
    }

    void dmm()
    {
        SimDevice dev;
        const std::vector<DmmRange> dcv = dev.dmmRanges(DmmFunction::DcVoltage);
        QCOMPARE(dcv.size(), size_t(4));
        QCOMPARE(dcv.front().label, QStringLiteral("60 V"));
        QCOMPARE(dcv.back().label, QStringLiteral("200 mV"));
        const std::vector<DmmRange> ohms = dev.dmmRanges(DmmFunction::Resistance);
        QCOMPARE(ohms.size(), size_t(6));
        QCOMPARE(ohms.front().label, QStringLiteral("20 MΩ"));
        QCOMPARE(ohms.back().label, QStringLiteral("200 Ω"));
        QCOMPARE(dev.dmmRanges(DmmFunction::AcCurrent).front().label, QStringLiteral("1 A"));
        QCOMPARE(dev.dmmRanges(DmmFunction::Diode).size(), size_t(1));
        QCOMPARE(dev.dmmRanges(DmmFunction::Continuity).size(), size_t(1));

        // Default timing: ~100 ms per DC reading.
        QElapsedTimer timer;
        timer.start();
        const DmmReading v = dev.dmmRead(DmmFunction::DcVoltage, 2.0);
        QVERIFY(timer.elapsed() >= 90);
        QVERIFY(!v.overload);
        QVERIFY(std::abs(v.value - 1.2345) <= 0.0021);
        QVERIFY(std::abs(v.value * 1000.0 - std::round(v.value * 1000.0)) < 1e-6);   // 1 mV resolution

        dev.setDmmReadTime(5, 10);
        QVERIFY(dev.dmmRead(DmmFunction::DcVoltage, 0.2).overload);
        dev.setDmmValue(DmmFunction::DcVoltage, -5.0);
        QCOMPARE(dev.dmmValue(DmmFunction::DcVoltage), -5.0);
        const DmmReading neg = dev.dmmRead(DmmFunction::DcVoltage, 20.0);
        QVERIFY(!neg.overload && std::abs(neg.value + 5.0) < 0.03);
        const DmmReading over = dev.dmmRead(DmmFunction::DcVoltage, 2.0);
        QVERIFY(over.overload && over.value < 0.0);   // -OVER
        QVERIFY(std::abs(dev.dmmRead(DmmFunction::AcVoltage, 2.0).value - 0.7071) < 0.002);
        QVERIFY(std::abs(dev.dmmRead(DmmFunction::DcCurrent, 0.02).value - 12.34e-3) < 3e-5);
        QVERIFY(std::abs(dev.dmmRead(DmmFunction::AcCurrent, 0.02).value - 5e-3) < 3e-5);
        QVERIFY(std::abs(dev.dmmRead(DmmFunction::Resistance, 20e3).value - 4700.0) < 30.0);
        QVERIFY(dev.dmmRead(DmmFunction::Resistance, 2e3).overload);
        QVERIFY(std::abs(dev.dmmRead(DmmFunction::Diode, 2.0).value - 0.65) < 0.003);
        QVERIFY(std::abs(dev.dmmRead(DmmFunction::Continuity, 200.0).value - 0.8) < 0.3);

        dev.setDmmOpenCircuit(true);
        QVERIFY(dev.dmmOpenCircuit());
        QVERIFY(dev.dmmRead(DmmFunction::Resistance, 20e6).overload);
        QVERIFY(dev.dmmRead(DmmFunction::Diode, 2.0).overload);
        QVERIFY(dev.dmmRead(DmmFunction::Continuity, 200.0).overload);
        QVERIFY(!dev.dmmRead(DmmFunction::DcVoltage, 20.0).overload);
        QCOMPARE(errorOf([&] { dev.dmmRead(DmmFunction::DcVoltage, 3.0); }), int(DeviceError::InvalidConfig));
    }

    void resetStopsTasksAndWakesReaders()
    {
        SimDevice dev;
        auto ao = playSine(dev, AoChannel::AO0, 100.0, 1.0);
        auto ai = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}}, 10e3, SampleMode::Continuous, 10000));
        ai->start();
        dev.dioSetDirection(0xFF, true);

        std::atomic<int> code{-2};
        std::thread reader([&] { code = errorOf([&] { ai->read(10000, 5000); }); });
        QThread::msleep(50);
        QElapsedTimer timer;
        timer.start();
        dev.reset();
        reader.join();
        QVERIFY(timer.elapsed() < 500);
        QCOMPARE(code.load(), int(DeviceError::Generic));
        QVERIFY(!ai->isRunning());
        QVERIFY(!ao->isRunning());
        QCOMPARE(dev.dioDirection(), uint8_t(0));
        ai.reset();
        ao.reset();
        dev.setNoiseRms(0.0);
        dev.setTestSignal(AiChannel::AI0, {dsp::Waveform::Dc, 0.0, 0.0, 0.25, 0.5, {}});
        QVERIFY(std::abs(dev.aiReadStatic(AiChannel::AI0, 2.0) - 0.25) < 1e-3);   // AO 0 back at 0 V
    }

    void disconnect()
    {
        SimDevice dev;
        auto ai = dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}}, 1000.0, SampleMode::Continuous, 1000));
        ai->start();
        dev.setConnected(false);
        QVERIFY(!dev.isConnected());
        QVERIFY(!ai->isRunning());
        QCOMPARE(errorOf([&] { ai->read(10, 100); }), int(DeviceError::NotConnected));
        QCOMPARE(errorOf([&] { dev.aoWriteStatic(AoChannel::AO0, 1.0); }), int(DeviceError::NotConnected));
        QCOMPARE(errorOf([&] { dev.dmmRead(DmmFunction::DcVoltage, 2.0); }), int(DeviceError::NotConnected));
        QCOMPARE(errorOf([&] { dev.dioRead(); }), int(DeviceError::NotConnected));
        ai.reset();
        QCOMPARE(errorOf([&] { dev.createAiTask(aiConfig({{AiChannel::AI0, 10.0}}, 1000.0, SampleMode::Finite, 10)); }),
                 int(DeviceError::NotConnected));
        dev.setConnected(true);
        QCOMPARE(errorOf([&] { dev.aiReadStatic(AiChannel::AI0, 10.0); }), kNoError);
    }
};

QTEST_GUILESS_MAIN(TestSimDevice)
#include "tst_simdevice.moc"
