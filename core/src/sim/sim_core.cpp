#include "sim_core.h"

#include "sim_counter.h"
#include "sim_tasks.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mydaq::sim {

namespace {

constexpr double kMinDivisor = kTimebase / kMaxRate;
constexpr double kMaxDivisor = 4294967295.0;   // 32-bit divisor
constexpr double kPruneSettleTaus = 25.0;      // history kept for the loopback filters to settle
constexpr double kPruneMargin = 0.1;           // s
// Hard limit on AO history (bounds memory while a stale AI task holds old unread samples; such
// samples then see the oldest retained output instead of the exact loopback).
constexpr double kMaxHistory = 20.0;           // s
constexpr double kDigitalHistory = 120.0;      // s of digital signal history kept for the counter

bool nearlyEqual(double a, double b) { return std::abs(a - b) < 1e-9; }

}  // namespace

double coerceRate(double requested)
{
    if (!(requested > 0.0))
        return kTimebase / kMaxDivisor;
    const double below = std::clamp(std::floor(kTimebase / requested), kMinDivisor, kMaxDivisor);
    const double above = std::clamp(below + 1.0, kMinDivisor, kMaxDivisor);
    const double fast = kTimebase / below;
    const double slow = kTimebase / above;
    return std::abs(fast - requested) <= std::abs(slow - requested) ? fast : slow;
}

double quantizeAo(double volts, double range)
{
    const double lsb = 2.0 * kAiHeadroom * range / kAiCodes;
    return std::clamp(std::round(volts / lsb), -kAiCodes / 2.0, kAiCodes / 2.0 - 1.0) * lsb;
}

Core::Clock::time_point deadlineFor(int timeoutMs)
{
    if (timeoutMs < 0)
        return Core::Clock::now() + std::chrono::hours(24);
    return Core::Clock::now() + std::chrono::milliseconds(timeoutMs);
}

QString lineNames(uint8_t mask)
{
    QStringList names;
    for (int line = 0; line < kDioLines; ++line)
        if (mask & lineMask(line))
            names << QStringLiteral("DIO%1").arg(line);
    return names.join(QStringLiteral(", "));
}

int index(AiChannel channel) { return static_cast<int>(channel); }
int index(AoChannel channel) { return static_cast<int>(channel); }
int index(DmmFunction function) { return static_cast<int>(function); }

std::vector<DmmRange> dmmRangesOf(DmmFunction function)
{
    const QString ohm = QStringLiteral("Ω");
    switch (function) {
    case DmmFunction::DcVoltage:
        return {{60.0, QStringLiteral("60 V")}, {20.0, QStringLiteral("20 V")}, {2.0, QStringLiteral("2 V")},
                {0.2, QStringLiteral("200 mV")}};
    case DmmFunction::AcVoltage:
        return {{20.0, QStringLiteral("20 V")}, {2.0, QStringLiteral("2 V")}, {0.2, QStringLiteral("200 mV")}};
    case DmmFunction::DcCurrent:
    case DmmFunction::AcCurrent:
        return {{1.0, QStringLiteral("1 A")}, {0.2, QStringLiteral("200 mA")}, {0.02, QStringLiteral("20 mA")}};
    case DmmFunction::Resistance:
        return {{20e6, QStringLiteral("20 M") + ohm}, {2e6, QStringLiteral("2 M") + ohm},
                {200e3, QStringLiteral("200 k") + ohm}, {20e3, QStringLiteral("20 k") + ohm},
                {2e3, QStringLiteral("2 k") + ohm},     {200.0, QStringLiteral("200 ") + ohm}};
    case DmmFunction::Diode: return {{2.0, QStringLiteral("2 V")}};
    case DmmFunction::Continuity: return {{200.0, QStringLiteral("200 ") + ohm}};
    }
    return {};
}

void validateAiRange(AiChannel channel, double range)
{
    if (isAudio(channel)) {
        if (!nearlyEqual(range, 2.0))
            throw DeviceError(DeviceError::InvalidConfig,
                              QStringLiteral("%1 has a fixed ±2 V range").arg(toString(channel)));
    } else if (!nearlyEqual(range, 10.0) && !nearlyEqual(range, 2.0)) {
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("%1 range must be ±10 V or ±2 V (got %2 V)")
                              .arg(toString(channel))
                              .arg(range));
    }
}

void validateAoValue(AoChannel channel, double range, double volts)
{
    if (!std::isfinite(volts) || std::abs(volts) > range + 1e-9)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("%1 V is outside the ±%2 V range of %3")
                                                          .arg(volts)
                                                          .arg(range)
                                                          .arg(toString(channel)));
}

