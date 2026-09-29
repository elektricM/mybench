#include "mydaq/sim_device.h"

#include "sim_core.h"
#include "sim_counter.h"
#include "sim_tasks.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace mydaq {

using sim::index;

namespace {

double aoRangeOf(AoChannel channel) { return isAudio(channel) ? 2.0 : 10.0; }

DeviceError invalid(const QString& message) { return DeviceError(DeviceError::InvalidConfig, message); }

void validateRate(double rate)
{
    if (!(rate > 0.0) || rate > sim::kMaxRate * (1.0 + 1e-9))
        throw invalid(QStringLiteral("Sample rate %1 S/s is outside 0..200 kS/s per channel").arg(rate));
}

void validateAiConfig(const AiConfig& config)
{
    const auto& channels = config.channels;
    if (channels.empty() || channels.size() > 2)
        throw invalid(QStringLiteral("An AI task needs 1 or 2 channels"));
    if (channels.size() == 2) {
        if (channels[0].channel == channels[1].channel)
            throw invalid(QStringLiteral("%1 is listed twice").arg(toString(channels[0].channel)));
        if (isAudio(channels[0].channel) != isAudio(channels[1].channel))
            throw invalid(QStringLiteral("AI and AudioInput channels share the converters and cannot be mixed"));
    }
    for (const AiChannelConfig& ch : channels)
        sim::validateAiRange(ch.channel, ch.range);
    validateRate(config.sampleRate);
    if (config.samplesPerChannel <= 0)
        throw invalid(QStringLiteral("samplesPerChannel must be positive"));
}

void validateAoConfig(const AoConfig& config)
{
    const auto& channels = config.channels;
    if (channels.empty() || channels.size() > 2)
        throw invalid(QStringLiteral("An AO task needs 1 or 2 channels"));
    if (channels.size() == 2) {
        if (channels[0].channel == channels[1].channel)
            throw invalid(QStringLiteral("%1 is listed twice").arg(toString(channels[0].channel)));
        if (isAudio(channels[0].channel) != isAudio(channels[1].channel))
            throw invalid(QStringLiteral("AO and AudioOutput channels share the converters and cannot be mixed"));
    }
    for (const AoChannelConfig& ch : channels) {
        const bool valid = std::abs(ch.range - 2.0) < 1e-9 || (!isAudio(ch.channel) && std::abs(ch.range - 10.0) < 1e-9);
        if (!valid)
            throw invalid(isAudio(ch.channel)
                              ? QStringLiteral("%1 has a fixed ±2 V range").arg(toString(ch.channel))
                              : QStringLiteral("%1 range must be ±10 V or ±2 V").arg(toString(ch.channel)));
    }
    validateRate(config.sampleRate);
}

bool isAcFunction(DmmFunction f) { return f == DmmFunction::AcVoltage || f == DmmFunction::AcCurrent; }

bool needsClosedLeads(DmmFunction f)
{
    return f == DmmFunction::Resistance || f == DmmFunction::Diode || f == DmmFunction::Continuity;
}

}  // namespace

SimDevice::SimDevice(const QString& id, const QString& name)
    : d(std::make_shared<sim::Core>(DeviceInfo{id, name, QStringLiteral("NI myDAQ"), QStringLiteral("SIMULATED"), true}))
{
}

SimDevice::~SimDevice() = default;

DeviceInfo SimDevice::info() const
{
    std::lock_guard lock(d->mutex);
    return d->info;
}

bool SimDevice::isConnected() const
{
    std::lock_guard lock(d->mutex);
    return d->connected;
}

double SimDevice::maxAiRate() const { return sim::kMaxRate; }
double SimDevice::maxAoRate() const { return sim::kMaxRate; }
double SimDevice::coerceAiRate(double requested) const { return sim::coerceRate(requested); }
double SimDevice::coerceAoRate(double requested) const { return sim::coerceRate(requested); }

std::unique_ptr<AiTask> SimDevice::createAiTask(const AiConfig& config)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    validateAiConfig(config);
    if (d->aiTask)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("Analog input is in use by another task"));
    return std::make_unique<sim::SimAiTask>(d, config);
}

std::unique_ptr<AoTask> SimDevice::createAoTask(const AoConfig& config)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    validateAoConfig(config);
    if (d->aoTask)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("Analog output is in use by another task"));
    return std::make_unique<sim::SimAoTask>(d, config);
}

double SimDevice::aiReadStatic(AiChannel channel, double range)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    sim::validateAiRange(channel, range);
    if (d->aiTask)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("Analog input is in use by a task"));
    const double v = d->sampleAi(channel, range, d->now());
    d->prune();
    return v;
}

void SimDevice::aoWriteStatic(AoChannel channel, double volts)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    if (d->aoTask)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("Analog output is in use by a timed task"));
    sim::validateAoValue(channel, aoRangeOf(channel), volts);
    d->outputs[index(channel)].setLevel(d->now(), sim::quantizeAo(volts, aoRangeOf(channel)));
    d->prune();
}

