#include "genengine.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <numbers>

namespace mybench::instruments::gen {

namespace {

constexpr double kTwoPow64 = 18446744073709551616.0;
constexpr double kTwoPow64Inverse = 1.0 / 18446744073709551616.0;

QString tr(const char* text)
{
    return QCoreApplication::translate("GenEngine", text);
}

double clip(double v, double range)
{
    return std::clamp(v, -range, range);
}

bool isAudio(OutputPair pair)
{
    return pair == OutputPair::AudioOutputs;
}

size_t waveformLength(const ArbSettings& arb)
{
    return arb.waveform ? std::max<size_t>(arb.waveform->size(), 2) : 0;
}

// gain x waveform, clipped to +-range, at least 2 samples (a 1-sample waveform repeats: DC).
std::vector<double> scaledWaveform(const ArbSettings& arb, double range)
{
    std::vector<double> out;
    if (!arb.waveform || arb.waveform->empty())
        return std::vector<double>(2, 0.0);
    out.reserve(std::max<size_t>(arb.waveform->size(), 2));
    for (double v : *arb.waveform)
        out.push_back(clip(arb.gain * v, range));
    if (out.size() < 2)
        out.push_back(out.front());
    return out;
}

}  // namespace

// --- Helpers ------------------------------------------------------------------------------------

mydaq::AoChannel aoChannel(OutputPair pair, int index)
{
    if (isAudio(pair))
        return index == 0 ? mydaq::AoChannel::AudioOutLeft : mydaq::AoChannel::AudioOutRight;
    return index == 0 ? mydaq::AoChannel::AO0 : mydaq::AoChannel::AO1;
}

double outputRange(OutputPair pair)
{
    return isAudio(pair) ? 2.0 : 10.0;
}

FgenPlan planFgenBuffer(double frequency, double maxRate, const std::function<double(double)>& coerceRate)
{
    FgenPlan plan;
    const double f = std::max(frequency, 1e-6);
    size_t length = 10000;
    double rate = f * 10000.0;
    if (rate > maxRate) {
        // One period of round(Rmax / f) samples at a rate at or just below Rmax.
        length = size_t(std::max(2.0, std::round(maxRate / f)));
        while (length > 2 && f * double(length) > maxRate * (1 + 1e-12))
            --length;
        rate = f * double(length);
    }
    plan.requestedRate = rate;
    plan.rate = coerceRate ? coerceRate(rate) : rate;
    plan.length = length;
    plan.frequency = plan.rate / double(length);
    return plan;
}

SweepPlan makeSweep(const SweepSettings& sweep)
{
    SweepPlan plan;
    const double direction = sweep.stop >= sweep.start ? 1.0 : -1.0;
    const double step = std::abs(sweep.step);
    const double tolerance = 1e-9 * std::max(std::abs(sweep.start), std::abs(sweep.stop));
    if (step <= 0 || sweep.start == sweep.stop) {
        plan.frequencies.push_back(sweep.start);
    } else {
        for (size_t k = 0; k < 1'000'000; ++k) {
            const double f = sweep.start + double(k) * step * direction;
            if (direction > 0 ? f > sweep.stop + tolerance : f < sweep.stop - tolerance)
                break;
            plan.frequencies.push_back(f);
        }
    }
    const double interval = std::clamp(sweep.intervalMs, 1.0, 300000.0) / 1000.0;
    plan.stepDuration = sweep.blockTiming ? std::ceil(interval / 0.25 - 1e-9) * 0.25 : interval;
    return plan;
}

double shapeValue(Shape shape, double cycles, double duty)
{
    const double u = cycles - std::floor(cycles);
    switch (shape) {
    case Shape::Sine: return std::sin(2.0 * std::numbers::pi * u);
    case Shape::Triangle: return u < 0.25 ? 4.0 * u : u < 0.75 ? 2.0 - 4.0 * u : 4.0 * u - 4.0;
    case Shape::Square: return u < duty ? 1.0 : -1.0;
    }
    return 0.0;
}

std::vector<double> functionPeriod(const FunctionSettings& function, size_t length, double range)
{
    std::vector<double> out(length);
    const double duty = std::clamp(function.dutyCycle, 0.0, 100.0) / 100.0;
    const double phase = function.phase / 360.0;
    for (size_t k = 0; k < length; ++k) {
        const double cycles = double(k) / double(length) + phase;
        out[k] = clip(function.offset + function.amplitude / 2.0 * shapeValue(function.shape, cycles, duty), range);
    }
    return out;
}

uint64_t phaseIncrement(double frequency, double rate)
{
    if (!(rate > 0) || !(frequency > 0))
        return 0;
    const double cycles = std::min(frequency / rate, 0.5);
    return uint64_t(std::llround(cycles * kTwoPow64 / 2.0)) * 2;   // llround range: work in halves
}

double ddsFrequency(uint64_t increment, double rate)
{
    return double(increment) * kTwoPow64Inverse * rate;
}

bool isRegenerable(const EngineSettings& settings)
{
    const ChannelSettings* function = nullptr;
    const ChannelSettings* arbitrary = nullptr;
    bool any = false;
    bool dc = false;
    for (const ChannelSettings& c : settings.channels) {
        if (!c.generating())
            continue;
        any = true;
        switch (c.mode) {
        case ChannelMode::Function:
            if (c.sweep)
                return false;
            if (function && function->function.frequency != c.function.frequency)
                return false;
            function = &c;
            break;
        case ChannelMode::Arbitrary:
            if (!c.arb.waveform || c.arb.linear)
                return false;   // Linear interpolation needs the stream
            if (arbitrary
                && (waveformLength(arbitrary->arb) != waveformLength(c.arb) || arbitrary->arb.updateRate != c.arb.updateRate
                    || arbitrary->arb.once != c.arb.once))
                return false;
            arbitrary = &c;
            break;
        case ChannelMode::Dc: dc = true; break;
        case ChannelMode::Off: break;
        }
    }
    if (function && arbitrary)
        return false;
    if (arbitrary && arbitrary->arb.once && dc)
        return false;   // a finite task would end while the DC level must go on
    return any;
}

StrategyKind chooseStrategy(const EngineSettings& settings, StrategyKind current, bool streamingSupported)
{
    bool live = false;
    bool allDc = true;
    for (const ChannelSettings& c : settings.channels) {
        if (!c.generating())
            continue;
        live = true;
        allDc = allDc && c.mode == ChannelMode::Dc;
    }
    if (!live)
        return StrategyKind::None;
    const bool regenerable = isRegenerable(settings);
    switch (settings.preference) {
    case EnginePreference::Stream:
        if (streamingSupported)
            return StrategyKind::Stream;
        break;
    case EnginePreference::Regenerate:
        if (allDc && settings.pair == OutputPair::AnalogOutputs)
            return StrategyKind::Static;
        return regenerable ? StrategyKind::Regenerate : StrategyKind::Stream;
    case EnginePreference::Auto:
        if (current == StrategyKind::Stream && streamingSupported)
            return StrategyKind::Stream;   // never downgrade a running stream
        if (allDc && settings.pair == OutputPair::AnalogOutputs)
            return StrategyKind::Static;
        if (streamingSupported)
            return StrategyKind::Stream;
        break;
    }
    // The backend cannot stream: the looped buffer when it can represent the outputs.
    if (allDc && settings.pair == OutputPair::AnalogOutputs)
        return StrategyKind::Static;
    return regenerable ? StrategyKind::Regenerate : StrategyKind::Stream;
}

// --- Strategies ---------------------------------------------------------------------------------

struct FinishEvent {
    int channel;
    uint64_t startId;
    bool held;      // Run Once: holds `value`
    double value;
};

class Strategy
{
public:
    Strategy(mydaq::Device& device, EngineHost& host) : m_device(device), m_host(host) {}
    virtual ~Strategy() = default;
    virtual StrategyKind kind() const = 0;
    // `settings` has finished channels already inactive, except in Stream (the synth holds them).
    virtual void apply(const EngineSettings& settings) = 0;
    virtual void step() = 0;
    virtual EngineStatus status() const = 0;
    virtual void shutdown() {}
    std::vector<FinishEvent> takeFinished() { return std::exchange(m_finished, {}); }

protected:
    mydaq::Device& m_device;
    EngineHost& m_host;
    std::vector<FinishEvent> m_finished;
};

// DC levels only: on-demand writes, no sample clock.
class StaticStrategy final : public Strategy
{
public:
    using Strategy::Strategy;
    StrategyKind kind() const override { return StrategyKind::Static; }

