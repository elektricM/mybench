// Simulated AI and AO tasks. Every member is guarded by Core::mutex; the *Locked methods expect the
// caller to hold it. A task registers itself as the Core's reservation on construction (the creator
// holds the mutex and checked it was free) and releases it on destruction.
#pragma once

#include "sim_core.h"

#include <memory>

namespace mydaq::sim {

class SimAiTask final : public AiTask {
public:
    SimAiTask(std::shared_ptr<Core> core, const AiConfig& config);   // Core::mutex held
    ~SimAiTask() override;

    const AiConfig& config() const override { return m_config; }
    double sampleRate() const override { return m_rate; }
    double channelSkew() const override { return m_skew; }
    void start() override;
    void stop() override;
    bool isRunning() const override;
    std::vector<std::vector<double>> read(int64_t n, int timeoutMs) override;
    int64_t available() const override;

    void stopLocked(double t);
    double oldestUnreadTime(double now) const;   // time of the next sample read() can return (now if none)

private:
    bool finite() const { return m_config.mode == SampleMode::Finite; }
    int64_t acquiredLocked(double now) const;    // samples per channel acquired since start()

    const std::shared_ptr<Core> m_core;
    const AiConfig m_config;
    const double m_rate;
    const double m_skew;
    const int64_t m_capacity;   // continuous: unread samples kept before Overflow
    bool m_started = false;     // an acquisition exists (running, finished or stopped)
    double m_start = 0.0;
    double m_stop = kForever;
    int64_t m_readPos = 0;
};

class SimAoTask final : public AoTask {
public:
    SimAoTask(std::shared_ptr<Core> core, const AoConfig& config);   // Core::mutex held
    ~SimAoTask() override;

    const AoConfig& config() const override { return m_config; }
    double sampleRate() const override { return m_rate; }
    void write(const std::vector<std::vector<double>>& data, int timeoutMs = 10000) override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    bool waitUntilDone(int timeoutMs) override;
    int64_t spaceAvailable() const override;

    void stopLocked(double t);

private:
    enum class Kind { Finite, Regenerate, Streaming };

    int64_t written() const { return m_streams.front()->size(); }
    double endTime() const { return m_start + double(written()) / m_rate; }   // non-looping runs
    int64_t generatedLocked(double now) const;
    void finishLocked(double t, bool underflow);   // a non-looping run ended at t
    void updateLocked(double now);                 // notices finished / underflowed runs
    void validate(const std::vector<std::vector<double>>& data) const;
    void newStreams();

    const std::shared_ptr<Core> m_core;
    const AoConfig m_config;
    const double m_rate;
    const Kind m_kind;
    const int64_t m_capacity;   // streaming: samples per channel buffered ahead of the output
    std::vector<std::shared_ptr<SampleStream>> m_streams;   // per channel
    bool m_running = false;
    bool m_underflow = false;
    double m_start = 0.0;
};

}  // namespace mydaq::sim