std::vector<DmmRange> SimDevice::dmmRanges(DmmFunction function) const { return sim::dmmRangesOf(function); }

DmmReading SimDevice::dmmRead(DmmFunction function, double rangeMax)
{
    const std::vector<DmmRange> ranges = sim::dmmRangesOf(function);
    if (std::none_of(ranges.begin(), ranges.end(),
                     [&](const DmmRange& r) { return std::abs(r.max - rangeMax) <= 1e-9 * r.max; }))
        throw invalid(QStringLiteral("%1 is not a %2 range").arg(rangeMax).arg(toString(function)));

    std::lock_guard dmmLock(d->dmmMutex);
    int readTimeMs = 0;
    {
        std::lock_guard lock(d->mutex);
        d->requireConnected();
        readTimeMs = isAcFunction(function) ? d->dmmAcMs : d->dmmDcMs;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(readTimeMs));

    std::lock_guard lock(d->mutex);
    d->requireConnected();
    if (d->dmmOpenCircuit && needsClosedLeads(function))
        return {10.0 * rangeMax, true};
    // 3½-digit display: resolution is 1/2000 of a "2" range (60 V range: 10 mV).
    const double resolution = std::pow(10.0, std::floor(std::log10(rangeMax) + 1e-9) - 3.0);
    double value = d->dmmValues[index(function)] + 0.3 * resolution * d->gaussian(d->rng);
    if (isAcFunction(function))
        value = std::abs(value);
    return {std::round(value / resolution) * resolution, std::abs(value) > rangeMax};
}

void SimDevice::dioSetDirection(uint8_t mask, bool output)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    if (const auto busy = uint8_t(mask & d->reservedLines()))
        throw DeviceError(DeviceError::ResourceBusy,
                          QStringLiteral("%1 is in use by the counter or FREQOUT").arg(sim::lineNames(busy)));
    d->dioDirection = output ? uint8_t(d->dioDirection | mask) : uint8_t(d->dioDirection & ~mask);
}

uint8_t SimDevice::dioDirection() const
{
    std::lock_guard lock(d->mutex);
    return d->dioDirection;
}

void SimDevice::dioWrite(uint8_t value, uint8_t mask)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    const auto lines = uint8_t(mask & d->dioDirection);
    d->dioLatch = uint8_t((d->dioLatch & ~lines) | (value & lines));
}

uint8_t SimDevice::dioRead()
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    const double t = d->now();
    uint8_t levels = 0;
    for (int line = 0; line < kDioLines; ++line)
        if (d->dioLevel(line, t))
            levels |= sim::lineMask(line);
    return levels;
}

std::unique_ptr<CounterInputTask> SimDevice::createCounterInputTask(const CounterInputConfig& config)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    sim::validateCounterInput(config);
    if (d->ciTask || d->coTask)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("ctr0 is in use by another counter task"));
    d->requireLinesFree(sim::counterInputLines(config));
    return std::make_unique<sim::SimCounterInputTask>(d, config);
}

std::unique_ptr<CounterOutputTask> SimDevice::createCounterOutputTask(const PulseOutputConfig& config)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    sim::validatePulseOutput(config);
    if (d->ciTask || d->coTask)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("ctr0 is in use by another counter task"));
    d->requireLinesFree(sim::lineMask(sim::kPulseOutputLine));
    return std::make_unique<sim::SimCounterOutputTask>(d, config);
}

double SimDevice::coerceCounterFrequency(double requested) const { return sim::coercePulseFrequency(requested); }

double SimDevice::coerceFrequencyOutput(double requested) const { return sim::coerceFreqOut(requested); }

double SimDevice::frequencyOutputStart(double frequency)
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    if (!(frequency > 0.0))
        throw invalid(QStringLiteral("The FREQOUT frequency must be positive"));
    if (!(d->freqOutFrequency > 0.0))
        d->requireLinesFree(sim::lineMask(sim::kFreqOutLine));
    const double actual = sim::coerceFreqOut(frequency);
    d->setFrequencyOutput(d->now(), actual);
    return actual;
}

void SimDevice::frequencyOutputStop()
{
    std::lock_guard lock(d->mutex);
    if (d->freqOutFrequency > 0.0)
        d->setFrequencyOutput(d->now(), 0.0);
}

bool SimDevice::frequencyOutputRunning() const
{
    std::lock_guard lock(d->mutex);
    return d->connected && d->freqOutFrequency > 0.0;
}

void SimDevice::reset()
{
    std::lock_guard lock(d->mutex);
    d->requireConnected();
    d->resetLocked();
}

// ---------------------------------------------------------------------------------------------

void SimDevice::setConnected(bool connected)
{
    std::lock_guard lock(d->mutex);
    if (connected == d->connected)
        return;
    d->connected = connected;
    if (connected)
        d->resetLocked();   // power-up state
    else
        d->stopTasksLocked();
}

