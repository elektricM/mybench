#include "aeqcontroller.h"

#include <mybench/devicehub.h>
#include <mybench/notifications.h>
#include <mybench/units.h>

#include <mydaq/dsp/fft.h>
#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/trigger.h>

#include <QDateTime>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

constexpr double kRates[] = {50000.0, 100000.0, 200000.0};
constexpr double kLatencies[] = {0.05, 0.1, 0.25, 0.35};
constexpr double kTimeConstants[] = {0.010, 0.125, 1.0};
constexpr double kTimeSpans[] = {0.020, 0.100};   // Time view: 20 ms (triggered), 100 ms (ELVISmx)
constexpr double kBlockSeconds = 0.010;
constexpr double kPeakHoldSeconds = 1.5;

std::optional<mydaq::AiChannel> aiChannelOf(AeqController::Source s)
{
    switch (s) {
    case AeqController::SourceAI0: return mydaq::AiChannel::AI0;
    case AeqController::SourceAI1: return mydaq::AiChannel::AI1;
    case AeqController::SourceAudioInLeft: return mydaq::AiChannel::AudioInLeft;
    case AeqController::SourceAudioInRight: return mydaq::AiChannel::AudioInRight;
    case AeqController::SourceNone: break;
    }
    return std::nullopt;
}

std::optional<mydaq::AoChannel> aoChannelOf(AeqController::Sink s)
{
    switch (s) {
    case AeqController::SinkAO0: return mydaq::AoChannel::AO0;
    case AeqController::SinkAO1: return mydaq::AoChannel::AO1;
    case AeqController::SinkAudioOutLeft: return mydaq::AoChannel::AudioOutLeft;
    case AeqController::SinkAudioOutRight: return mydaq::AoChannel::AudioOutRight;
    case AeqController::SinkNone: break;
    }
    return std::nullopt;
}

double nowSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Circular buffer keeping the last `size` samples.
class Ring
{
public:
    explicit Ring(size_t size) : m_data(size, 0.0) {}
    void push(std::span<const double> x)
    {
        for (double v : x) {
            m_data[m_next] = v;
            m_next = (m_next + 1) % m_data.size();
        }
        m_filled = std::min(m_data.size(), m_filled + x.size());
    }
    bool full() const { return m_filled == m_data.size(); }
    size_t filled() const { return m_filled; }
    // The last n samples, oldest first.
    std::vector<double> last(size_t n) const
    {
        n = std::min(n, m_data.size());
        std::vector<double> out(n);
        const size_t start = (m_next + m_data.size() - n) % m_data.size();
        for (size_t i = 0; i < n; ++i)
            out[i] = m_data[(start + i) % m_data.size()];
        return out;
    }
    void clear()
    {
        std::fill(m_data.begin(), m_data.end(), 0.0);
        m_filled = 0;
    }

private:
    std::vector<double> m_data;
    size_t m_next = 0;
    size_t m_filled = 0;
};

}  // namespace

AeqController::AeqController() : InstrumentController(u"aeq"_s)
{
    setSupportsSingle(false);
    setRequiredResources(ResourceArbiter::AnalogInput | ResourceArbiter::AnalogOutput);
    resetMeters();
    updateResponse();
    connect(DeviceHub::instance(), &DeviceHub::currentChanged, this, &AeqController::routingChanged);
}

AeqController::~AeqController()
{
    shutdown();   // the worker lambda captures `this`
}

// --- Settings ---------------------------------------------------------------------------------

namespace {

bool isAudioSource(AeqController::Source s)
{
    return s == AeqController::SourceAudioInLeft || s == AeqController::SourceAudioInRight;
}
bool isAudioSink(AeqController::Sink s)
{
    return s == AeqController::SinkAudioOutLeft || s == AeqController::SinkAudioOutRight;
}
// The same side of the other kind: AI 0 <-> AudioInput Left, AI 1 <-> AudioInput Right.
AeqController::Source counterpart(AeqController::Source s)
{
    switch (s) {
    case AeqController::SourceAI0: return AeqController::SourceAudioInLeft;
    case AeqController::SourceAI1: return AeqController::SourceAudioInRight;
    case AeqController::SourceAudioInLeft: return AeqController::SourceAI0;
    case AeqController::SourceAudioInRight: return AeqController::SourceAI1;
    case AeqController::SourceNone: break;
    }
    return s;
}
AeqController::Sink counterpart(AeqController::Sink s)
{
    switch (s) {
    case AeqController::SinkAO0: return AeqController::SinkAudioOutLeft;
    case AeqController::SinkAO1: return AeqController::SinkAudioOutRight;
    case AeqController::SinkAudioOutLeft: return AeqController::SinkAO0;
    case AeqController::SinkAudioOutRight: return AeqController::SinkAO1;
    case AeqController::SinkNone: break;
    }
    return s;
}

}  // namespace

