// Simulated NI myDAQ: a wall-clock paced stand-in for the hardware, so every instrument can be
// developed and tested without a device. It implements the full Device contract (reservations,
// errors, blocking behaviour) and models a small test fixture wired to the inputs.
//
// Wiring
//   AO 0            -> AI 0 directly, and -> AI 1 through a first-order RC low-pass (fc = 1 kHz by
//                      default: a Bode sweep AO 0 -> AI 0 / AI 1 shows -3 dB / -45 deg at fc).
//   AudioOutput L/R -> AudioInput L/R, AC coupled (first-order high-pass, 5 Hz).
//   AO 1            -> not connected.
// An input whose source is not driven (no timed AO task generating on it, static level 0 V) shows
// its built-in test signal instead (setTestSignal(); defaults below).
// Every AI sample gets Gaussian noise (0.5 mV rms), is clipped at +-1.05 x range and quantized to
// 16 bits over that span. AO samples are quantized the same way over their range (static writes:
// +-10 V) and held (zero-order hold); the RC paths are solved exactly for it.
//
// Timing: an AI task acquires at its coerced rate from start() and read() blocks until the samples
// exist (a finite task of N samples at rate r takes N / r seconds). AO consumes samples at its rate
// on the same monotonic clock, so AO -> AI loopback is phase coherent. Rates are 100 MHz / integer
// divisor, at most 200 kS/s per channel. AI0/AI1 are multiplexed like the real device: in a
// 2-channel task the second channel is sampled channelSkew() = 2.5 us after the first (also in the
// loopback model). Continuous AI overflows when more than max(samplesPerChannel, 1 s) of data is
// unread; a streaming AO task buffers 1 s and underflows when it runs dry.
//
// Digital lines: an input line shows the external signal applied to it (setDigitalSource(); by
// default bit n of an 8-bit counter incrementing at 2 Hz, i.e. a 1/2^n Hz square wave), or the
// simulated quadrature encoder on DIO0 (A), DIO2 (B), DIO1 (Z) when enabled. Output lines read back
// the written level; DIO3 shows the ctr0 pulse train while a counter output task exists and DIO4
// the FREQOUT square wave while it runs. ctr0 measures these signals with exact edge times,
// quantized to its timebase ticks. A counter task or FREQOUT cannot use a line configured as a DIO
// output (ResourceBusy), and dioSetDirection() on a line they reserve throws ResourceBusy;
// dioWrite() leaves reserved lines alone like any non-output line. Buffered edge counting and
// buffered encoders are NotSupported (the myDAQ counter has no sample clock).
//
// The simulator registered by DeviceManager has id "sim:0"; reach its controls with
//   std::dynamic_pointer_cast<mydaq::SimDevice>(mydaq::DeviceManager::instance().device("sim:0"))
// All methods are thread-safe.
#pragma once

#include "mydaq/device.h"
#include "mydaq/dsp/waveform.h"

#include <memory>
#include <vector>

namespace mydaq {

namespace sim {
struct Core;
}

// Signal shown on an undriven input.
struct SimTestSignal {
    dsp::Waveform waveform = dsp::Waveform::Sine;
    double frequency = 1000.0;     // Hz
    double amplitude = 1.0;        // V peak (Noise: uniform in +-amplitude)
    double offset = 0.0;           // V; ignored on audio inputs (AC coupled)
    double dutyCycle = 0.5;        // Square only, 0..1
    std::vector<double> harmonics; // Sine only: harmonics[k] = amplitude of harmonic k+2 relative
                                   // to the fundamental (e.g. {0, 0.01} = 1 % third harmonic)
};

// External signal applied to a DIO line (seen by dioRead() while the line is an input, and by ctr0).
struct SimDigitalSource {
    enum class Kind {
        Floating,          // bit n of an 8-bit counter incrementing at 2 Hz (a 1/2^n Hz square wave)
        Clock,             // square wave: rising edges at delay + k / frequency
        Level,             // constant `high`
        FrequencyOutput,   // wired to FREQOUT (DIO4): follows it, low while it is stopped
    };
    Kind kind = Kind::Floating;
    double frequency = 1000.0;   // Clock: Hz
    double dutyCycle = 0.5;      // Clock: fraction of the period high, 0 < duty < 1
    double delay = 0.0;          // Clock: s; all clocks share one time origin, so equal-frequency
                                 // clocks with different delays have a fixed edge separation
    bool high = false;           // Level
};

// Quadrature encoder on DIO0 (A), DIO2 (B) and DIO1 (Z), overriding their sources while enabled.
// A leads B by 90 degrees when turning forward; Z is high once per revolution for the quarter
// cycle where A and B are both high.
struct SimEncoder {
    bool enabled = false;
    double speed = 100.0;             // cycles (pulses of A) per second; negative turns backwards
    int pulsesPerRevolution = 1024;
};

class SimDevice final : public Device {
public:
    explicit SimDevice(const QString& id = QStringLiteral("sim:0"),
                       const QString& name = QStringLiteral("Simulated myDAQ"));
    ~SimDevice() override;