    void apply(const EngineSettings& settings) override
    {
        m_settings = settings;
        const double range = outputRange(settings.pair);
        for (int i = 0; i < 2; ++i) {
            const ChannelSettings& c = settings.channels[size_t(i)];
            const double level = c.generating() && c.mode == ChannelMode::Dc ? clip(c.dcLevel, range) : 0.0;
            if (m_written[size_t(i)] && *m_written[size_t(i)] == level)
                continue;
            if (!m_written[size_t(i)] && level == 0.0 && !c.generating())
                continue;   // never touched: nothing to reset
            m_device.aoWriteStatic(aoChannel(settings.pair, i), level);
            m_written[size_t(i)] = level;
        }
    }
    void step() override { m_host.sleepMs(20); }
    EngineStatus status() const override
    {
        EngineStatus s;
        s.strategy = kind();
        for (int i = 0; i < 2; ++i) {
            const ChannelSettings& c = m_settings.channels[size_t(i)];
            ChannelStatus& cs = s.channels[size_t(i)];
            cs.startId = c.startId;
            if (c.generating() && c.mode == ChannelMode::Dc) {
                cs.state = ChannelState::Running;
                cs.level = clip(c.dcLevel, outputRange(m_settings.pair));
            }
        }
        return s;
    }

private:
    EngineSettings m_settings;
    std::array<std::optional<double>, 2> m_written;
};

// One buffer looped by the device (ELVISmx behaviour).
class RegenerateStrategy final : public Strategy
{
public:
    using Strategy::Strategy;
    ~RegenerateStrategy() override { shutdown(); }
    StrategyKind kind() const override { return StrategyKind::Regenerate; }

