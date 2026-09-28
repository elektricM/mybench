// Hardware abstraction for the NI myDAQ (and a simulated stand-in).
//
// This header is the contract between the instruments (UI), the simulator and the native USB
// backend. Semantics follow NI-DAQmx loosely so a DAQmx backend can map 1:1 on Windows.
//
// Threading: every Device method is thread-safe. A task object is used from one thread at a time
// (typically an instrument's worker thread). Blocking calls take explicit timeouts.
// Errors: all failures throw mydaq::DeviceError.
#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mydaq {

class DeviceError : public std::runtime_error {
public:
    enum Code {
        Generic,
        NotConnected,   // device unplugged / not opened
        ResourceBusy,   // subsystem or channel already reserved by another task
        InvalidConfig,  // bad channel combination, rate, range, sample count...
        Timeout,        // read/write/wait did not complete in time
        Overflow,       // continuous AI: host did not read fast enough, data lost
        Underflow,      // streaming AO: host did not write fast enough
        UsbError,       // transport failure
        NotSupported,
    };
    DeviceError(Code code, const QString& message)
        : std::runtime_error(message.toStdString()), m_code(code) {}
    Code code() const { return m_code; }
    QString message() const { return QString::fromStdString(what()); }

private:
    Code m_code;
};

// ---------------------------------------------------------------------------------------------
// Channels

enum class AiChannel { AI0, AI1, AudioInLeft, AudioInRight };
enum class AoChannel { AO0, AO1, AudioOutLeft, AudioOutRight };

QString toString(AiChannel ch);   // "AI 0", "AI 1", "AudioInput Left", "AudioInput Right"
QString toString(AoChannel ch);   // "AO 0", "AO 1", "AudioOutput Left", "AudioOutput Right"
inline bool isAudio(AiChannel ch) { return ch == AiChannel::AudioInLeft || ch == AiChannel::AudioInRight; }
inline bool isAudio(AoChannel ch) { return ch == AoChannel::AudioOutLeft || ch == AoChannel::AudioOutRight; }

enum class SampleMode { Finite, Continuous };

// ---------------------------------------------------------------------------------------------
// Analog input (AI0/AI1 differential; AudioIn L/R). AI and AudioIn share the ADCs: one task may
// use AI channels or audio channels, never both, and only one AI task can exist at a time.

struct AiChannelConfig {
    AiChannel channel = AiChannel::AI0;
    double range = 10.0;   // symmetric range ±range V. AI0/AI1: 10 or 2. Audio: 2 (fixed).
};

struct AiConfig {
    std::vector<AiChannelConfig> channels;   // 1..2 channels, no duplicates
    double sampleRate = 1000.0;              // per channel, S/s; coerced (see Device::coerceAiRate)
    SampleMode mode = SampleMode::Finite;
    int64_t samplesPerChannel = 1000;        // Finite: exact count per start(). Continuous: host buffer hint
};

class AiTask {
public:
    virtual ~AiTask() = default;                 // stops the task and releases the reservation
    virtual const AiConfig& config() const = 0;
    virtual double sampleRate() const = 0;       // actual rate after coercion
    // Inter-channel sampling skew in seconds: channel k is sampled k*skew after channel 0 within a
    // scan (0 for simultaneous sampling). Phase-sensitive instruments (Bode) correct for it.
    virtual double channelSkew() const = 0;
    virtual void start() = 0;                    // Finite: begins a new acquisition of samplesPerChannel
    virtual void stop() = 0;                     // idempotent
    virtual bool isRunning() const = 0;
    // Blocks until n samples per channel are available or timeoutMs elapses (throws Timeout).
    // Returns data[channel][sample] in volts, channels in config order.
    // n < 0: return whatever is available now without blocking (may be empty).
    // Finite: n must not exceed the remaining samples. Continuous: throws Overflow if data was lost.
    virtual std::vector<std::vector<double>> read(int64_t n, int timeoutMs) = 0;
    virtual int64_t available() const = 0;       // samples per channel readable without blocking
};

// ---------------------------------------------------------------------------------------------
// Analog output (AO0/AO1 ±10 V; AudioOut L/R ±2 V). One hardware-timed AO task at a time.