    // Device
    DeviceInfo info() const override;
    bool isConnected() const override;
    double maxAiRate() const override;
    double maxAoRate() const override;
    double coerceAiRate(double requested) const override;
    double coerceAoRate(double requested) const override;
    std::unique_ptr<AiTask> createAiTask(const AiConfig& config) override;
    std::unique_ptr<AoTask> createAoTask(const AoConfig& config) override;
    double aiReadStatic(AiChannel channel, double range) override;
    void aoWriteStatic(AoChannel channel, double volts) override;
    std::vector<DmmRange> dmmRanges(DmmFunction function) const override;
    DmmReading dmmRead(DmmFunction function, double rangeMax) override;
    void dioSetDirection(uint8_t mask, bool output) override;
    uint8_t dioDirection() const override;
    void dioWrite(uint8_t value, uint8_t mask = 0xFF) override;
    uint8_t dioRead() override;
    std::unique_ptr<CounterInputTask> createCounterInputTask(const CounterInputConfig& config) override;
    std::unique_ptr<CounterOutputTask> createCounterOutputTask(const PulseOutputConfig& config) override;
    double coerceCounterFrequency(double requested) const override;
    double coerceFrequencyOutput(double requested) const override;
    double frequencyOutputStart(double frequency) override;
    void frequencyOutputStop() override;
    bool frequencyOutputRunning() const override;
    void reset() override;

    // --- Simulation controls -----------------------------------------------------------------

    // Simulates unplugging (false: running tasks stop, hardware calls throw NotConnected, and the
    // simulator provider stops listing the device) and plugging back in (outputs 0 V, DIO inputs).
    void setConnected(bool connected);

    // Test signals of undriven inputs. Defaults:
    //   AI 0: 1 kHz sine, 1 V peak, 1 % third harmonic.
    //   AI 1: 200 Hz square, +-0.5 V, 20 % duty cycle.
    //   AudioInput Left: 440 Hz sine, 0.5 V peak.   AudioInput Right: 1 kHz sine, 0.5 V peak.
    void setTestSignal(AiChannel channel, const SimTestSignal& signal);
    SimTestSignal testSignal(AiChannel channel) const;

    void setNoiseRms(double volts);        // AI noise, default 0.5e-3 V rms
    double noiseRms() const;
    void setRcCutoff(double hertz);        // AO 0 -> AI 1 low-pass corner, default 1000 Hz
    double rcCutoff() const;
    // AI0 -> AI1 conversion delay of new 2-channel AI tasks (reported and applied), default 2.5 us.
    void setChannelSkew(double seconds);
    double channelSkew() const;

    // Digital signals (defaults: every line Floating, encoder disabled).
    void setDigitalSource(int line, const SimDigitalSource& source);
    SimDigitalSource digitalSource(int line) const;
    void setEncoder(const SimEncoder& encoder);   // position stays continuous across changes
    SimEncoder encoder() const;
    double encoderPosition() const;               // cycles of A turned since the device was created

    // DMM input per function, base units (AC functions: RMS). Defaults: DC V 1.2345 V,
    // AC V 0.7071 V, DC A 12.34 mA, AC A 5.000 mA, resistance 4.7 kΩ, diode 0.650 V,
    // continuity 0.8 Ω. Readings add ~0.3 count of noise and are rounded to the range resolution
    // (3½ digits); |value| > range sets overload.
    void setDmmValue(DmmFunction function, double value);
    double dmmValue(DmmFunction function) const;
    // Open test leads: resistance, diode and continuity read overload.
    void setDmmOpenCircuit(bool open);
    bool dmmOpenCircuit() const;
    // Time per reading, default 100 ms (DC, resistance, diode, continuity) and 300 ms (AC).
    void setDmmReadTime(int dcMs, int acMs);

private:
    std::shared_ptr<sim::Core> d;
};

}  // namespace mydaq