    void apply(const EngineSettings& settings) override
    {
        Spec spec = build(settings);
        const bool restartOnce = spec.finite && spec.startIds != m_spec.startIds;
        if (m_task && spec.sameOutput(m_spec) && !restartOnce) {
            m_spec.startIds = spec.startIds;
            m_settings = settings;
            return;
        }
        const bool sameTask = m_task && spec.channels == m_spec.channels && spec.rate == m_spec.rate
                              && spec.finite == m_spec.finite;
        if (sameTask) {
            m_task->stop();
        } else {
            const std::vector<mydaq::AoChannel> previous = m_spec.channels;
            m_task.reset();
            // Channels leaving the task go to 0 V (AO pair; the audio outputs are AC coupled).
            if (settings.pair == OutputPair::AnalogOutputs || m_spec.pair == OutputPair::AnalogOutputs) {
                for (mydaq::AoChannel ch : previous) {
                    if (std::find(spec.channels.begin(), spec.channels.end(), ch) == spec.channels.end()
                        && !mydaq::isAudio(ch)) {
                        try {
                            m_device.aoWriteStatic(ch, 0.0);
                        } catch (const mydaq::DeviceError&) {
                        }
                    }
                }
            }
            mydaq::AoConfig config;
            for (mydaq::AoChannel ch : spec.channels)
                config.channels.push_back({ch, outputRange(settings.pair)});
            config.sampleRate = spec.rate;
            config.mode = spec.finite ? mydaq::SampleMode::Finite : mydaq::SampleMode::Continuous;
            config.regenerate = true;
            m_task = m_device.createAoTask(config);
        }
        m_task->write(spec.buffers, 2000);
        m_task->start();
        m_done = false;
        m_spec = std::move(spec);
        m_settings = settings;
        m_host.samplesWritten(int64_t(m_spec.buffers.size() * m_spec.buffers.front().size()));
    }

    void step() override
    {
        if (!m_task) {
            m_host.sleepMs(20);
            return;
        }
        if (m_spec.finite && !m_done) {
            if (m_task->waitUntilDone(20)) {
                m_done = true;
                for (size_t k = 0; k < m_spec.indices.size(); ++k) {
                    const int i = m_spec.indices[k];
                    const ChannelSettings& c = m_settings.channels[size_t(i)];
                    const bool hold = !c.arb.zeroAfterOnce;
                    m_finished.push_back({i, c.startId, hold, hold ? m_spec.buffers[k].back() : 0.0});
                }
            }
            return;
        }
        m_host.sleepMs(20);
    }

    EngineStatus status() const override
    {
        EngineStatus s;
        s.strategy = kind();
        if (!m_task)
            return s;
        s.taskRate = m_task->sampleRate();
        s.taskChannels = int(m_spec.channels.size());
        for (size_t k = 0; k < m_spec.indices.size(); ++k) {
            const int i = m_spec.indices[k];
            const ChannelSettings& c = m_settings.channels[size_t(i)];
            ChannelStatus& cs = s.channels[size_t(i)];
            cs.startId = c.startId;
            cs.state = m_done ? ChannelState::Done : ChannelState::Running;
            switch (c.mode) {
            case ChannelMode::Function:
                // The display shows the requested frequency (as ELVISmx); the buffer's own
                // frequency is reported separately for the caption.
                cs.frequency = c.function.frequency;
                cs.bufferLength = int(m_spec.length);
                cs.bufferFrequency = s.taskRate / double(m_spec.length);
                break;
            case ChannelMode::Arbitrary:
                cs.updateRate = s.taskRate;
                if (m_done)
                    cs.level = m_spec.buffers[k].back();
                break;
            case ChannelMode::Dc: cs.level = m_spec.buffers[k].front(); break;
            case ChannelMode::Off: break;
            }
        }
        return s;
    }