struct AoChannelConfig {
    AoChannel channel = AoChannel::AO0;
    double range = 10.0;   // AO0/AO1: 10 or 2 (finer steps). Audio: 2.
};

struct AoConfig {
    std::vector<AoChannelConfig> channels;   // 1..2 channels, no duplicates
    double sampleRate = 1000.0;              // per channel, S/s; coerced (see Device::coerceAoRate)
    SampleMode mode = SampleMode::Continuous;
    // Continuous only. true: the buffer written before start() repeats until stop().
    // false: streaming; the caller keeps calling write() while running (Underflow if it falls behind).
    bool regenerate = true;
};

class AoTask {
public:
    virtual ~AoTask() = default;                 // stops the task and releases the reservation
    virtual const AoConfig& config() const = 0;
    virtual double sampleRate() const = 0;       // actual rate after coercion
    // data[channel][sample] in volts, channels in config order, all channels the same length.
    // Before start(): sets the buffer (Finite, or Continuous+regenerate) / pre-fills (streaming).
    // While running with regenerate=false: appends, blocking up to timeoutMs while the buffer is full.
    // While running with regenerate=true: throws NotSupported (stop, write, start instead).
    virtual void write(const std::vector<std::vector<double>>& data, int timeoutMs = 10000) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;                     // idempotent; outputs keep their last value
    virtual bool isRunning() const = 0;
    virtual bool waitUntilDone(int timeoutMs) = 0;   // Finite: true once all samples were generated
    virtual int64_t spaceAvailable() const = 0;      // streaming: samples/channel writable without blocking
};

// ---------------------------------------------------------------------------------------------
// DMM (separate isolated converter; usable while AI/AO tasks run)

enum class DmmFunction { DcVoltage, AcVoltage, DcCurrent, AcCurrent, Resistance, Diode, Continuity };
QString toString(DmmFunction f);
QString unitOf(DmmFunction f);   // "V", "A", "Ω"

struct DmmRange {
    double max = 0;   // full scale in base units (V, A, Ω)
    QString label;    // e.g. "20 V", "200 mA", "2 MΩ"
};

struct DmmReading {
    double value = 0;       // base units; AC functions return RMS
    bool overload = false;  // input exceeds the range ("OL" on the display); only the sign of value is meaningful (±OVER)
};

// ---------------------------------------------------------------------------------------------
// Static digital I/O: 8 lines (DIO 0..7), direction per line, software-timed.

constexpr int kDioLines = 8;

// ---------------------------------------------------------------------------------------------
// Counter/timer ctr0 and the FREQOUT generator. Their signals use DIO lines (PFI n = DIO n); a
// counter task reserves the lines it uses, and DIO calls on reserved lines throw ResourceBusy.
// Default terminals (as NI-DAQmx resolves them on the myDAQ):
//   CountEdges: edges DIO0, direction DIO2 (ExternalDirection), reset DIO1 (when enabled)
//   Frequency / Period / PulseWidth / SemiPeriod: input DIO1
//   TwoEdgeSeparation: first edge DIO2, second edge DIO1
//   Encoders: A DIO0, B DIO2, Z DIO1
//   Pulse train output (ctr0): DIO3;  FREQOUT: DIO4
// There is one counter: at most one counter task (input or output) at a time; FREQOUT is separate.

enum class Edge { Rising, Falling };

enum class CounterMeasurement {
    CountEdges,          // value: edge count (32-bit, wraps)
    Frequency,           // value: Hz (reciprocal of one period of the input)
    Period,              // value: s
    PulseWidth,          // value: s (high time for Rising, low time for Falling)
    SemiPeriod,          // value: s (time between consecutive edges of either polarity)
    TwoEdgeSeparation,   // value: s (first edge on DIO2 to second edge on DIO1)
    LinearEncoder,       // value: position in `distancePerPulse` units
    AngularEncoder,      // value: angle in degrees
};
QString toString(CounterMeasurement m);

