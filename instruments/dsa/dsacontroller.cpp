#include "dsacontroller.h"

#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/window.h>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

constexpr std::array kLines{100, 200, 400, 800, 1600, 3200};

}  // namespace

DsaController::DsaController() : InstrumentController(u"dsa"_s)
{
    setRequiredResources(ResourceArbiter::AnalogInput);
}

DsaController::~DsaController()
{
    shutdown();
}

void DsaController::setSourceChannel(int channel)
{
    channel = std::clamp(channel, 0, 1);
    if (channel != m_source) {
        m_source = channel;
        emit sourceChannelChanged();
        restart();
    }
}

void DsaController::setFrequencySpan(double hertz)
{
    hertz = std::clamp(hertz, 10.0, 80000.0);
    if (hertz != m_span) {
        m_span = hertz;
        emit frequencySpanChanged();
        restart();
    }
}

void DsaController::setResolutionIndex(int index)
{
    index = std::clamp(index, 0, int(kLines.size()) - 1);
    if (index != m_resolutionIndex) {
        m_resolutionIndex = index;
        emit resolutionIndexChanged();
        restart();
    }
}

void DsaController::setWindowIndex(int index)
{
    index = std::clamp(index, 0, int(mydaq::dsp::windowTypes().size()) - 1);
    if (index != m_windowIndex) {
        m_windowIndex = index;
        emit windowIndexChanged();
        restart();
    }
}

QStringList DsaController::resolutionLabels() const
{
    QStringList labels;
    for (int lines : kLines)
        labels.append(QString::number(lines));
    return labels;
}

QStringList DsaController::windowLabels() const
{
    QStringList labels;
    for (mydaq::dsp::WindowType type : mydaq::dsp::windowTypes())
        labels.append(mydaq::dsp::windowName(type));
    return labels;
}

InstrumentController::WorkerFunction DsaController::createWorker()
{
    mydaq::AiConfig config;
    config.channels = {{m_source == 0 ? mydaq::AiChannel::AI0 : mydaq::AiChannel::AI1, 10.0}};
    config.sampleRate = std::min(200000.0, 2.56 * m_span);
    config.samplesPerChannel = int64_t(2.56 * kLines[size_t(m_resolutionIndex)]);
    config.mode = mydaq::SampleMode::Finite;
    const mydaq::dsp::WindowType window = mydaq::dsp::windowTypes()[size_t(m_windowIndex)];

    // TODO(dsa agent): averaging, display units/modes, trigger, markers, spectrogram.
    return [this, config, window](WorkerContext& ctx) {
        auto task = ctx.device().createAiTask(config);
        const double rate = task->sampleRate();
        ctx.setSampleRate(rate);
        ctx.setStatus(tr("Acquiring"));
        const int timeoutMs = int(1000.0 * double(config.samplesPerChannel) / rate) + 500;
        while (!ctx.stopRequested()) {
            task->start();
            const auto data = task->read(config.samplesPerChannel, timeoutMs);
            ctx.addSamples(config.samplesPerChannel);
            const mydaq::dsp::PowerSpectrum spectrum = mydaq::dsp::powerSpectrum(data[0], rate, window);
            const mydaq::dsp::PeakEstimate peak = mydaq::dsp::findPeak(spectrum);
            m_spectrumTrace.setUniform(0.0, spectrum.info.df, mydaq::dsp::formatSpectrum(spectrum, {}));
            m_timeTrace.setUniform(0.0, 1.0 / rate, data[0]);
            ctx.publish([this, peak, frequencies = spectrum.frequencies(), power = spectrum.power]() mutable {
                m_fundamental = peak.frequency;
                m_power = peak.power;
                m_frequencies = std::move(frequencies);
                m_spectrum = std::move(power);
                emit resultsChanged();
            });
            ctx.acquisitionDone();
            if (ctx.once() || !ctx.sleepMs(15))
                break;
        }
    };
}

DataTable DsaController::exportTable() const
{
    DataTable table;
    table.addColumn(tr("Frequency"), u"Hz"_s, m_frequencies);
    table.addColumn(tr("Power"), u"V²"_s, m_spectrum);
    table.addMetadata(tr("Window"), windowLabels().value(m_windowIndex));
    return table;
}

void registerDsa(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"dsa"_s,
        .title = QObject::tr("Spectrum Analyzer"),
        .shortTitle = QObject::tr("Spec"),
        .description = QObject::tr("FFT spectrum, THD, SINAD, averaging"),
        .module = u"MyBench.Instruments.Dsa"_s,
        .icon = u"instrument-dsa"_s,
        .order = 50,
        .createController = [] { return new DsaController; },
    });
}

}  // namespace mybench::instruments