    void shutdown() override
    {
        if (m_task) {
            try {
                m_task->stop();
            } catch (const mydaq::DeviceError&) {
            }
        }
        m_task.reset();
    }

private:
    struct Spec {
        OutputPair pair = OutputPair::AnalogOutputs;
        std::vector<mydaq::AoChannel> channels;
        std::vector<int> indices;
        std::vector<std::vector<double>> buffers;
        std::vector<uint64_t> startIds;
        double rate = 0;
        size_t length = 0;
        bool finite = false;
        bool sameOutput(const Spec& o) const
        {
            return channels == o.channels && rate == o.rate && finite == o.finite && buffers == o.buffers;
        }
    };

    Spec build(const EngineSettings& settings) const
    {
        Spec spec;
        spec.pair = settings.pair;
        const double range = outputRange(settings.pair);
        const double maxRate = m_device.maxAoRate();
        const ChannelSettings* function = nullptr;
        const ChannelSettings* arbitrary = nullptr;
        for (const ChannelSettings& c : settings.channels) {
            if (!c.generating())
                continue;
            if (c.mode == ChannelMode::Function && !function)
                function = &c;
            if (c.mode == ChannelMode::Arbitrary && !arbitrary)
                arbitrary = &c;
        }
        if (function) {
            const FgenPlan plan = planFgenBuffer(function->function.frequency, maxRate,
                                                 [this](double r) { return m_device.coerceAoRate(r); });
            spec.rate = plan.rate;
            spec.length = plan.length;
        } else if (arbitrary) {
            spec.rate = m_device.coerceAoRate(std::clamp(arbitrary->arb.updateRate, kMinUpdateRate, maxRate));
            spec.length = waveformLength(arbitrary->arb);
            spec.finite = arbitrary->arb.once;
        } else {
            spec.rate = m_device.coerceAoRate(std::min(maxRate, 1000.0));
            spec.length = 2;
        }
        for (int i = 0; i < 2; ++i) {
            const ChannelSettings& c = settings.channels[size_t(i)];
            if (!c.generating())
                continue;
            spec.indices.push_back(i);
            spec.channels.push_back(aoChannel(settings.pair, i));
            spec.startIds.push_back(c.startId);
            switch (c.mode) {
            case ChannelMode::Function: spec.buffers.push_back(functionPeriod(c.function, spec.length, range)); break;
            case ChannelMode::Arbitrary: spec.buffers.push_back(scaledWaveform(c.arb, range)); break;
            case ChannelMode::Dc: spec.buffers.emplace_back(spec.length, clip(c.dcLevel, range)); break;
            case ChannelMode::Off: break;
            }
        }
        return spec;
    }

    std::unique_ptr<mydaq::AoTask> m_task;
    Spec m_spec;
    EngineSettings m_settings;
    bool m_done = false;
};

// Per-channel synthesizer of the streaming strategy.
class ChannelSynth
{
public:
    void configure(double rate, double range)
    {
        m_rate = rate;
        m_range = range;
    }

    void update(const ChannelSettings& s)
    {
        const bool restart = !m_started || s.startId != m_settings.startId || s.mode != m_settings.mode
                             || s.sweep != m_settings.sweep || s.active != m_settings.active;
        const bool newWaveform = s.arb.waveform != m_settings.arb.waveform;
        m_settings = s;
        m_live = s.generating();
        if (restart) {
            m_started = true;
            m_phase = 0;
            m_position = 0;
            m_done = false;
            m_finishReported = false;
            m_amp = s.function.amplitude / 2.0;
            m_offset = s.function.offset;
            m_dc = m_live && s.mode == ChannelMode::Dc ? s.dcLevel : m_dc;
            m_sweepActive = false;
            if (m_live && s.mode == ChannelMode::Function && s.sweep) {
                m_sweep = makeSweep(s.sweepSettings);
                m_sweepActive = true;
                m_sweepIndex = 0;
                m_stepSamples = std::max<int64_t>(1, std::llround(m_sweep.stepDuration * m_rate));
                m_stepLeft = m_stepSamples;
            }
        } else if (newWaveform) {
            m_position = 0;
            m_done = false;
            m_finishReported = false;
        }
        m_ampTarget = s.function.amplitude / 2.0;
        m_offsetTarget = s.function.offset;
        m_duty = std::clamp(s.function.dutyCycle, 0.0, 100.0) / 100.0;
        double turns = std::fmod(s.function.phase / 360.0, 1.0);
        if (turns < 0)
            turns += 1.0;
        m_phaseOffset = uint64_t(std::llround(turns * kTwoPow64 / 4.0)) * 4;
        m_increment = phaseIncrement(m_sweepActive ? currentSweepFrequency() : s.function.frequency, m_rate);
        m_dcTarget = s.dcLevel;
        m_waveform = s.mode == ChannelMode::Arbitrary ? s.arb.waveform : nullptr;
        m_positionIncrement = std::clamp(s.arb.updateRate, 1e-3, m_rate) / m_rate;
    }

