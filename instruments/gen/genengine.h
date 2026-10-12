// Generation engine of the Waveform Generator (docs/instruments/gen.md 3).
//
// UI-free and device-agnostic: it only uses the mydaq::Device API, so it runs the same on the
// simulator, the USB backend and in the unit tests. The worker thread calls Engine::run(), which
// generates until every channel has stopped or finished, or until the host asks it to stop.
//
// Strategies, chosen from the channel settings and the engine preference (chooseStrategy()):
//   Static      every generating channel is a DC level: on-demand writes (Device::aoWriteStatic),
//               no sample clock (the ELVISmx "DC Level" application).
//   Stream      the reference engine (gen.md 3.1-3.5): a continuous stream at the device's maximum
//               rate for both channels of the pair. Function channels are DDS phase accumulators
//               (glitch-free, phase-continuous changes, sample-exact sweep steps), Arbitrary
//               channels are resampled by a position accumulator at their own Update Rate (Hold or
//               Linear interpolation), DC levels are ramped. One channel starts or stops without
//               disturbing the other.
//   Regenerate  one buffer that the device loops, as ELVISmx does: a Function channel is one period
//               of planFgenBuffer() (Fs = 10 000 f up to the maximum rate, else f round(Rmax / f));
//               Arbitrary channels play gain x waveform at their Update Rate (Run Once = a finite
//               task). Live changes stop, rewrite and restart the task (not phase continuous).
//               Only the generating channels are in the task (ELVISmx: one AO channel).
// Preferences: Auto (default) streams everything except DC-only outputs (Static), and falls back
// to Regenerate when the backend cannot stream; Regenerate reproduces ELVISmx (looped buffer
// whenever one buffer can represent the outputs, else the stream); Stream always streams. A running
// Stream is never downgraded in Auto (so one channel can stop without disturbing the other).
#pragma once

#include <mydaq/device.h>

#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace mybench::instruments::gen {

enum class ChannelMode { Off, Function, Arbitrary, Dc };

// Arbitrary Update Rate limits: ELVISmx accepts 0.1 S/s (gen.md 4.2.2); the top is the task rate.
inline constexpr double kMinUpdateRate = 0.1;
enum class Shape { Sine, Triangle, Square };   // ELVISmx button order
enum class OutputPair { AnalogOutputs, AudioOutputs };
enum class StrategyKind { None, Static, Regenerate, Stream };
enum class EnginePreference { Auto, Regenerate, Stream };

struct FunctionSettings {
    Shape shape = Shape::Sine;
    double frequency = 100.0;   // Hz
    double amplitude = 1.0;     // Vpp
    double offset = 0.0;        // V
    double dutyCycle = 50.0;    // % (Square)
    double phase = 0.0;         // degrees [MYBENCH], Stream only
    bool operator==(const FunctionSettings&) const = default;
};

struct SweepSettings {
    double start = 100.0;      // Hz
    double stop = 1000.0;      // Hz
    double step = 100.0;       // Hz
    double intervalMs = 1000;  // 1 ... 300 000 ms
    bool blockTiming = false;  // true: steps last a whole number of 250 ms blocks (ELVISmx)
    bool operator==(const SweepSettings&) const = default;
};

struct ArbSettings {
    std::shared_ptr<const std::vector<double>> waveform;   // volts, before gain
    double gain = 1.0;
    double updateRate = 1000.0;   // S/s
    bool once = false;            // Run Once (else Run Continuously)
    bool linear = false;          // [MYBENCH] linear interpolation between samples (Stream)
    bool zeroAfterOnce = false;   // Run Once ends at 0 V (ELVISmx) instead of holding the last sample
};

struct ChannelSettings {
    ChannelMode mode = ChannelMode::Off;
    bool active = false;     // output on
    bool sweep = false;      // Function: single sweep instead of continuous
    uint64_t startId = 0;    // new value = the channel (re)starts: phase / position reset
    FunctionSettings function;
    SweepSettings sweepSettings;
    ArbSettings arb;
    double dcLevel = 0.0;    // V
    bool generating() const { return active && mode != ChannelMode::Off; }
};

struct EngineSettings {
    OutputPair pair = OutputPair::AnalogOutputs;
    std::array<ChannelSettings, 2> channels;
    EnginePreference preference = EnginePreference::Auto;
    uint64_t alignId = 0;    // new value: Stream restarts both Function phases together
};

enum class ChannelState { Idle, Running, Sweeping, Done };