// Plain setters (settings and session restore write the four routing properties one at a time and
// must not rewrite each other). The UI uses chooseInput/chooseOutput below.
void AeqController::setInputLeft(Source source)
{
    if (source == m_inputLeft)
        return;
    m_inputLeft = source;
    emit routingChanged();
    restart();
}

void AeqController::setInputRight(Source source)
{
    if (source == m_inputRight)
        return;
    m_inputRight = source;
    emit routingChanged();
    restart();
}

void AeqController::setOutputLeft(Sink sink)
{
    if (sink == m_outputLeft)
        return;
    m_outputLeft = sink;
    emit routingChanged();
    restart();
}

void AeqController::setOutputRight(Sink sink)
{
    if (sink == m_outputRight)
        return;
    m_outputRight = sink;
    emit routingChanged();
    restart();
}

// AI 0/1 and the audio inputs share the converters (one AI task): choosing one kind for a side
// moves the other side to the same kind, like the Scope does, instead of failing at Run.
void AeqController::chooseInput(int side, int choice)
{
    const Source source = Source(std::clamp(choice, 0, int(SourceNone)));
    Source& other = side == 0 ? m_inputRight : m_inputLeft;
    const Source before = other;
    if (source != SourceNone && other != SourceNone && isAudioSource(other) != isAudioSource(source))
        (side == 0 ? m_inputRight : m_inputLeft) = counterpart(other);
    const bool followed = other != before;
    (side == 0 ? m_inputLeft : m_inputRight) = source;
    if (followed)
        Notifications::instance()->notify(Notifications::Info, title(),
            tr("Input (%1) switched to %2: AI 0/AI 1 and the audio inputs share the converters and cannot be mixed")
                .arg(side == 0 ? u"R"_s : u"L"_s, sourceNames().value(int(other))));
    emit routingChanged();
    restart();
}

void AeqController::chooseOutput(int side, int choice)
{
    const Sink sink = Sink(std::clamp(choice, 0, int(SinkNone)));
    Sink& other = side == 0 ? m_outputRight : m_outputLeft;
    const Sink before = other;
    if (sink != SinkNone && other != SinkNone && isAudioSink(other) != isAudioSink(sink))
        other = counterpart(other);
    const bool followed = other != before;
    (side == 0 ? m_outputLeft : m_outputRight) = sink;
    if (followed)
        Notifications::instance()->notify(Notifications::Info, title(),
            tr("Output (%1) switched to %2: AO 0/AO 1 and the audio outputs share the converters and cannot be mixed")
                .arg(side == 0 ? u"R"_s : u"L"_s, sinkNames().value(int(other))));
    emit routingChanged();
    restart();
}

void AeqController::useAnalogInputs()
{
    setInputLeft(SourceAI0);
    setInputRight(SourceAI1);
}

void AeqController::postLive() { m_liveEq->post(m_eq); }

void AeqController::setGain(aeq::Band band, double db)
{
    db = std::clamp(std::round(db * 10.0) / 10.0, -aeq::kMaxGainDb, aeq::kMaxGainDb);
    if (!std::isfinite(db) || db == m_eq.gainsDb[band])
        return;
    m_eq.gainsDb[band] = db;
    postLive();
    updateResponse();
    emit eqChanged();
}

void AeqController::flat()
{
    for (int b = 0; b < aeq::BandCount; ++b)
        m_eq.gainsDb[size_t(b)] = 0;
    postLive();
    updateResponse();
    emit eqChanged();
}

void AeqController::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (!std::isfinite(volume) || volume == m_eq.volume)
        return;
    m_eq.volume = volume;
    postLive();
    emit eqChanged();
}

void AeqController::setBypass(bool bypass)
{
    if (bypass == m_eq.bypass)
        return;
    m_eq.bypass = bypass;
    postLive();
    updateResponse();
    emit eqChanged();
}

