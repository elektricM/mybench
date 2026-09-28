// Oscilloscope core: the acquisition and analysis logic of the Scope, free of QML so it can be
// unit tested against the simulator (instruments/scope/tests).
//
// Behaviour follows docs/instruments/algorithms/scope.md (recovered ELVISmx behaviour, the
// authoritative source) and docs/instruments/scope.md (MYBENCH functional spec):
//   - value lists (Time/Div 5 µs … 200 ms, Volts/Div 10 mV … 5 V for AI, … 1 V for audio);
//   - record length / sample rate policy (≈ 2501 samples per screen, ELVISmx §2);
//   - input range from Volts/Div (≥ 500 mV/div → ±10 V, else ±2 V; audio always ±2 V);
//   - software edge trigger on a 2N-sample acquisition (previous/current comparison, no
//     hysteresis by default, trigger sample at N × H % of the screen);
//   - measurements RMS / Freq (FFT single tone, shown only above 0.2 × V/div) / Vp-p, and the
//     ELVISmx number format (3 decimals when |v| ≥ 1, else 2, with SI prefix);
//   - autoscale (16 000-sample probe, A = 2.2 × half amplitude, Volts/Div and Time/Div = the list
//     entries nearest to 0.3 × A and 0.5 / f with LabVIEW's "threshold and round").
#pragma once

#include <mydaq/device.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace mybench::instruments::scope {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---------------------------------------------------------------------------------------------
// Settings vocabulary

enum class Source { AI0, AI1, AudioLeft, AudioRight };
enum class InputRange { Auto, Range10V, Range2V };      // MYBENCH extension (Advanced)
enum class TriggerType { Immediate, Edge };             // ELVISmx "Digital" is ELVIS-only
enum class TriggerSource { Chan0, Chan1 };              // "Chan 0 Source" / "Chan 1 Source"
enum class Slope { Rising, Falling };
enum class SamplingMode { Standard, HighResolution };   // Standard = ELVISmx policy
enum class NoTrigger { Clear, KeepLast, Auto };        // Clear = ELVISmx (empty graph)
enum class MathFunction { Sum, Difference, Product, InvertA, InvertB, FftA, FftB };

bool isAudio(Source source);
mydaq::AiChannel aiChannel(Source source);
QString sourceName(Source source);   // "AI 0", "AI 1", "AudioInput Left", "AudioInput Right"

// Time/Div values, ascending (5 µs … 200 ms, 15 values, default 5 ms).
const std::vector<double>& timePerDivValues();
// Volts/Div values of a source, ascending (AI: 10 mV … 5 V; audio: 10 mV … 1 V).
const std::vector<double>& voltsPerDivValues(bool audio);
// "5 ms", "500 µs", "1 V", "20 mV" (ELVISmx ring labels).
QString divisionLabel(double value, const QString& unit);
// Index of the list entry nearest to `value` (log distance).
int nearestIndex(const std::vector<double>& ascending, double value);

// ---------------------------------------------------------------------------------------------
// Rate and record length (ELVISmx subScanRateSelect)

struct RatePlan {
    double rate = 0;        // requested per-channel sample rate, S/s
    int64_t samples = 0;    // record length N (samples + 1: both screen edges carry a sample)
};
// Standard: ELVISmx (≤ 2501 samples per screen: full rate up to 1 ms/div). HighResolution
// (MYBENCH): up to 100 000 samples per screen, rate = min(maxRate, 100 000 / Ts) (as the
// 8-Channel Oscilloscope featured app: full rate up to 50 ms/div), N = round(rate × Ts) + 1.
RatePlan planRate(double timePerDiv, double maxRate, SamplingMode mode = SamplingMode::Standard);

// Input range (±V) of a channel: audio 2 V; AI Auto: 10 V from 500 mV/div up, else 2 V.
double inputRangeFor(Source source, double voltsPerDiv, InputRange mode = InputRange::Auto);

// ---------------------------------------------------------------------------------------------
// Software edge trigger (ELVISmx submyDAQScopeAcquire)

struct TriggerSettings {
    double level = 0;          // V
    Slope slope = Slope::Rising;
    double hysteresis = 0;     // V (0 = ELVISmx: plain previous/current comparison)
};
// First sample index at which the trigger pre-position lands, pre = N × H / 100 converted to an
// index like LabVIEW does (round half to even), clamped to [0, N].
int64_t pretriggerSamples(int64_t recordLength, double horizontalPercent);
// Searches x[pre, pre + N) of a 2N-sample acquisition for the first crossing. Rising:
// previous < level ≤ sample; falling: previous > level ≥ sample; `previous` starts equal to the
// level, so the first sample of the window never triggers. With hysteresis > 0 the edge must be
// armed first (rising: a sample below level − hysteresis). Returns the offset k of the crossing
// inside the window: the displayed record is x[k, k + N), whose sample `pre` is the trigger.
std::optional<int64_t> findTrigger(const std::vector<double>& x, int64_t recordLength, int64_t pre,
                                   const TriggerSettings& trigger);