void SimDevice::setTestSignal(AiChannel channel, const SimTestSignal& signal)
{
    std::lock_guard lock(d->mutex);
    d->testSignals[index(channel)] = signal;
}

SimTestSignal SimDevice::testSignal(AiChannel channel) const
{
    std::lock_guard lock(d->mutex);
    return d->testSignals[index(channel)];
}

void SimDevice::setNoiseRms(double volts)
{
    std::lock_guard lock(d->mutex);
    d->noiseRms = std::max(0.0, volts);
}

QStringList SimDevice::unsupportedFeatures() const
{
    std::lock_guard lock(d->mutex);
    return d->unsupportedFeatures;
}

void SimDevice::setUnsupportedFeatures(const QStringList& features)
{
    std::lock_guard lock(d->mutex);
    d->unsupportedFeatures = features;
}

double SimDevice::noiseRms() const
{
    std::lock_guard lock(d->mutex);
    return d->noiseRms;
}

void SimDevice::setRcCutoff(double hertz)
{
    if (!(hertz > 0.0))
        throw invalid(QStringLiteral("The RC cutoff must be positive"));
    std::lock_guard lock(d->mutex);
    d->rcFilter.setCutoff(hertz);
}

double SimDevice::rcCutoff() const
{
    std::lock_guard lock(d->mutex);
    return d->rcFilter.cutoff();
}

void SimDevice::setManualClock(bool manual)
{
    std::lock_guard lock(d->mutex);
    if (manual == d->manualClock.load())
        return;
    if (manual)
        d->manualTime = d->now();
    d->manualClock = manual;
    d->changed.notify_all();
}

void SimDevice::advanceClock(double seconds)
{
    std::lock_guard lock(d->mutex);
    if (!d->manualClock || !(seconds >= 0.0))
        throw invalid(QStringLiteral("advanceClock() needs the manual clock and a non-negative step"));
    d->manualTime = d->manualTime.load() + seconds;
    d->changed.notify_all();
}

void SimDevice::setChannelSkew(double seconds)
{
    if (!(seconds >= 0.0))
        throw invalid(QStringLiteral("The channel skew cannot be negative"));
    std::lock_guard lock(d->mutex);
    d->channelSkew = seconds;
}

double SimDevice::channelSkew() const
{
    std::lock_guard lock(d->mutex);
    return d->channelSkew;
}

void SimDevice::setDigitalSource(int line, const SimDigitalSource& source)
{
    if (line < 0 || line >= kDioLines)
        throw invalid(QStringLiteral("There is no DIO%1").arg(line));
    if (source.kind == SimDigitalSource::Kind::Clock
        && (!(source.frequency > 0.0) || !(source.dutyCycle > 0.0) || !(source.dutyCycle < 1.0)))
        throw invalid(QStringLiteral("A clock source needs a positive frequency and 0 < duty < 1"));
    std::lock_guard lock(d->mutex);
    d->dioSources[size_t(line)] = source;
    d->applyDigitalSource(line, d->now());
}

SimDigitalSource SimDevice::digitalSource(int line) const
{
    if (line < 0 || line >= kDioLines)
        throw invalid(QStringLiteral("There is no DIO%1").arg(line));
    std::lock_guard lock(d->mutex);
    return d->dioSources[size_t(line)];
}

void SimDevice::setEncoder(const SimEncoder& encoder)
{
    if (encoder.pulsesPerRevolution < 1 || !std::isfinite(encoder.speed))
        throw invalid(QStringLiteral("The encoder needs a finite speed and at least one pulse per revolution"));
    std::lock_guard lock(d->mutex);
    const double t = d->now();
    d->encoderPhase = d->encoderPosition(t);
    d->encoderTime = t;
    d->encoder = encoder;
    d->applyEncoder(t);
}

SimEncoder SimDevice::encoder() const
{
    std::lock_guard lock(d->mutex);
    return d->encoder;
}

double SimDevice::encoderPosition() const
{
    std::lock_guard lock(d->mutex);
    return d->encoderPosition(d->now());
}

void SimDevice::setDmmValue(DmmFunction function, double value)
{
    std::lock_guard lock(d->mutex);
    d->dmmValues[index(function)] = value;
}

double SimDevice::dmmValue(DmmFunction function) const
{
    std::lock_guard lock(d->mutex);
    return d->dmmValues[index(function)];
}

void SimDevice::setDmmOpenCircuit(bool open)
{
    std::lock_guard lock(d->mutex);
    d->dmmOpenCircuit = open;
}

bool SimDevice::dmmOpenCircuit() const
{
    std::lock_guard lock(d->mutex);
    return d->dmmOpenCircuit;
}

void SimDevice::setDmmReadTime(int dcMs, int acMs)
{
    std::lock_guard lock(d->mutex);
    d->dmmDcMs = std::max(0, dcMs);
    d->dmmAcMs = std::max(0, acMs);
}

}  // namespace mydaq