void AeqController::setRateIndex(int index)
{
    index = std::clamp(index, 0, int(std::size(kRates)) - 1);
    if (index == m_rateIndex)
        return;
    m_rateIndex = index;
    updateResponse();
    emit streamChanged();
    restart();
}

void AeqController::setLatencyIndex(int index)
{
    index = std::clamp(index, 0, int(std::size(kLatencies)) - 1);
    if (index == m_latencyIndex)
        return;
    m_latencyIndex = index;
    emit streamChanged();
    restart();
}

void AeqController::setView(View view)
{
    if (view == m_view)
        return;
    m_view = view;
    emit displayChanged();
}

void AeqController::setDisplayChannel(int channel)
{
    channel = std::clamp(channel, 0, 1);
    if (channel == m_displayChannel)
        return;
    m_displayChannel = channel;
    m_liveDisplay->post(display());
    emit displayChanged();
}

void AeqController::setAveragingIndex(int index)
{
    index = std::clamp(index, 0, int(std::size(kTimeConstants)) - 1);
    if (index == m_averagingIndex)
        return;
    m_averagingIndex = index;
    m_liveDisplay->post(display());
    emit displayChanged();
}

AeqController::Display AeqController::display() const
{
    return {m_displayChannel, kTimeConstants[m_averagingIndex], kTimeSpans[m_timeSpanIndex]};
}

QStringList AeqController::timeSpanNames() const
{
    return {tr("20 ms"), tr("100 ms")};
}

double AeqController::timeSpan() const
{
    return kTimeSpans[m_timeSpanIndex];
}

void AeqController::setTimeSpanIndex(int index)
{
    index = std::clamp(index, 0, int(std::size(kTimeSpans)) - 1);
    if (index == m_timeSpanIndex)
        return;
    m_timeSpanIndex = index;
    m_liveDisplay->post(display());
    emit displayChanged();
}

void AeqController::setAutoScaleTime(bool on)
{
    if (on == m_autoScaleTime)
        return;
    m_autoScaleTime = on;
    emit displayChanged();
}

void AeqController::setAutoScaleSpectrum(bool on)
{
    if (on == m_autoScaleSpectrum)
        return;
    m_autoScaleSpectrum = on;
    emit displayChanged();
}

void AeqController::setShowInput(bool show)
{
    if (show == m_showInput)
        return;
    m_showInput = show;
    emit displayChanged();
}

// --- Derived ----------------------------------------------------------------------------------

QStringList AeqController::sourceNames() const
{
    return {u"AI 0"_s, u"AI 1"_s, tr("AudioInput Left"), tr("AudioInput Right"), tr("None")};
}

QStringList AeqController::sinkNames() const
{
    return {u"AO 0"_s, u"AO 1"_s, tr("AudioOutput Left"), tr("AudioOutput Right"), tr("None")};
}

QStringList AeqController::rateNames() const
{
    QStringList names;
    for (double r : kRates)
        names << Units::format(r, u"S/s"_s, 3);
    return names;
}

QStringList AeqController::latencyNames() const
{
    QStringList names;
    for (double l : kLatencies)
        names << Units::format(l, u"s"_s, 3);
    return names;
}

QStringList AeqController::averagingNames() const
{
    return {tr("10 ms (ELVISmx)"), tr("125 ms (Fast)"), tr("1 s (Slow)")};
}

QString AeqController::routingError() const
{
    const auto inL = aiChannelOf(m_inputLeft);
    const auto inR = aiChannelOf(m_inputRight);
    const auto outL = aoChannelOf(m_outputLeft);
    const auto outR = aoChannelOf(m_outputRight);
    if (!inL && !inR)
        return tr("Choose at least one input.");
    if (!outL && !outR)
        return tr("Choose at least one output.");
    if (inL && inR && mydaq::isAudio(*inL) != mydaq::isAudio(*inR))
        return tr("AI 0/AI 1 and the audio inputs share the converters: choose inputs of one kind.");
    if (outL && outR && mydaq::isAudio(*outL) != mydaq::isAudio(*outR))
        return tr("AO 0/AO 1 and the audio outputs share the converters: choose outputs of one kind.");
    if (outL && outR && *outL == *outR)
        return tr("Output (L) and Output (R) are the same channel.");
    return {};
}