// ---------------------------------------------------------------------------------------------

Core::Core(DeviceInfo deviceInfo) : info(std::move(deviceInfo))
{
    using dsp::Waveform;
    testSignals[index(AiChannel::AI0)] = {Waveform::Sine, 1000.0, 1.0, 0.0, 0.5, {0.0, 0.01}};
    testSignals[index(AiChannel::AI1)] = {Waveform::Square, 200.0, 0.5, 0.0, 0.2, {}};
    testSignals[index(AiChannel::AudioInLeft)] = {Waveform::Sine, 440.0, 0.5, 0.0, 0.5, {}};
    testSignals[index(AiChannel::AudioInRight)] = {Waveform::Sine, 1000.0, 0.5, 0.0, 0.5, {}};

    dmmValues[index(DmmFunction::DcVoltage)] = 1.2345;
    dmmValues[index(DmmFunction::AcVoltage)] = 0.7071;
    dmmValues[index(DmmFunction::DcCurrent)] = 12.34e-3;
    dmmValues[index(DmmFunction::AcCurrent)] = 5.000e-3;
    dmmValues[index(DmmFunction::Resistance)] = 4.7e3;
    dmmValues[index(DmmFunction::Diode)] = 0.650;
    dmmValues[index(DmmFunction::Continuity)] = 0.8;

    for (int line = 0; line < kDioLines; ++line)
        applyDigitalSource(line, 0.0);
}

double Core::now() const { return std::chrono::duration<double>(Clock::now() - epoch).count(); }

Core::Clock::time_point Core::timePoint(double t) const
{
    return epoch + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t));
}

void Core::requireConnected() const
{
    if (!connected)
        throw DeviceError(DeviceError::NotConnected, QStringLiteral("%1 is not connected").arg(info.name));
}

double Core::testSignalValue(AiChannel channel, double t)
{
    const SimTestSignal& s = testSignals[index(channel)];
    double v = 0.0;
    if (s.waveform == dsp::Waveform::Noise) {
        v = s.amplitude * std::uniform_real_distribution<double>(-1.0, 1.0)(rng);
    } else {
        const double cycles = s.frequency * t;
        v = dsp::waveformShape(s.waveform, cycles, s.dutyCycle);
        if (s.waveform == dsp::Waveform::Sine && !s.harmonics.empty()) {
            const double phase = 2.0 * std::numbers::pi * (cycles - std::floor(cycles));
            for (size_t k = 0; k < s.harmonics.size(); ++k)
                v += s.harmonics[k] * std::sin(double(k + 2) * phase);
        }
        v *= s.amplitude;
    }
    return isAudio(channel) ? v : v + s.offset;
}

double Core::sampleAi(AiChannel channel, double range, double t)
{
    double v = 0.0;
    switch (channel) {
    case AiChannel::AI0: {
        const AoTimeline::Level level = outputs[index(AoChannel::AO0)].levelAt(t);
        v = level.driven ? level.value : testSignalValue(channel, t);
        break;
    }
    case AiChannel::AI1: {
        const AoTimeline& source = outputs[index(AoChannel::AO0)];
        source.advance(rcFilter, t);
        v = source.levelAt(t).driven ? rcFilter.value() : testSignalValue(channel, t);
        break;
    }
    case AiChannel::AudioInLeft:
    case AiChannel::AudioInRight: {
        // High-pass = input - low-pass(input), exact for the zero-order-hold output.
        const bool left = channel == AiChannel::AudioInLeft;
        const AoTimeline& source = outputs[index(left ? AoChannel::AudioOutLeft : AoChannel::AudioOutRight)];
        ZohLowPass& coupling = audioCoupling[left ? 0 : 1];
        source.advance(coupling, t);
        const AoTimeline::Level level = source.levelAt(t);
        v = level.driven ? level.value - coupling.value() : testSignalValue(channel, t);
        break;
    }
    }
    v += noiseRms * gaussian(rng);
    const double lsb = 2.0 * kAiHeadroom * range / kAiCodes;
    const double code = std::clamp(std::round(v / lsb), -kAiCodes / 2.0, kAiCodes / 2.0 - 1.0);
    return code * lsb;
}