    // Restart the phase (Align phases, underflow fallback).
    void resetPhase() { m_phase = 0; }
    void setRate(double rate)
    {
        const double old = m_rate;
        m_rate = rate;
        if (m_sweepActive && old > 0) {
            m_stepSamples = std::max<int64_t>(1, std::llround(m_sweep.stepDuration * m_rate));
            m_stepLeft = std::max<int64_t>(1, std::llround(double(m_stepLeft) * rate / old));
        }
        update(m_settings);
    }

    // Renders n samples; returns the offset of a finish (sweep end / Run Once end) or -1.
    int64_t render(double* out, size_t n)
    {
        int64_t finishedAt = -1;
        if (!m_live) {
            std::fill(out, out + n, 0.0);
            return -1;
        }
        switch (m_settings.mode) {
        case ChannelMode::Function: finishedAt = renderFunction(out, n); break;
        case ChannelMode::Arbitrary: finishedAt = renderArbitrary(out, n); break;
        case ChannelMode::Dc: {
            const double step = (m_dcTarget - m_dc) / double(n);
            for (size_t i = 0; i < n; ++i)
                out[i] = clip(m_dc + step * double(i + 1), m_range);
            m_dc = m_dcTarget;
            break;
        }
        case ChannelMode::Off: std::fill(out, out + n, 0.0); break;
        }
        return finishedAt;
    }

    ChannelStatus status() const
    {
        ChannelStatus s;
        s.startId = m_settings.startId;
        if (!m_live)
            return s;
        switch (m_settings.mode) {
        case ChannelMode::Function:
            if (m_done) {
                s.sweepFinished = true;
                s.sweepSteps = int(m_sweep.frequencies.size());
                s.sweepStep = s.sweepSteps;
                s.frequency = m_sweep.frequencies.empty() ? 0 : m_sweep.frequencies.back();
                s.state = ChannelState::Idle;
                break;
            }
            s.state = m_sweepActive ? ChannelState::Sweeping : ChannelState::Running;
            s.frequency = ddsFrequency(m_increment, m_rate);
            if (m_sweepActive) {
                s.sweepStep = int(m_sweepIndex) + 1;
                s.sweepSteps = int(m_sweep.frequencies.size());
            }
            break;
        case ChannelMode::Arbitrary:
            if (m_done && m_settings.arb.zeroAfterOnce) {
                s.finished = true;   // back at 0 V: Idle
                break;
            }
            s.state = m_done ? ChannelState::Done : ChannelState::Running;
            s.updateRate = m_positionIncrement * m_rate;
            s.level = m_held;
            break;
        case ChannelMode::Dc:
            s.state = ChannelState::Running;
            s.level = clip(m_dcTarget, m_range);
            break;
        case ChannelMode::Off: break;
        }
        return s;
    }

    bool finished() const { return m_done; }
    double held() const { return m_held; }
    uint64_t startId() const { return m_settings.startId; }
    bool holdsAtEnd() const { return m_settings.mode == ChannelMode::Arbitrary && !m_settings.arb.zeroAfterOnce; }
    bool live() const { return m_live; }

private:
    double currentSweepFrequency() const
    {
        return m_sweep.frequencies.empty() ? 0.0 : m_sweep.frequencies[std::min(m_sweepIndex, m_sweep.frequencies.size() - 1)];
    }

    int64_t renderFunction(double* out, size_t n)
    {
        if (m_done) {
            std::fill(out, out + n, 0.0);   // sweep over: safe level
            return -1;
        }
        int64_t finishedAt = -1;
        const Shape shape = m_settings.function.shape;
        const double ampStep = (m_ampTarget - m_amp) / double(n);
        const double offsetStep = (m_offsetTarget - m_offset) / double(n);
        for (size_t i = 0; i < n; ++i) {
            if (m_sweepActive) {
                if (m_stepLeft <= 0) {
                    ++m_sweepIndex;
                    if (m_sweepIndex >= m_sweep.frequencies.size()) {
                        m_done = true;
                        finishedAt = int64_t(i);
                        std::fill(out + i, out + n, 0.0);
                        break;
                    }
                    m_increment = phaseIncrement(currentSweepFrequency(), m_rate);
                    m_stepLeft = m_stepSamples;
                }
                --m_stepLeft;
            }
            const double a = m_amp + ampStep * double(i + 1);
            const double o = m_offset + offsetStep * double(i + 1);
            const double cycles = double(m_phase + m_phaseOffset) * kTwoPow64Inverse;
            out[i] = clip(o + a * shapeValue(shape, cycles, m_duty), m_range);
            m_phase += m_increment;
        }
        m_amp = m_ampTarget;
        m_offset = m_offsetTarget;
        return finishedAt;
    }