bool AeqController::feedbackInSimulator() const
{
    if (!DeviceHub::instance()->isSimulated())
        return false;
    const auto has = [&](Sink s) { return m_outputLeft == s || m_outputRight == s; };
    const auto hasIn = [&](Source s) { return m_inputLeft == s || m_inputRight == s; };
    return (hasIn(SourceAudioInLeft) && has(SinkAudioOutLeft)) || (hasIn(SourceAudioInRight) && has(SinkAudioOutRight))
        || ((hasIn(SourceAI0) || hasIn(SourceAI1)) && has(SinkAO0));
}

double AeqController::streamRate() const
{
    const double requested = kRates[m_rateIndex];
    if (const mydaq::DevicePtr device = DeviceHub::instance()->device())
        return device->coerceAiRate(requested);
    return requested;
}

double AeqController::latencySeconds() const { return kLatencies[m_latencyIndex]; }
double AeqController::blockSeconds() const { return kBlockSeconds; }

void AeqController::updateResponse()
{
    // Tone response (volume not included) on a log grid up to the band limit.
    const double fs = kRates[m_rateIndex];
    const aeq::Equalizer eq(fs);
    aeq::EqSettings unity = m_eq;
    unity.volume = 1.0;
    const double top = std::min(25000.0, 0.49 * fs);
    constexpr int kPoints = 320;
    std::vector<double> f(kPoints), total(kPoints), bands[aeq::BandCount];
    for (auto& b : bands)
        b.resize(kPoints);
    const std::array<double, aeq::BandCount> g = m_eq.linearGains();
    auto db = [](double magnitude) { return 20.0 * std::log10(std::max(magnitude, 1e-9)); };
    for (int i = 0; i < kPoints; ++i) {
        f[size_t(i)] = 20.0 * std::pow(top / 20.0, double(i) / (kPoints - 1));
        total[size_t(i)] = db(std::abs(eq.response(f[size_t(i)], unity)));
        for (int b = 0; b < aeq::BandCount; ++b)
            bands[b][size_t(i)] = db(g[size_t(b)] * std::abs(eq.bandResponse(aeq::Band(b), f[size_t(i)])));
    }
    m_response.setXY(f, total);
    m_responseBass.setXY(f, bands[aeq::Bass]);
    m_responseMid.setXY(f, bands[aeq::Mid]);
    m_responseTreble.setXY(f, bands[aeq::Treble]);
}

void AeqController::resetMeters()
{
    QVariantList meters;
    for (const QString& name : {u"In L"_s, u"In R"_s, u"Out L"_s, u"Out R"_s})
        meters.append(QVariantMap{{u"name"_s, name}, {u"peak"_s, -120.0}, {u"rms"_s, -120.0},
                                  {u"hold"_s, -120.0}, {u"active"_s, false}});
    m_meters = meters;
    m_clipping = false;
    emit metersChanged();
}

// --- Run --------------------------------------------------------------------------------------