void Core::prune()
{
    const double t = now();
    double oldest = t;
    if (aiTask)
        oldest = std::min(oldest, aiTask->oldestUnreadTime(t));
    const double tau = std::max({rcFilter.tau(), audioCoupling[0].tau(), audioCoupling[1].tau()});
    const double keepFrom = std::max(oldest - kPruneSettleTaus * tau - kPruneMargin, t - kMaxHistory);
    for (AoTimeline& output : outputs)
        output.prune(keepFrom);

    double digitalFrom = t - kDigitalHistory;
    if (ciTask)
        digitalFrom = std::min(digitalFrom, ciTask->oldestNeededTime(t));
    for (DigitalTimeline& line : dioExternal)
        line.prune(digitalFrom);
    pulseOutput.prune(t - kPruneMargin);
    freqOutput.prune(t - kPruneMargin);
}

void Core::stopTasksLocked()
{
    const double t = now();
    if (aiTask)
        aiTask->stopLocked(t);
    if (aoTask)
        aoTask->stopLocked(t);
    if (ciTask)
        ciTask->stopLocked(t);
    if (coTask)
        coTask->stopLocked(t);
    if (freqOutFrequency > 0.0)
        setFrequencyOutput(t, 0.0);
    changed.notify_all();
}

// ---------------------------------------------------------------------------------------------
// Digital lines

void Core::requireLinesFree(uint8_t lines) const
{
    if (const auto asOutputs = uint8_t(lines & dioDirection))
        throw DeviceError(DeviceError::ResourceBusy,
                          QStringLiteral("%1 is configured as a digital output").arg(lineNames(asOutputs)));
    if (const auto busy = uint8_t(lines & reservedLines()))
        throw DeviceError(DeviceError::ResourceBusy,
                          QStringLiteral("%1 is in use by the counter or FREQOUT").arg(lineNames(busy)));
}

bool Core::dioLevel(int line, double t) const
{
    if (line == kPulseOutputLine && coTask)
        return pulseOutput.levelAt(t);
    if (line == kFreqOutLine && freqOutFrequency > 0.0)
        return freqOutput.levelAt(t);
    if (dioDirection & lineMask(line))
        return dioLatch & lineMask(line);
    return dioExternal[size_t(line)].levelAt(t);
}

void Core::applyDigitalSource(int line, double t)
{
    if (encoder.enabled && line <= 2)
        return;   // the encoder drives DIO0..2
    const SimDigitalSource& source = dioSources[size_t(line)];
    DigitalTimeline& signal = dioExternal[size_t(line)];
    switch (source.kind) {
    case SimDigitalSource::Kind::Floating: {   // bit `line` of a counter incrementing at 2 Hz
        const double rate = 1.0 / double(1 << line);
        signal.setSquare(t, t * rate - 0.5, rate, 0.5);
        break;
    }
    case SimDigitalSource::Kind::Clock:
        signal.setSquare(t, source.frequency * (t - source.delay), source.frequency, source.dutyCycle);
        break;
    case SimDigitalSource::Kind::Level: signal.setConstant(t, source.high); break;
    case SimDigitalSource::Kind::FrequencyOutput:
        if (freqOutFrequency > 0.0)
            signal.setSquare(t, freqOutput.phaseAt(t), freqOutFrequency, 0.5);
        else
            signal.setConstant(t, false);
        break;
    }
}

double Core::encoderPosition(double t) const
{
    return encoderPhase + (encoder.enabled ? encoder.speed : 0.0) * (t - encoderTime);
}

void Core::applyEncoder(double t)
{
    if (!encoder.enabled) {
        for (int line = 0; line <= 2; ++line)
            applyDigitalSource(line, t);
        return;
    }
    // A high for frac(c) in [0, 0.5), B for [0.25, 0.75), Z for c mod PPR in [0.25, 0.5).
    const double c = encoderPosition(t);
    const double v = encoder.speed;
    const double ppr = encoder.pulsesPerRevolution;
    dioExternal[0].setSquare(t, c, v, 0.5);
    dioExternal[2].setSquare(t, c - 0.25, v, 0.5);
    dioExternal[1].setSquare(t, (c - 0.25) / ppr, v / ppr, 0.25 / ppr);
}

void Core::setFrequencyOutput(double t, double frequency)
{
    freqOutFrequency = std::max(0.0, frequency);
    if (freqOutFrequency > 0.0)
        freqOutput.setSquare(t, 0.0, freqOutFrequency, 0.5);
    else
        freqOutput.setConstant(t, false);
    for (int line = 0; line < kDioLines; ++line)
        if (dioSources[size_t(line)].kind == SimDigitalSource::Kind::FrequencyOutput)
            applyDigitalSource(line, t);
}

void Core::resetLocked()
{
    stopTasksLocked();
    const double t = now();
    for (AoTimeline& output : outputs)
        output.setLevel(t, 0.0);
    dioDirection = 0;
    dioLatch = 0;
    prune();
}

}  // namespace mydaq::sim