    int64_t renderArbitrary(double* out, size_t n)
    {
        if (!m_waveform || m_waveform->empty()) {
            std::fill(out, out + n, 0.0);
            return -1;
        }
        if (m_done) {
            std::fill(out, out + n, m_held);
            return -1;
        }
        const std::vector<double>& w = *m_waveform;
        const size_t size = w.size();
        const double length = double(std::max<size_t>(size, 2));
        const double gain = m_settings.arb.gain;
        const bool linear = m_settings.arb.linear;
        const bool once = m_settings.arb.once;
        auto sampleAt = [&](size_t k) { return size == 1 ? w[0] : w[k % size]; };
        int64_t finishedAt = -1;
        for (size_t i = 0; i < n; ++i) {
            if (m_position >= length) {
                if (once) {
                    m_done = true;
                    m_held = m_settings.arb.zeroAfterOnce ? 0.0 : clip(gain * sampleAt(size_t(length) - 1), m_range);
                    finishedAt = int64_t(i);
                    std::fill(out + i, out + n, m_held);
                    break;
                }
                m_position = std::fmod(m_position, length);
            }
            const size_t k = size_t(m_position);
            double v = sampleAt(k);
            if (linear) {
                const size_t next = k + 1 < size_t(length) ? k + 1 : (once ? k : 0);
                v += (sampleAt(next) - v) * (m_position - double(k));
            }
            out[i] = clip(gain * v, m_range);
            m_position += m_positionIncrement;
        }
        return finishedAt;
    }

    ChannelSettings m_settings;
    bool m_started = false;
    bool m_live = false;
    bool m_done = false;
    bool m_finishReported = false;
    double m_rate = 0;
    double m_range = 10;
    // Function (NCO)
    uint64_t m_phase = 0;
    uint64_t m_increment = 0;
    uint64_t m_phaseOffset = 0;
    double m_amp = 0, m_ampTarget = 0, m_offset = 0, m_offsetTarget = 0, m_duty = 0.5;
    // Sweep
    SweepPlan m_sweep;
    bool m_sweepActive = false;
    size_t m_sweepIndex = 0;
    int64_t m_stepSamples = 1;
    int64_t m_stepLeft = 0;
    // Arbitrary
    std::shared_ptr<const std::vector<double>> m_waveform;
    double m_position = 0;
    double m_positionIncrement = 0;
    double m_held = 0;
    // DC
    double m_dc = 0, m_dcTarget = 0;
};

// Continuous stream of both channels of the pair.
class StreamStrategy final : public Strategy
{
public:
    StreamStrategy(mydaq::Device& device, EngineHost& host, const Engine::StreamTiming& timing)
        : Strategy(device, host), m_timing(timing)
    {
    }
    ~StreamStrategy() override { shutdown(); }
    StrategyKind kind() const override { return StrategyKind::Stream; }

    void apply(const EngineSettings& settings) override
    {
        const bool align = m_task && settings.alignId != m_settings.alignId;
        m_settings = settings;
        if (!m_task) {
            m_rate = m_device.coerceAoRate(m_device.maxAoRate());
            createTask();
        }
        for (int i = 0; i < 2; ++i) {
            m_synth[size_t(i)].update(settings.channels[size_t(i)]);
            if (align)
                m_synth[size_t(i)].resetPhase();
        }
        if (!m_running)
            startStream();
    }

    void step() override
    {
        using Clock = std::chrono::steady_clock;
        const double elapsed = std::chrono::duration<double>(Clock::now() - m_start).count();
        const int64_t played = std::min<int64_t>(m_written, int64_t(elapsed * m_rate));
        publishPlayed(played);
        const int64_t target = int64_t(m_timing.leadSeconds * m_rate);
        int blocks = 0;
        while (m_written - played < target && blocks < 20 && !m_host.stopRequested()) {
            if (m_task->spaceAvailable() < m_block && blocks > 0)
                break;
            if (!writeBlock())
                return;   // task recreated after an underflow
            ++blocks;
        }
        if (blocks == 0)
            m_host.sleepMs(std::max(1, int(m_timing.blockSeconds * 1000)));
    }

    EngineStatus status() const override { return m_playedStatus; }

    void shutdown() override
    {
        if (m_task) {
            try {
                m_task->stop();
            } catch (const mydaq::DeviceError&) {
            }
        }
        m_task.reset();
        m_running = false;
    }

private:
    void createTask()
    {
        mydaq::AoConfig config;
        for (int i = 0; i < 2; ++i)
            config.channels.push_back({aoChannel(m_settings.pair, i), outputRange(m_settings.pair)});
        config.sampleRate = m_rate;
        config.mode = mydaq::SampleMode::Continuous;
        config.regenerate = false;
        m_task = m_device.createAoTask(config);
        m_rate = m_task->sampleRate();
        m_block = std::max<int64_t>(16, std::llround(m_timing.blockSeconds * m_rate));
        for (ChannelSynth& s : m_synth) {
            s.configure(m_rate, outputRange(m_settings.pair));
        }
        m_running = false;
    }