InstrumentController::WorkerFunction AeqController::createWorker()
{
    if (const QString error = routingError(); !error.isEmpty())
        throw InvalidInput(error);

    struct Snapshot {
        std::vector<mydaq::AiChannel> inputs;   // task channels (unique)
        int inputOfSide[2] = {-1, -1};         // L/R -> index in inputs
        std::vector<mydaq::AoChannel> outputs;
        std::vector<int> sideOfOutput;          // output -> 0 (L) / 1 (R)
        double rate = 50000;
        double latency = 0.1;
    } snap;
    const Source sources[2] = {m_inputLeft, m_inputRight};
    for (int side = 0; side < 2; ++side) {
        if (const auto ch = aiChannelOf(sources[side])) {
            const auto it = std::find(snap.inputs.begin(), snap.inputs.end(), *ch);
            snap.inputOfSide[side] = int(it - snap.inputs.begin());
            if (it == snap.inputs.end())
                snap.inputs.push_back(*ch);
        }
    }
    const Sink sinks[2] = {m_outputLeft, m_outputRight};
    for (int side = 0; side < 2; ++side) {
        if (const auto ch = aoChannelOf(sinks[side])) {
            snap.outputs.push_back(*ch);
            snap.sideOfOutput.push_back(side);
        }
    }
    snap.rate = kRates[m_rateIndex];
    snap.latency = kLatencies[m_latencyIndex];
    m_liveEq->clear();
    m_liveDisplay->clear();
    m_dropouts = 0;
    resetMeters();

    return [this, snap, eqSettings = m_eq, display = display(),
            liveEq = m_liveEq, liveDisplay = m_liveDisplay](WorkerContext& ctx) mutable {
        mydaq::Device& device = ctx.device();
        const double fs = device.coerceAiRate(snap.rate);
        const auto block = size_t(std::max(64.0, std::round(fs * kBlockSeconds)));
        const auto prefill = size_t(std::round(snap.latency * fs));
        ctx.setSampleRate(fs);

        mydaq::AiConfig aiConfig;
        for (mydaq::AiChannel ch : snap.inputs)
            aiConfig.channels.push_back({ch, kFullScale});
        aiConfig.sampleRate = fs;
        aiConfig.mode = mydaq::SampleMode::Continuous;
        aiConfig.samplesPerChannel = int64_t(fs);
        mydaq::AoConfig aoConfig;
        for (mydaq::AoChannel ch : snap.outputs)
            aoConfig.channels.push_back({ch, kFullScale});
        aoConfig.sampleRate = fs;
        aoConfig.mode = mydaq::SampleMode::Continuous;
        aoConfig.regenerate = false;

        std::unique_ptr<mydaq::AiTask> ai;
        std::unique_ptr<mydaq::AoTask> ao;
        std::vector<aeq::Equalizer> equalizers(snap.outputs.size(), aeq::Equalizer(fs));
        auto startStreams = [&] {
            ao.reset();
            ai.reset();
            ai = device.createAiTask(aiConfig);
            ao = device.createAoTask(aoConfig);
            ao->write(std::vector<std::vector<double>>(snap.outputs.size(), std::vector<double>(prefill, 0.0)));
            for (aeq::Equalizer& e : equalizers)
                e.reset();
            ao->start();
            ai->start();
        };
        startStreams();
        ctx.setStatus(tr("Streaming · latency %1").arg(Units::format(snap.latency + kBlockSeconds, u"s"_s, 3)));

        // Display state.
        const size_t fftLength = aeq::maximumAnalysisLength(fs);   // 1 s history; the FFT grows to it
        Ring ringIn(fftLength);
        Ring ringOut(fftLength);
        const aeq::OctaveBands bands = aeq::octaveBands(24, 25.0, std::min(25000.0, 0.48 * fs));
        std::vector<double> avgIn(bands.centers.size(), 0.0);
        std::vector<double> avgOut(bands.centers.size(), 0.0);
        bool averaged = false;
        double lastSpectrum = nowSeconds();
        double lastMeters = lastSpectrum;
        double lastClip = -1e9;
        double lastWarning = -1e9;
        struct Meter {
            double peak = -120, rms = -120, hold = -120, holdTime = 0;
            double sumSquares = 0, maxAbs = 0;
            size_t count = 0;
            bool active = false;
        };
        std::array<Meter, 4> meters;   // In L, In R, Out L, Out R
        for (int side = 0; side < 2; ++side)
            meters[size_t(side)].active = snap.inputOfSide[side] >= 0;
        for (int side : snap.sideOfOutput)
            meters[2 + size_t(side)].active = true;
        int dropouts = 0;
        // Under- and overflows share one warning toast, at most every 5 s.
        auto warnRestart = [&](const QString& text) {
            const double t = nowSeconds();
            if (t - lastWarning > 5.0) {
                lastWarning = t;
                ctx.warn(text);
            }
        };

        std::vector<std::vector<double>> outputs(snap.outputs.size(), std::vector<double>(block));
        while (!ctx.stopRequested()) {
            if (auto s = liveEq->take())
                eqSettings = *s;
            if (auto d = liveDisplay->take()) {
                if (d->channel != display.channel) {
                    ringIn.clear();
                    ringOut.clear();
                    averaged = false;
                }
                display = *d;
            }

            std::vector<std::vector<double>> input;
            try {
                input = ai->read(int64_t(block), 200);
            } catch (const mydaq::DeviceError& error) {
                if (error.code() == mydaq::DeviceError::Timeout)
                    continue;
                if (error.code() != mydaq::DeviceError::Overflow)
                    throw;
                ++dropouts;
                warnRestart(tr("The input overflowed; the audio stream was restarted. A shorter block or a lower rate helps."));
                startStreams();
                continue;
            }

            // Filter every output from its side's input (a single input feeds both sides).
            bool clipped = false;
            for (size_t o = 0; o < snap.outputs.size(); ++o) {
                const int side = snap.sideOfOutput[o];
                int in = snap.inputOfSide[side];
                if (in < 0)
                    in = snap.inputOfSide[1 - side];
                equalizers[o].process(input[size_t(in)], outputs[o], eqSettings);
                for (double& v : outputs[o]) {
                    if (std::abs(v) > kFullScale) {
                        v = std::clamp(v, -kFullScale, kFullScale);
                        clipped = true;
                    }
                }
            }
            try {
                ao->write(outputs, 200);
            } catch (const mydaq::DeviceError& error) {
                if (error.code() != mydaq::DeviceError::Underflow)
                    throw;
                ++dropouts;
                warnRestart(tr("The output ran dry; the audio stream was restarted. A longer latency helps."));
                startStreams();
                continue;
            }
            ctx.addSamples(qint64(block * (snap.inputs.size() + snap.outputs.size())));
            const double now = nowSeconds();
            if (clipped)
                lastClip = now;

            // Meters.
            auto accumulate = [](Meter& m, std::span<const double> x) {
                for (double v : x) {
                    m.maxAbs = std::max(m.maxAbs, std::abs(v));
                    m.sumSquares += v * v;
                }
                m.count += x.size();
            };
            for (int side = 0; side < 2; ++side) {
                if (snap.inputOfSide[side] >= 0)
                    accumulate(meters[size_t(side)], input[size_t(snap.inputOfSide[side])]);
            }
            for (size_t o = 0; o < snap.outputs.size(); ++o)
                accumulate(meters[2 + size_t(snap.sideOfOutput[o])], outputs[o]);

            // Display channel rings.
            const int dside = display.channel;
            int din = snap.inputOfSide[dside] >= 0 ? snap.inputOfSide[dside] : snap.inputOfSide[1 - dside];
            ringIn.push(input[size_t(din)]);
            int dout = -1;
            for (size_t o = 0; o < snap.outputs.size(); ++o) {
                if (snap.sideOfOutput[o] == dside || dout < 0)
                    dout = int(o);
            }
            if (dout >= 0)
                ringOut.push(outputs[size_t(dout)]);

            if (now - lastMeters >= 0.04) {
                QVariantList list;
                static const char* const names[] = {"In L", "In R", "Out L", "Out R"};
                for (size_t i = 0; i < meters.size(); ++i) {
                    Meter& m = meters[i];
                    if (m.count > 0) {
                        m.peak = m.maxAbs > 0 ? std::max(-120.0, 20.0 * std::log10(m.maxAbs / kFullScale)) : -120.0;
                        const double rms = std::sqrt(m.sumSquares / double(m.count));
                        m.rms = rms > 0 ? std::max(-120.0, 20.0 * std::log10(rms / kFullScale)) : -120.0;
                    }
                    if (m.peak >= m.hold || now - m.holdTime > kPeakHoldSeconds) {
                        m.hold = m.peak;
                        m.holdTime = now;
                    }
                    m.maxAbs = m.sumSquares = 0;
                    m.count = 0;
                    list.append(QVariantMap{{u"name"_s, QString::fromLatin1(names[i])}, {u"peak"_s, m.peak},
                                            {u"rms"_s, m.rms}, {u"hold"_s, m.hold}, {u"active"_s, m.active}});
                }
                const bool clipping = now - lastClip < 1.0;
                ctx.publish([this, list, clipping, dropouts] {
                    m_meters = list;
                    m_clipping = clipping;
                    m_dropouts = dropouts;
                    emit metersChanged();
                }, 1);
                lastMeters = now;
            }

            const size_t analysisSize = aeq::analysisLength(ringIn.filled(), fs);
            if (now - lastSpectrum >= 0.1 && analysisSize > 0) {
                const double dt = now - lastSpectrum;
                lastSpectrum = now;
                const double alpha = averaged ? 1.0 - std::exp(-dt / std::max(1e-3, display.timeConstant)) : 1.0;
                auto analyse = [&](const Ring& ring, std::vector<double>& avg, plot::TraceBuffer& trace) {
                    const mydaq::dsp::PowerSpectrum ps =
                        mydaq::dsp::powerSpectrum(ring.last(analysisSize), fs, mydaq::dsp::WindowType::Hanning);
                    const std::vector<double> p = aeq::bandPowers(bands, ps.power, ps.info.df);
                    std::vector<double> db(p.size());
                    for (size_t b = 0; b < p.size(); ++b) {
                        avg[b] += alpha * (p[b] / ps.info.enbw - avg[b]);
                        db[b] = avg[b] > 0 ? std::max(-140.0, 10.0 * std::log10(avg[b])) : -140.0;
                    }
                    trace.setXY(bands.centers, db);
                };
                analyse(ringIn, avgIn, m_spectrumIn);
                if (dout >= 0)
                    analyse(ringOut, avgOut, m_spectrumOut);
                averaged = true;

                // Waveforms: 20 ms from a rising zero crossing of the input; the 100 ms view
                // (ELVISmx) shows the latest block as it is.
                const auto window = size_t(std::round(fs * display.timeSpan));
                const std::vector<double> in = ringIn.last(2 * window);
                const std::vector<double> out = ringOut.last(2 * window);
                size_t start = window;
                mydaq::dsp::EdgeTrigger trigger;
                trigger.level = 0;
                trigger.hysteresis = 0.01;
                if (display.timeSpan <= 0.0201) {
                    if (const auto edge = mydaq::dsp::findEdge(in, trigger); edge && *edge <= double(window))
                        start = size_t(*edge);
                }
                m_timeIn.setUniform(0.0, 1.0 / fs, std::span<const double>(in).subspan(start, window));
                if (dout >= 0)
                    m_timeOut.setUniform(0.0, 1.0 / fs, std::span<const double>(out).subspan(start, window));
                ctx.acquisitionDone();
            }
        }
        ao.reset();
        ai.reset();
        // Safe state: the analog outputs back to 0 V (the audio outputs need nothing).
        for (mydaq::AoChannel ch : snap.outputs) {
            if (!mydaq::isAudio(ch)) {
                try {
                    device.aoWriteStatic(ch, 0.0);
                } catch (const mydaq::DeviceError&) {
                }
            }
        }
    };
}