// ---------------------------------------------------------------------------------------------
// Measurements (ELVISmx subUIDisplay + MYBENCH extensions)

struct Measurements {
    // ELVISmx
    double rms = kNaN;          // V, DC included (after coupling)
    double frequency = kNaN;    // Hz, FFT single tone; NaN when the tone ≤ 0.2 × V/div
    double peakToPeak = kNaN;   // V, max − min
    // MYBENCH extensions
    double mean = kNaN;
    double minimum = kNaN;
    double maximum = kNaN;
    double amplitude = kNaN;    // V, high − low state levels (histogram modes)
    double high = kNaN;
    double low = kNaN;
    double period = kNaN;       // s, from mid-level crossings (counter style)
    double counterFrequency = kNaN;   // Hz, 1 / period
    double dutyCycle = kNaN;    // 0..1
    double riseTime = kNaN;     // s, 10 % → 90 % of low..high
    double fallTime = kNaN;     // s, 90 % → 10 %
    double positiveWidth = kNaN;    // s, at the mid level
    double negativeWidth = kNaN;    // s
};
// `voltsPerDiv` sets the Freq threshold (tone amplitude must exceed 0.2 × V/div).
Measurements measure(const std::vector<double>& x, double sampleRate, double voltsPerDiv);

// ELVISmx measurement text: SI prefix, 3 decimals when |value| ≥ 1 (in base units), else 2;
// "?" for NaN/infinity ("707.11 mV", "1.000 kHz", "0.00 V", "25.00 µs").
QString formatMeasurement(double value, const QString& unit);
// Fixed decimals of the SI mantissa ("50.00 kS/s", "200.00 kS/s": the Sample Rate indicator).
QString formatFixed(double value, const QString& unit, int decimals);

// ---------------------------------------------------------------------------------------------
// Autoscale (ELVISmx subMswScopeGetAutoSetupValues + Autoscale TimeBase)

// LabVIEW "Threshold 1D Array" on an ascending table followed by rounding to the nearest index:
// the fractional index where `value` would sit (linear interpolation between neighbours),
// clamped to the table, then rounded.
int thresholdIndex(const std::vector<double>& ascending, double value);

struct AutoscaleProbe {           // analysis of one channel's 16 000-sample probe block
    double maximum = kNaN;
    double minimum = kNaN;
    double toneFrequency = kNaN;
    double toneAmplitude = kNaN;
};
struct AutoscaleChannel {
    double signalRange = kNaN;    // entry of the signal-range table (tone threshold)
    double midLevel = kNaN;       // (max + min) / 2 of the probe (MYBENCH: trigger level option)
    double voltsPerDiv = kNaN;    // new Volts/Div
    bool tonePresent = false;
    double frequency = kNaN;      // Hz when tonePresent
};
AutoscaleProbe analyseProbe(const std::vector<double>& block, double sampleRate);
AutoscaleChannel autoscaleChannel(const AutoscaleProbe& probe, bool audio);
// Time/Div for the lowest detected frequency: 0.5 / f snapped to the list; NaN when none.
double autoscaleTimePerDiv(double lowestFrequency);
inline constexpr int64_t kAutoscaleSamples = 16000;
const std::vector<double>& autoscaleRates();   // probe rates, tried in order until a tone is found

// ---------------------------------------------------------------------------------------------
// Math channel (MYBENCH extension)

bool isFft(MathFunction f);
bool needsBothChannels(MathFunction f);
QString mathName(MathFunction f);   // "CH 0 + CH 1", ..., "FFT CH 0"
std::vector<double> computeMath(MathFunction f, const std::vector<double>& a, const std::vector<double>& b);
// Magnitude spectrum in dBV RMS (Hanning window), bins 0 … N/2 spaced sampleRate / N, and its
// strongest tone.
struct FftResult {
    std::vector<double> db;          // dBV RMS per bin
    double binWidth = 0;             // Hz
    double peakFrequency = kNaN;     // strongest non-DC tone (interpolated), Hz
    double peakLevel = kNaN;         // its level, dBV RMS
};
FftResult fftAnalyse(const std::vector<double>& x, double sampleRate);

// ---------------------------------------------------------------------------------------------
// Acquisition

struct ChannelSettings {
    Source source = Source::AI0;
    bool enabled = true;
    double voltsPerDiv = 1.0;
    InputRange range = InputRange::Auto;
};