    void startStream()
    {
        m_written = 0;
        m_events.clear();
        m_finishEvents.clear();
        const int64_t prefill = std::max<int64_t>(m_block, int64_t(m_timing.leadSeconds * m_rate));
        std::vector<std::vector<double>> data(2, std::vector<double>(size_t(prefill)));
        renderInto(data, size_t(prefill));
        m_task->write(data, 2000);
        m_task->start();
        m_start = std::chrono::steady_clock::now();
        m_running = true;
        m_written = prefill;
        m_host.samplesWritten(prefill * 2);
    }

    void renderInto(std::vector<std::vector<double>>& data, size_t n)
    {
        for (int i = 0; i < 2; ++i) {
            ChannelSynth& synth = m_synth[size_t(i)];
            const int64_t finishedAt = synth.render(data[size_t(i)].data(), n);
            if (finishedAt >= 0)
                m_finishEvents.push_back({m_written + finishedAt,
                                          FinishEvent{i, synth.startId(), synth.holdsAtEnd(), synth.held()}});
        }
        EngineStatus s;
        s.strategy = StrategyKind::Stream;
        s.taskRate = m_rate;
        s.taskChannels = 2;
        for (int i = 0; i < 2; ++i)
            s.channels[size_t(i)] = m_synth[size_t(i)].status();
        if (m_events.empty() || !(m_events.back().second == s))
            m_events.emplace_back(m_written, s);
    }

    bool writeBlock()
    {
        std::vector<std::vector<double>> data(2, std::vector<double>(size_t(m_block)));
        renderInto(data, size_t(m_block));
        try {
            m_task->write(data, 1000);
        } catch (const mydaq::DeviceError& error) {
            if (error.code() != mydaq::DeviceError::Underflow)
                throw;
            recoverFromUnderflow();
            return false;
        }
        m_written += m_block;
        m_host.samplesWritten(m_block * 2);
        return true;
    }

    void recoverFromUnderflow()
    {
        const double fallback = m_device.coerceAoRate(m_timing.fallbackRate);
        if (m_rate <= fallback * (1 + 1e-9))
            throw mydaq::DeviceError(mydaq::DeviceError::Underflow,
                                     tr("The output ran out of data because the computer could not keep up. "
                                        "The generator was stopped."));
        shutdown();
        m_rate = fallback;
        createTask();
        for (ChannelSynth& s : m_synth) {
            s.setRate(m_rate);
            s.resetPhase();
        }
        m_host.warn(tr("Generator output was interrupted (computer too slow); continuing at %1 kS/s.")
                        .arg(m_rate / 1000.0, 0, 'f', 0));
        startStream();
    }

    void publishPlayed(int64_t played)
    {
        while (!m_events.empty() && m_events.front().first <= played) {
            m_playedStatus = m_events.front().second;
            m_events.pop_front();
        }
        while (!m_finishEvents.empty() && m_finishEvents.front().first <= played) {
            m_finished.push_back(m_finishEvents.front().second);
            m_finishEvents.pop_front();
        }
    }