struct ChannelStatus {
    ChannelState state = ChannelState::Idle;
    uint64_t startId = 0;
    double frequency = 0;     // Function: generated frequency (Hz): DDS, or the requested one (Regenerate)
    int bufferLength = 0;     // Function in Regenerate: samples per period of the looped buffer
    double bufferFrequency = 0;   // ... and the frequency that buffer really produces
    double updateRate = 0;    // Arbitrary: actual update rate (S/s)
    double level = 0;         // DC level / held value of a finished Run Once
    int sweepStep = 0;        // 1-based current step
    int sweepSteps = 0;
    bool sweepFinished = false;
    bool finished = false;    // Run Once over with zeroAfterOnce: back at 0 V
    bool operator==(const ChannelStatus&) const = default;
};

struct EngineStatus {
    StrategyKind strategy = StrategyKind::None;
    double taskRate = 0;      // S/s of the AO task (0 = none)
    int taskChannels = 0;
    std::array<ChannelStatus, 2> channels;
    bool operator==(const EngineStatus&) const = default;
};

// What the engine needs from its environment (the worker context, or a test).
class EngineHost
{
public:
    virtual ~EngineHost() = default;
    virtual bool stopRequested() const = 0;
    virtual bool sleepMs(int ms) = 0;   // false when stopping
    virtual std::optional<EngineSettings> takeSettings() = 0;
    virtual void publishStatus(const EngineStatus& status) = 0;
    virtual void warn(const QString& message) = 0;
    virtual void samplesWritten(int64_t samples) { (void)samples; }
};

struct EngineResult {
    std::array<bool, 2> done{};       // Run Once finished (holding `held`; not with zeroAfterOnce)
    std::array<double, 2> held{};
    std::array<uint64_t, 2> startId{};
};

// --- Pure helpers (unit-tested) ------------------------------------------------------------------

mydaq::AoChannel aoChannel(OutputPair pair, int index);
double outputRange(OutputPair pair);   // 10 V (AO) or 2 V (audio)

// ELVISmx FGEN buffer: one period of N samples at `rate`.
struct FgenPlan {
    double requestedRate = 0;
    double rate = 0;          // coerced by the device
    size_t length = 0;        // samples per period (= buffer)
    double frequency = 0;     // generated: rate / length
};
FgenPlan planFgenBuffer(double frequency, double maxRate, const std::function<double(double)>& coerceRate);

// Sweep: frequencies Start, Start +- Step ... up to Stop (included when it falls on a step).
struct SweepPlan {
    std::vector<double> frequencies;
    double stepDuration = 0;   // s (Step Interval, rounded up to 250 ms blocks when blockTiming)
};
SweepPlan makeSweep(const SweepSettings& sweep);

// One period (N samples) of a Function channel: O + Vpp/2 shape(k / N).
std::vector<double> functionPeriod(const FunctionSettings& function, size_t length, double range);
double shapeValue(Shape shape, double cycles, double duty);   // unit shape at phase `cycles`

// Strategy for `settings` given the one currently running (`current`); `streamingSupported`
// false after the backend refused a streaming task.
StrategyKind chooseStrategy(const EngineSettings& settings, StrategyKind current, bool streamingSupported = true);
// A single regenerated buffer can represent the generating channels.
bool isRegenerable(const EngineSettings& settings);

// DDS phase increment for `frequency` at `rate` and the frequency it really produces.
uint64_t phaseIncrement(double frequency, double rate);
double ddsFrequency(uint64_t increment, double rate);

// The engine's own wording of an error when the framework would replace it (a second underflow:
// "... The generator was stopped.", gen.md 7): returned as a Generic error, others unchanged.
mydaq::DeviceError userFacingError(const mydaq::DeviceError& error);

// --- Engine --------------------------------------------------------------------------------------

class Strategy;

class Engine
{
public:
    Engine(mydaq::Device& device, EngineHost& host);
    ~Engine();

    // Generates until no channel is generating, or the host stops it. Throws mydaq::DeviceError on
    // device failures (after cleaning up). Outputs end at 0 V (AO pair) except channels whose Run
    // Once finished on its own, which hold their last sample.
    EngineResult run(const EngineSettings& initial);

    // Stream tuning (tests may shorten them).
    struct StreamTiming {
        double blockSeconds = 0.010;
        double leadSeconds = 0.100;
        double fallbackRate = 100000.0;
    };
    void setStreamTiming(const StreamTiming& timing) { m_timing = timing; }

private:
    std::unique_ptr<Strategy> makeStrategy(StrategyKind kind);
    void staticSafeState(const EngineSettings& settings, const EngineResult& result, bool natural);

    mydaq::Device& m_device;
    EngineHost& m_host;
    StreamTiming m_timing;
};

}  // namespace mybench::instruments::gen