struct CounterInputConfig {
    CounterMeasurement measurement = CounterMeasurement::CountEdges;
    Edge edge = Edge::Rising;        // active/starting edge (TwoEdgeSeparation: first edge)
    Edge secondEdge = Edge::Falling; // TwoEdgeSeparation only
    // CountEdges
    enum class Direction { Up, Down, External } direction = Direction::Up;
    uint32_t initialCount = 0;
    bool resetOnDio1 = false;        // count reset input on DIO1
    // Frequency/Period/PulseWidth/SemiPeriod/TwoEdgeSeparation: expected range of the value, used to
    // pick the internal timebase (100 MHz, 20 MHz or 100 kHz) so the 32-bit count neither overflows
    // nor loses resolution.
    double minValue = 1.0;
    double maxValue = 1.0e6;
    // Encoders
    enum class Decoding { X1, X2, X4, TwoPulse } decoding = Decoding::X4;
    bool zIndexEnabled = false;
    double zIndexValue = 0;
    double distancePerPulse = 1.0;   // LinearEncoder
    int pulsesPerRevolution = 1024;  // AngularEncoder
    // On-demand (readScalar) when false; buffered implicit timing (one sample per measured period,
    // pulse or edge, read with readBuffered) when true.
    bool buffered = false;
    SampleMode mode = SampleMode::Continuous;   // buffered only
    int64_t samplesPerChannel = 1000;           // buffered: Finite count / Continuous buffer hint
};

class CounterInputTask {
public:
    virtual ~CounterInputTask() = default;   // stops and releases ctr0 and its DIO lines
    virtual const CounterInputConfig& config() const = 0;
    virtual uint8_t dioLinesUsed() const = 0;   // mask of reserved DIO lines
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    // On-demand: latest value in the measurement's unit. Blocks up to timeoutMs for the first
    // complete measurement (Frequency/Period need an input edge); Timeout if none arrives.
    virtual double readScalar(int timeoutMs) = 0;
    // Buffered: blocks until n values are available (n < 0: whatever is available, non-blocking).
    virtual std::vector<double> readBuffered(int64_t n, int timeoutMs) = 0;
};

struct PulseOutputConfig {
    double frequency = 1000.0;     // Hz; coerced to the timebase (see coerceCounterFrequency)
    double dutyCycle = 0.5;        // 0 < duty < 1, coerced so high and low times are >= 2 ticks
    bool idleHigh = false;         // output level while idle / between pulses
    double initialDelay = 0.0;     // s before the first pulse
    SampleMode mode = SampleMode::Continuous;
    int64_t pulseCount = 1;        // Finite only
};

class CounterOutputTask {
public:
    virtual ~CounterOutputTask() = default;   // stops, output to idle level, releases ctr0 and DIO3
    virtual const PulseOutputConfig& config() const = 0;
    virtual double actualFrequency() const = 0;
    virtual double actualDutyCycle() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    // While running (Continuous): change frequency/duty without stopping (takes effect at the next
    // period boundary).
    virtual void update(double frequency, double dutyCycle) = 0;
    virtual bool waitUntilDone(int timeoutMs) = 0;   // Finite: true once all pulses were generated
};

// ---------------------------------------------------------------------------------------------

// Optional features: names for Device::unsupportedFeatures(), so panels can grey out options a
// backend does not implement (yet) instead of failing at Run. Feature names are stable strings
// "<area>.<feature>"; a backend lists only what it does NOT support.
namespace feature {
// Counter input measurements / options (CounterInputConfig).
inline constexpr char CounterPulseWidth[] = "counter.pulseWidth";
inline constexpr char CounterSemiPeriod[] = "counter.semiPeriod";
inline constexpr char CounterTwoEdgeSeparation[] = "counter.twoEdgeSeparation";
inline constexpr char CounterLinearEncoder[] = "counter.linearEncoder";
inline constexpr char CounterAngularEncoder[] = "counter.angularEncoder";
inline constexpr char CounterFallingEdge[] = "counter.fallingEdge";
inline constexpr char CounterCountDown[] = "counter.countDown";
inline constexpr char CounterExternalDirection[] = "counter.externalDirection";
inline constexpr char CounterInitialCount[] = "counter.initialCount";
inline constexpr char CounterResetOnDio[] = "counter.resetOnDio";
inline constexpr char CounterBuffered[] = "counter.buffered";
// Pulse train output options (PulseOutputConfig).
inline constexpr char PulseIdleHigh[] = "pulse.idleHigh";
inline constexpr char PulseInitialDelay[] = "pulse.initialDelay";
// FREQOUT on DIO4.
inline constexpr char FrequencyOutput[] = "freqout";
// ±2 V range of AO0/AO1 (AoChannelConfig::range = 2).
inline constexpr char AoRange2V[] = "ao.range2V";
}  // namespace feature