struct Snapshot {
    ChannelSettings channel[2] = {{Source::AI0, true, 1.0, InputRange::Auto},
                                  {Source::AI1, false, 1.0, InputRange::Auto}};
    double timePerDiv = 5e-3;
    SamplingMode sampling = SamplingMode::Standard;
    bool roll = false;
    TriggerType triggerType = TriggerType::Immediate;
    TriggerSource triggerSource = TriggerSource::Chan0;
    Slope slope = Slope::Rising;
    double level = 0;
    double horizontalPercent = 50;
    double hysteresisDiv = 0;       // Advanced; 0 = ELVISmx
    NoTrigger noTrigger = NoTrigger::Clear;   // Advanced; Clear = ELVISmx
    bool mathEnabled = false;
    MathFunction mathFunction = MathFunction::Sum;
};

// Validation message (empty when the snapshot can run): no channel, AI mixed with audio.
QString validate(const Snapshot& snapshot);

// Which task columns feed which scope channels, the task configuration and record length.
struct AcquisitionPlan {
    mydaq::AiConfig config;          // channels, rate, Finite, samplesPerChannel (N or 2N)
    int column[2] = {-1, -1};        // task column of CH 0 / CH 1 (-1: disabled)
    double range[2] = {0, 0};        // effective input range of CH 0 / CH 1
    int64_t recordLength = 0;        // N
    bool edge = false;               // acquires 2N and searches a trigger
    int triggerChannel = 0;          // scope channel of the trigger source
};
AcquisitionPlan planAcquisition(const Snapshot& snapshot, const mydaq::Device& device);
bool sameTask(const AcquisitionPlan& a, const AcquisitionPlan& b);

// One displayed record.
struct Record {
    std::vector<double> data[2];     // volts after coupling, N samples (empty: disabled)
    double sampleRate = 0;           // actual rate
    double skew[2] = {0, 0};         // display time offset of each channel (inter-channel skew)
    bool triggered = false;          // Edge: a crossing was found
    int64_t triggerIndex = -1;       // index of the trigger sample in the record (Edge)
    bool enabled(int ch) const { return !data[ch].empty(); }
};

// Result of one acquisition cycle.
struct CycleResult {
    std::optional<Record> record;    // nullopt: nothing to display (no trigger, or stopped)
    bool timedOut = false;           // Edge without crossing (Timeout LED)
    bool stopped = false;            // interrupted by stop
    int64_t samplesMoved = 0;        // samples per channel × channels read
};

// Owns the AI task between cycles (recreated only when the plan changes) and runs one cycle:
// start, read N or 2N samples in ≤ 100 ms chunks (so stop reacts quickly), trigger search,
// coupling. Used from one thread.
class Acquirer
{
public:
    using StopFn = std::function<bool()>;

    CycleResult acquire(mydaq::Device& device, const Snapshot& snapshot, const StopFn& stop);
    // Destroys the task (e.g. before another AI task such as the autoscale probe).
    void reset();
    double sampleRate() const;
    const AcquisitionPlan& plan() const { return m_plan; }

    // Reads `total` samples per channel of a started finite task in chunks. Empty when stopped.
    static std::vector<std::vector<double>> readChunked(mydaq::AiTask& task, int64_t total, const StopFn& stop);

private:
    std::unique_ptr<mydaq::AiTask> m_task;
    AcquisitionPlan m_plan;
};

// AC coupling (audio sources): subtract the record mean in place.
void removeMean(std::vector<double>& x);

// Autoscale probe of the enabled channels (one task, ±10 V for AI / ±2 V audio, 16 000 samples,
// retried at slower rates while no tone is found). Returns per scope channel results (NaN when
// the channel is disabled); `stop` interrupts it (then nullopt).
struct AutoscaleResult {
    AutoscaleChannel channel[2];
    double timePerDiv = kNaN;        // NaN: keep
};
std::optional<AutoscaleResult> runAutoscale(mydaq::Device& device, const Snapshot& snapshot,
                                            const Acquirer::StopFn& stop);

// Roll mode: a continuous task and a sliding window of the last N samples.
class Roller
{
public:
    // Creates and starts the continuous task. Throws mydaq::DeviceError.
    void start(mydaq::Device& device, const Snapshot& snapshot);
    void stop();
    bool running() const { return m_task != nullptr; }
    // Appends what arrived since the last call (non-blocking). Returns the samples moved.
    int64_t poll();
    // The current window as a record (the newest sample is the last one).
    Record record() const;
    int64_t windowLength() const { return m_plan.recordLength; }
    // True when the running task already acquires what `plan` (from planAcquisition) asks for.
    bool matches(const AcquisitionPlan& plan) const;
    int64_t filled() const;
    const AcquisitionPlan& plan() const { return m_plan; }

private:
    std::unique_ptr<mydaq::AiTask> m_task;
    AcquisitionPlan m_plan;
    std::vector<std::vector<double>> m_ring;   // per task column, capacity N
    int64_t m_count = 0;                       // samples received in total
};

}  // namespace mybench::instruments::scope