void AeqController::onFinished()
{
    resetMeters();
}

DataTable AeqController::exportTable() const
{
    DataTable table;
    table.title = title();
    table.instrumentId = instrumentId();
    table.addMetadata(tr("Input (L)"), sourceNames().value(m_inputLeft));
    table.addMetadata(tr("Input (R)"), sourceNames().value(m_inputRight));
    table.addMetadata(tr("Output (L)"), sinkNames().value(m_outputLeft));
    table.addMetadata(tr("Output (R)"), sinkNames().value(m_outputRight));
    table.addMetadata(tr("Bass"), Units::decibels(bass(), 1));
    table.addMetadata(tr("Midtone"), Units::decibels(midtone(), 1));
    table.addMetadata(tr("Treble"), Units::decibels(treble(), 1));
    table.addMetadata(tr("Volume"), QString::number(volume(), 'f', 2));
    table.addMetadata(tr("Bypass"), bypass() ? tr("on") : tr("off"));
    table.addMetadata(tr("Sample rate"), Units::format(streamRate(), u"S/s"_s));
    table.addMetadata(tr("Spectrum"), tr("1/24 octave, channel %1").arg(m_displayChannel ? u"R"_s : u"L"_s));
    auto add = [&](const plot::TraceBuffer& buffer, const QString& name, const QString& unit, bool withX,
                   const QString& xName) {
        const plot::TraceSnapshot s = buffer.snapshot();
        if (!s || s->empty())
            return;
        std::vector<double> x(s->size()), y(s->size());
        for (size_t i = 0; i < s->size(); ++i) {
            x[i] = s->xAt(i);
            y[i] = s->yAt(i);
        }
        if (withX)
            table.addColumn(xName, u"Hz"_s, std::move(x));
        table.addColumn(name, unit, std::move(y));
    };
    add(m_spectrumIn, tr("Input"), u"dBV"_s, true, tr("Band center"));
    add(m_spectrumOut, tr("Output"), u"dBV"_s, m_spectrumIn.count() == 0, tr("Band center"));
    add(m_response, tr("Tone response"), u"dB"_s, true, tr("Frequency"));
    return table;
}

void registerAeq(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"aeq"_s,
        .title = QObject::tr("Audio Equalizer"),
        .shortTitle = QObject::tr("EQ"),
        .description = QObject::tr("Live 3-band equalizer from the audio input to the audio output"),
        .module = u"MyBench.Instruments.Aeq"_s,
        .icon = u"qrc:/qt/qml/MyBench/Instruments/Aeq/assets/instrument-aeq.svg"_s,
        .order = 90,
        .createController = [] { return new AeqController; },
    });
}

}  // namespace mybench::instruments