struct DeviceInfo {
    QString id;            // stable key, e.g. "usb:036CD1C6" or "sim:0"
    QString name;          // display name, e.g. "myDAQ (036CD1C6)" or "Simulated myDAQ"
    QString productType;   // "NI myDAQ"
    QString serialNumber;
    bool simulated = false;
};

class Device {
public:
    virtual ~Device() = default;

    virtual DeviceInfo info() const = 0;
    virtual bool isConnected() const = 0;

    // Rates the hardware can produce (divider of an internal timebase), nearest to `requested`.
    virtual double maxAiRate() const = 0;   // per channel
    virtual double maxAoRate() const = 0;   // per channel
    virtual double coerceAiRate(double requested) const = 0;
    virtual double coerceAoRate(double requested) const = 0;

    // Tasks reserve their subsystem from creation until destruction (ResourceBusy otherwise).
    virtual std::unique_ptr<AiTask> createAiTask(const AiConfig& config) = 0;
    virtual std::unique_ptr<AoTask> createAoTask(const AoConfig& config) = 0;

    // On-demand single values (no task). AI: ResourceBusy while an AI task exists.
    virtual double aiReadStatic(AiChannel channel, double range) = 0;
    virtual void aoWriteStatic(AoChannel channel, double volts) = 0;   // ResourceBusy while a timed AO task exists

    // DMM: one blocking reading (AC functions take longer). Ranges are listed largest first.
    virtual std::vector<DmmRange> dmmRanges(DmmFunction function) const = 0;
    virtual DmmReading dmmRead(DmmFunction function, double rangeMax) = 0;

    // DIO: lines with a 1 in `mask` become outputs (output=true) or inputs (output=false).
    virtual void dioSetDirection(uint8_t mask, bool output) = 0;
    virtual uint8_t dioDirection() const = 0;                    // 1 = output
    virtual void dioWrite(uint8_t value, uint8_t mask = 0xFF) = 0;   // only output lines in mask change
    virtual uint8_t dioRead() = 0;                               // level of all 8 lines

    // Counter/timer ctr0 (one task at a time, input or output) and FREQOUT on DIO4. Backends that
    // don't implement them yet keep these defaults (NotSupported).
    virtual std::unique_ptr<CounterInputTask> createCounterInputTask(const CounterInputConfig&) { throw notSupported(); }
    virtual std::unique_ptr<CounterOutputTask> createCounterOutputTask(const PulseOutputConfig&) { throw notSupported(); }
    virtual double coerceCounterFrequency(double requested) const { return requested; }   // pulse train, 100 MHz / n
    // FREQOUT: square wave of timebase/n with timebase 20 MHz, 10 MHz or 100 kHz and n = 1..16.
    virtual double coerceFrequencyOutput(double requested) const { return requested; }
    virtual double frequencyOutputStart(double) { throw notSupported(); }   // reserves DIO4; returns the actual Hz
    virtual void frequencyOutputStop() {}                                   // releases DIO4
    virtual bool frequencyOutputRunning() const { return false; }

    // Stops all tasks (AI, AO, counter, FREQOUT), outputs to 0 V, DIO to inputs.
    virtual void reset() = 0;

    // Optional features (mydaq::feature names) this backend does not implement: the panels grey
    // them out. Default: everything is supported. supports() is the convenience check.
    virtual QStringList unsupportedFeatures() const { return {}; }
    bool supports(const QString& feature) const { return !unsupportedFeatures().contains(feature); }

protected:
    static DeviceError notSupported() { return DeviceError(DeviceError::NotSupported, QStringLiteral("Not supported by this device backend")); }
};

using DevicePtr = std::shared_ptr<Device>;

}  // namespace mydaq
