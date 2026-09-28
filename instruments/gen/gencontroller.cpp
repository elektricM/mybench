#include "gencontroller.h"

#include <mybench/units.h>

#include <mydaq/dsp/waveform.h>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

mydaq::dsp::Waveform toDsp(GenController::Waveform waveform)
{
    switch (waveform) {
    case GenController::Square:
        return mydaq::dsp::Waveform::Square;
    case GenController::Triangle:
        return mydaq::dsp::Waveform::Triangle;
    case GenController::Sine:
        break;
    }
    return mydaq::dsp::Waveform::Sine;
}

QString waveformName(GenController::Waveform waveform)
{
    switch (waveform) {
    case GenController::Square:
        return QObject::tr("square");
    case GenController::Triangle:
        return QObject::tr("triangle");
    case GenController::Sine:
        break;
    }
    return QObject::tr("sine");
}

}  // namespace

GenController::GenController() : InstrumentController(u"gen"_s)
{
    setRequiredResources(ResourceArbiter::AnalogOutput);
    setSupportsSingle(false);   // TODO(gen agent): Single = one pass of the buffer, Sweep
    updatePreview();
}

void GenController::updatePreview()
{
    // Two periods of the slower enabled channel, both channels on the same time axis.
    double slowest = 0;
    for (const Channel& c : m_ch) {
        if (c.enabled && c.frequency > 0)
            slowest = slowest == 0 ? c.frequency : std::min(slowest, c.frequency);
    }
    const double duration = slowest > 0 ? 2.0 / slowest : 0.002;
    constexpr size_t points = 1000;
    for (size_t i = 0; i < m_preview.size(); ++i) {
        const Channel& c = m_ch[i];
        if (!c.enabled) {
            m_preview[i].clear();
            continue;
        }
        mydaq::dsp::WaveformParams params;
        params.type = toDsp(c.waveform);
        params.frequency = c.frequency;
        params.amplitude = c.amplitude / 2;
        params.offset = c.offset;
        m_preview[i].setUniform(0.0, duration / double(points - 1),
                                mydaq::dsp::generate(params, double(points - 1) / duration, points));
    }
}

GenController::~GenController()
{
    shutdown();
}

QString GenController::outputSummary() const
{
    if (!isRunning())
        return tr("Off");
    QStringList parts;
    for (int i = 0; i < 2; ++i) {
        const Channel& c = m_ch[size_t(i)];
        if (c.enabled)
            parts.append(u"AO %1  %2 %3 %4"_s.arg(i).arg(Units::format(c.frequency, u"Hz"_s), waveformName(c.waveform),
                                                   Units::format(c.amplitude, u"Vpp"_s, 3)));
    }
    return parts.join(u"   "_s);
}

InstrumentController::WorkerFunction GenController::createWorker()
{
    if (!m_ch[0].enabled && !m_ch[1].enabled)
        throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig, tr("enable AO 0 or AO 1"));
    const std::array<Channel, 2> channels = m_ch;

    // TODO(gen agent): arbitrary waveforms, sweep, streaming/DDS for independent frequencies,
    // audio outputs (docs/instruments/gen.md).
    return [channels](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        mydaq::AoConfig config;
        const Channel& reference = channels[0].enabled ? channels[0] : channels[1];
        const mydaq::dsp::BufferPlan plan = mydaq::dsp::planPeriodicBuffer(
            reference.frequency, device.maxAoRate(), [&device](double rate) { return device.coerceAoRate(rate); });
        std::vector<std::vector<double>> buffer;
        for (int i = 0; i < 2; ++i) {
            const Channel& c = channels[size_t(i)];
            if (!c.enabled)
                continue;
            config.channels.push_back({i == 0 ? mydaq::AoChannel::AO0 : mydaq::AoChannel::AO1, 10.0});
            mydaq::dsp::WaveformParams params;
            params.type = toDsp(c.waveform);
            params.frequency = c.frequency;
            params.amplitude = c.amplitude / 2;
            params.offset = c.offset;
            buffer.push_back(mydaq::dsp::generate(params, plan.sampleRate, plan.length));
        }
        config.sampleRate = plan.sampleRate;
        config.mode = mydaq::SampleMode::Continuous;
        config.regenerate = true;
        {
            auto task = device.createAoTask(config);
            task->write(buffer);
            task->start();
            ctx.setSampleRate(task->sampleRate());
            ctx.setStatus(tr("Generating"));
            while (ctx.sleepMs(100))
                ctx.addSamples(int64_t(task->sampleRate() / 10) * int64_t(buffer.size()));
            task->stop();
        }
        // Safe state: outputs back to 0 V once the task is gone.
        for (const mydaq::AoChannelConfig& channel : config.channels)
            device.aoWriteStatic(channel.channel, 0.0);
    };
}

void registerGen(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"gen"_s,
        .title = QObject::tr("Waveform Generator"),
        .shortTitle = QObject::tr("Gen"),
        .description = QObject::tr("Function and arbitrary waveforms on AO 0 / AO 1"),
        .module = u"MyBench.Instruments.Gen"_s,
        .icon = u"instrument-gen"_s,
        .order = 30,
        .createController = [] { return new GenController; },
    });
}

}  // namespace mybench::instruments
