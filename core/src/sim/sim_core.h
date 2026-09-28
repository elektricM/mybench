// State shared by a SimDevice and its tasks (tasks keep it alive, so a task may outlive its device).
#pragma once

#include "ao_timeline.h"
#include "digital_timeline.h"
#include "mydaq/sim_device.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>

namespace mydaq::sim {

class SimAiTask;
class SimAoTask;
class SimCounterInputTask;
class SimCounterOutputTask;

inline constexpr double kTimebase = 100e6;       // Hz; sample clocks are kTimebase / divisor
inline constexpr double kMaxRate = 200e3;        // S/s per channel, AI and AO
inline constexpr double kAiHeadroom = 1.05;      // ADC span is +-1.05 x range
inline constexpr int kAiCodes = 65536;           // 16-bit converter
inline constexpr double kAudioCouplingHz = 5.0;  // AudioIn AC-coupling corner
inline constexpr double kDefaultSkew = 2.5e-6;   // s: AI0/AI1 are multiplexed at 400 kS/s
inline constexpr int kPulseOutputLine = 3;       // ctr0 output (PFI3)
inline constexpr int kFreqOutLine = 4;           // FREQOUT (PFI4)

constexpr uint8_t lineMask(int line) { return uint8_t(1u << line); }
QString lineNames(uint8_t mask);   // "DIO1, DIO3"

inline constexpr double kWakeSlack = 1e-6;       // s, added to computed wake-up times

double coerceRate(double requested);   // nearest kTimebase / integer divisor, at most kMaxRate
double quantizeAo(double volts, double range);   // 16-bit DAC code over +-1.05 x range
int index(AiChannel channel);
int index(AoChannel channel);
int index(DmmFunction function);
std::vector<DmmRange> dmmRangesOf(DmmFunction function);

struct Core {
    using Clock = std::chrono::steady_clock;

    explicit Core(DeviceInfo deviceInfo);

    // Everything below is guarded by `mutex`; `changed` is notified whenever a task stops or the
    // device is reset or disconnected, so blocked reads/writes re-check their state.
    mutable std::mutex mutex;
    std::condition_variable changed;

    const Clock::time_point epoch = Clock::now();
    DeviceInfo info;
    bool connected = true;

    SimAiTask* aiTask = nullptr;   // reservations: the live tasks register themselves
    SimAoTask* aoTask = nullptr;

    std::array<AoTimeline, 4> outputs;          // by AoChannel
    std::array<SimTestSignal, 4> testSignals;   // by AiChannel
    ZohLowPass rcFilter{1000.0};                // AO 0 -> AI 1
    std::array<ZohLowPass, 2> audioCoupling{ZohLowPass(kAudioCouplingHz), ZohLowPass(kAudioCouplingHz)};
    double noiseRms = 0.5e-3;
    QStringList unsupportedFeatures;   // simulated backend limits (default: none)
    double channelSkew = kDefaultSkew;   // reported by 2-channel AI tasks
    std::mt19937_64 rng{0x6d79444151ULL};
    std::normal_distribution<double> gaussian{0.0, 1.0};

    std::array<double, 7> dmmValues{};   // by DmmFunction
    bool dmmOpenCircuit = false;
    int dmmDcMs = 100;
    int dmmAcMs = 300;
    std::mutex dmmMutex;   // one converter: serializes readings (never taken while holding `mutex`)

    uint8_t dioDirection = 0;
    uint8_t dioLatch = 0;

    // Digital lines. dioExternal[i] is the signal applied to line i from outside (what an input
    // sees): its source, or the encoder on DIO0..2. The counter output and FREQOUT drive DIO3/DIO4.
    std::array<DigitalTimeline, kDioLines> dioExternal;
    std::array<SimDigitalSource, kDioLines> dioSources;
    SimEncoder encoder;
    double encoderTime = 0.0;       // encoder position (cycles) was encoderPhase at encoderTime
    double encoderPhase = 0.0;
    DigitalTimeline pulseOutput;    // ctr0 pulse train, while a counter output task exists
    DigitalTimeline freqOutput;     // FREQOUT square wave
    double freqOutFrequency = 0.0;  // 0: stopped
    SimCounterInputTask* ciTask = nullptr;   // at most one of the two exists
    SimCounterOutputTask* coTask = nullptr;
    uint8_t counterLines = 0;       // DIO lines reserved by the counter task

    uint8_t reservedLines() const { return uint8_t(counterLines | (freqOutFrequency > 0.0 ? lineMask(kFreqOutLine) : 0)); }
    // Throws ResourceBusy if one of `lines` is reserved or configured as a DIO output.
    void requireLinesFree(uint8_t lines) const;
    bool dioLevel(int line, double t) const;
    void applyDigitalSource(int line, double t);
    void applyEncoder(double t);
    double encoderPosition(double t) const;
    void setFrequencyOutput(double t, double frequency);   // 0 stops it

    double now() const;                                // seconds since epoch
    Clock::time_point timePoint(double t) const;       // inverse of now()
    void requireConnected() const;                     // throws NotConnected
    // One converted AI sample at device time t (advances the loopback filters: call in time order).
    double sampleAi(AiChannel channel, double range, double t);
    // Drops AO history that no pending AI sample can need.
    void prune();
    // Stops the tasks and returns the outputs and DIO to their power-up state.
    void resetLocked();
    void stopTasksLocked();

private:
    double testSignalValue(AiChannel channel, double t);
};

// Absolute deadline for a timeout in ms (negative: wait "forever", re-checked daily).
Core::Clock::time_point deadlineFor(int timeoutMs);

// Common validation, throwing InvalidConfig.
void validateAiRange(AiChannel channel, double range);
void validateAoValue(AoChannel channel, double range, double volts);

}  // namespace mydaq::sim