    Engine::StreamTiming m_timing;
    EngineSettings m_settings;
    std::unique_ptr<mydaq::AoTask> m_task;
    std::array<ChannelSynth, 2> m_synth;
    double m_rate = 0;
    int64_t m_block = 2000;
    int64_t m_written = 0;
    bool m_running = false;
    std::chrono::steady_clock::time_point m_start;
    std::deque<std::pair<int64_t, EngineStatus>> m_events;
    std::deque<std::pair<int64_t, FinishEvent>> m_finishEvents;
    EngineStatus m_playedStatus;
};

mydaq::DeviceError userFacingError(const mydaq::DeviceError& error)
{
    if (error.code() == mydaq::DeviceError::Underflow)
        return mydaq::DeviceError(mydaq::DeviceError::Generic, error.message());
    return error;
}

// --- Engine -------------------------------------------------------------------------------------

Engine::Engine(mydaq::Device& device, EngineHost& host) : m_device(device), m_host(host) {}

Engine::~Engine() = default;

std::unique_ptr<Strategy> Engine::makeStrategy(StrategyKind kind)
{
    switch (kind) {
    case StrategyKind::Static: return std::make_unique<StaticStrategy>(m_device, m_host);
    case StrategyKind::Regenerate: return std::make_unique<RegenerateStrategy>(m_device, m_host);
    case StrategyKind::Stream: return std::make_unique<StreamStrategy>(m_device, m_host, m_timing);
    case StrategyKind::None: break;
    }
    return nullptr;
}

EngineResult Engine::run(const EngineSettings& initial)
{
    EngineSettings settings = initial;
    EngineResult result;
    std::array<std::optional<uint64_t>, 2> finished;
    std::unique_ptr<Strategy> strategy;
    bool streaming = true;
    bool natural = false;
    bool dirty = true;   // settings, finished channels or strategy changed since the last apply()
    std::optional<EngineStatus> published;

    auto masked = [&] {
        EngineSettings m = settings;
        for (int i = 0; i < 2; ++i) {
            if (finished[size_t(i)] && *finished[size_t(i)] == m.channels[size_t(i)].startId)
                m.channels[size_t(i)].active = false;
        }
        return m;
    };

    try {
        while (!m_host.stopRequested()) {
            if (auto next = m_host.takeSettings()) {
                settings = *next;
                dirty = true;
            }
            for (int i = 0; i < 2; ++i) {
                if (finished[size_t(i)] && *finished[size_t(i)] != settings.channels[size_t(i)].startId) {
                    finished[size_t(i)].reset();
                    result.done[size_t(i)] = false;
                    dirty = true;
                }
            }
            const EngineSettings live = masked();
            const StrategyKind current = strategy ? strategy->kind() : StrategyKind::None;
            const StrategyKind wanted = chooseStrategy(live, current, streaming);
            if (wanted == StrategyKind::None) {
                natural = true;
                break;
            }
            if (wanted == StrategyKind::Stream && !streaming)
                throw mydaq::DeviceError(mydaq::DeviceError::NotSupported,
                                         tr("this combination of outputs needs streaming, which the device "
                                            "backend does not support yet"));
            if (wanted != current) {
                if (strategy)
                    strategy->shutdown();
                strategy = makeStrategy(wanted);
                dirty = true;
            }
            try {
                if (dirty)
                    strategy->apply(wanted == StrategyKind::Stream ? settings : live);
                dirty = false;
                strategy->step();
            } catch (const mydaq::DeviceError& error) {
                if (error.code() != mydaq::DeviceError::NotSupported || wanted != StrategyKind::Stream)
                    throw;
                strategy.reset();
                streaming = false;
                if (!isRegenerable(live))
                    throw;
                m_host.warn(tr("The device cannot stream yet: the generator uses a regenerated buffer."));
                continue;
            }
            for (const FinishEvent& event : strategy->takeFinished()) {
                if (event.startId != settings.channels[size_t(event.channel)].startId)
                    continue;
                finished[size_t(event.channel)] = event.startId;
                dirty = true;
                if (event.held) {
                    result.done[size_t(event.channel)] = true;
                    result.held[size_t(event.channel)] = event.value;
                    result.startId[size_t(event.channel)] = event.startId;
                }
            }
            EngineStatus status = strategy->status();
            for (int i = 0; i < 2; ++i) {
                // A finished channel keeps reporting its end state (Done / sweep finished / back
                // at 0 V after Run Once).
                ChannelStatus& cs = status.channels[size_t(i)];
                if (!finished[size_t(i)])
                    continue;
                if (result.done[size_t(i)] && cs.state != ChannelState::Done) {
                    cs.state = ChannelState::Done;
                    cs.level = result.held[size_t(i)];
                    cs.startId = result.startId[size_t(i)];
                } else if (!result.done[size_t(i)] && !cs.sweepFinished && !cs.finished
                           && settings.channels[size_t(i)].mode == ChannelMode::Arbitrary) {
                    cs = ChannelStatus{};
                    cs.startId = *finished[size_t(i)];
                    cs.finished = true;
                }
            }
            if (!published || !(*published == status)) {
                m_host.publishStatus(status);
                published = status;
            }
        }
    } catch (...) {
        if (strategy)
            strategy->shutdown();
        strategy.reset();
        staticSafeState(settings, result, false);
        throw;
    }
    if (strategy)
        strategy->shutdown();
    strategy.reset();
    staticSafeState(settings, result, natural);
    if (!natural)
        result.done = {};
    EngineStatus end;
    for (int i = 0; i < 2; ++i) {
        end.channels[size_t(i)].startId = settings.channels[size_t(i)].startId;
        if (result.done[size_t(i)]) {
            end.channels[size_t(i)].state = ChannelState::Done;
            end.channels[size_t(i)].level = result.held[size_t(i)];
        }
    }
    m_host.publishStatus(end);
    return result;
}

void Engine::staticSafeState(const EngineSettings& settings, const EngineResult& result, bool natural)
{
    if (settings.pair == OutputPair::AudioOutputs)
        return;   // AC coupled: nothing to set
    for (int i = 0; i < 2; ++i) {
        const double level = natural && result.done[size_t(i)] ? result.held[size_t(i)] : 0.0;
        try {
            m_device.aoWriteStatic(aoChannel(settings.pair, i), level);
        } catch (const mydaq::DeviceError&) {
            // e.g. ResourceBusy: another instrument already owns AO; it sets the outputs.
        }
    }
}

}  // namespace mybench::instruments::gen
