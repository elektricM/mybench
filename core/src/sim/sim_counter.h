// Simulated ctr0 tasks (counter input and pulse-train output). Every member is guarded by
// Core::mutex; the *Locked methods expect the caller to hold it. A task registers itself as the
// Core's counter reservation on construction (the creator holds the mutex and checked it was free)
// and releases it on destruction.
#pragma once

#include "sim_core.h"

#include <deque>
#include <memory>

namespace mydaq::sim {

// Validation and coercion, throwing InvalidConfig / NotSupported.
void validateCounterInput(const CounterInputConfig& config);
void validatePulseOutput(const PulseOutputConfig& config);
uint8_t counterInputLines(const CounterInputConfig& config);   // DIO lines the measurement uses
// Fastest of 100 MHz, 20 MHz, 100 kHz whose 32-bit count holds the longest expected interval.
double counterInputTimebase(const CounterInputConfig& config);
double coercePulseFrequency(double requested);                 // 100 MHz / n, n >= 4
double coercePulseDuty(double frequency, double duty);         // high and low >= 2 ticks
double coerceFreqOut(double requested);                        // {20 MHz, 10 MHz, 100 kHz} / (1..16)

class SimCounterInputTask final : public CounterInputTask {
public:
    SimCounterInputTask(std::shared_ptr<Core> core, const CounterInputConfig& config);   // Core::mutex held
    ~SimCounterInputTask() override;

    const CounterInputConfig& config() const override { return m_config; }
    uint8_t dioLinesUsed() const override { return m_lines; }
    void start() override;
    void stop() override;
    bool isRunning() const override;
    double readScalar(int timeoutMs) override;
    std::vector<double> readBuffered(int64_t n, int timeoutMs) override;

    void stopLocked(double t);
    double oldestNeededTime(double now) const;   // signal history the task may still read

private:
    // A timed measurement is an interval from a start edge to an end edge (see intervalSpec()).
    enum class Polarity { Rising, Falling, Any };
    struct IntervalSpec {
        int startLine = 1;
        Polarity startPolarity = Polarity::Rising;
        int endLine = 1;
        Polarity endPolarity = Polarity::Rising;
        bool chained = true;   // the end edge also starts the next interval
    };
    // Forward scan state of the buffered measurement (edges up to `cursor` are consumed).
    struct Scanner {
        double cursor = 0.0;
        double pendingStart = 0.0;
        bool hasStart = false;
    };

    bool countsEvents() const;   // CountEdges and encoders (vs timed intervals)
    IntervalSpec intervalSpec() const;
    DigitalChange nextOf(int line, double t, Polarity p) const;
    DigitalChange previousOf(int line, double t, Polarity p) const;
    double valueOf(double from, double to) const;   // interval -> measurement unit, tick-quantized
    bool latestInterval(double t, double& from, double& to) const;
    double firstIntervalEnd() const;
    // Appends up to `limit` intervals completed by `until`; returns the end time of the last one.
    double scan(Scanner& scanner, double until, size_t limit, std::deque<double>* values) const;
    void produceLocked(double until) const;      // fills the buffered queue
    double countValueLocked(double until) const; // CountEdges / encoder value at `until`

    const std::shared_ptr<Core> m_core;
    const CounterInputConfig m_config;
    const uint8_t m_lines;
    const double m_timebase;
    const int64_t m_capacity;   // buffered continuous: values kept before Overflow
    bool m_started = false;
    double m_start = 0.0;
    double m_stop = kForever;

    // Results are computed lazily from the simulated signals when asked for.
    mutable Scanner m_scanner;
    mutable std::deque<double> m_queue;   // buffered values not read yet
    mutable int64_t m_produced = 0;       // buffered values produced since start()
    mutable double m_eventCursor = 0.0;   // event counting: changes up to here are counted
    mutable int64_t m_count = 0;
    mutable bool m_a = false, m_b = false, m_z = false, m_zCondition = false;
};

class SimCounterOutputTask final : public CounterOutputTask {
public:
    SimCounterOutputTask(std::shared_ptr<Core> core, const PulseOutputConfig& config);   // Core::mutex held
    ~SimCounterOutputTask() override;

    const PulseOutputConfig& config() const override { return m_config; }
    double actualFrequency() const override;
    double actualDutyCycle() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    void update(double frequency, double dutyCycle) override;
    bool waitUntilDone(int timeoutMs) override;

    void stopLocked(double t);

private:
    bool finite() const { return m_config.mode == SampleMode::Finite; }
    void schedulePulses(double t);   // square wave of the current frequency/duty from t

    const std::shared_ptr<Core> m_core;
    PulseOutputConfig m_config;
    double m_frequency;         // coerced
    double m_duty;              // coerced
    bool m_running = false;
    double m_firstPulse = 0.0;  // start of the pulses (after the initial delay)
    double m_end = kForever;    // Finite: end of the last pulse
    double m_segmentBegin = 0.0;   // current square segment: period boundaries at begin + k / m_frequency
};

}  // namespace mydaq::sim
