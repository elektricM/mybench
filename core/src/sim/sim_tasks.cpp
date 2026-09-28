#include "sim_tasks.h"

#include <algorithm>
#include <cmath>

namespace mydaq::sim {

namespace {

using Clock = Core::Clock;

// Sample clock ticks elapsed in `seconds` (small tolerance for rounding of computed wake-ups).
int64_t ticksIn(double seconds, double rate)
{
    const double x = seconds * rate;
    return x <= 0.0 ? 0 : int64_t(std::floor(x + 1e-9));
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// AI

SimAiTask::SimAiTask(std::shared_ptr<Core> core, const AiConfig& config)
    : m_core(std::move(core)),
      m_config(config),
      m_rate(coerceRate(config.sampleRate)),
      m_skew(config.channels.size() > 1 ? m_core->channelSkew : 0.0),
      m_capacity(std::max<int64_t>(config.samplesPerChannel, int64_t(std::ceil(m_rate))))
{
    m_core->aiTask = this;
}

SimAiTask::~SimAiTask()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
    if (m_core->aiTask == this)
        m_core->aiTask = nullptr;
}

int64_t SimAiTask::acquiredLocked(double now) const
{
    if (!m_started)
        return 0;
    const int64_t n = ticksIn(std::min(now, m_stop) - m_start, m_rate);
    return finite() ? std::min(n, m_config.samplesPerChannel) : n;
}

double SimAiTask::oldestUnreadTime(double now) const
{
    if (!m_started)
        return now;
    const int64_t acquired = acquiredLocked(now);
    const bool acquiring = now < m_stop && !(finite() && acquired >= m_config.samplesPerChannel);
    if (!acquiring && m_readPos >= acquired)
        return now;   // nothing left to read
    return m_start + double(m_readPos) / m_rate;
}

void SimAiTask::stopLocked(double t)
{
    if (m_started && t < m_stop) {
        m_stop = std::max(t, m_start);
        m_core->changed.notify_all();
    }
}

void SimAiTask::start()
{
    std::lock_guard lock(m_core->mutex);
    m_core->requireConnected();
    m_started = true;
    m_start = m_core->now();
    m_stop = kForever;
    m_readPos = 0;
    m_core->changed.notify_all();
}

void SimAiTask::stop()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
}

bool SimAiTask::isRunning() const
{
    std::lock_guard lock(m_core->mutex);
    if (!m_core->connected || !m_started)
        return false;
    const double now = m_core->now();
    if (now >= m_stop)
        return false;
    return !finite() || acquiredLocked(now) < m_config.samplesPerChannel;
}

int64_t SimAiTask::available() const
{
    std::lock_guard lock(m_core->mutex);
    if (!m_core->connected)
        return 0;
    const int64_t backlog = acquiredLocked(m_core->now()) - m_readPos;
    return finite() ? backlog : std::min(backlog, m_capacity);
}

std::vector<std::vector<double>> SimAiTask::read(int64_t n, int timeoutMs)
{
    std::unique_lock lock(m_core->mutex);
    m_core->requireConnected();
    const size_t channels = m_config.channels.size();
    if (n == 0 || (n < 0 && !m_started))
        return std::vector<std::vector<double>>(channels);
    if (!m_started)
        throw DeviceError(DeviceError::Generic, QStringLiteral("The AI task is not started"));
    if (finite() && n > m_config.samplesPerChannel - m_readPos)
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("Cannot read %1 samples: %2 remain in this acquisition")
                              .arg(n)
                              .arg(m_config.samplesPerChannel - m_readPos));
    if (!finite() && n > m_capacity)
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("Cannot read %1 samples at once: the AI buffer holds %2")
                              .arg(n)
                              .arg(m_capacity));

    const Clock::time_point deadline = deadlineFor(timeoutMs);
    int64_t count = 0;
    for (;;) {
        const double now = m_core->now();
        const int64_t backlog = acquiredLocked(now) - m_readPos;
        if (!finite() && backlog > m_capacity) {
            stopLocked(now);
            m_started = false;
            throw DeviceError(DeviceError::Overflow,
                              QStringLiteral("AI buffer overflow: samples were not read fast enough, %1 lost")
                                  .arg(backlog - m_capacity));
        }
        if (n < 0 || backlog >= n) {
            count = n < 0 ? backlog : n;
            break;
        }
        if (now >= m_stop)
            throw DeviceError(DeviceError::Generic,
                              QStringLiteral("The AI task stopped before %1 samples were acquired").arg(n));
        if (Clock::now() >= deadline)
            throw DeviceError(DeviceError::Timeout,
                              QStringLiteral("Timed out waiting for %1 AI samples (%2 acquired)").arg(n).arg(backlog));
        const Clock::time_point ready = m_core->timePoint(m_start + double(m_readPos + n) / m_rate + kWakeSlack);
        m_core->changed.wait_until(lock, std::min(ready, deadline));
        m_core->requireConnected();
    }

    std::vector<std::vector<double>> data(channels, std::vector<double>(size_t(count)));
    for (int64_t i = 0; i < count; ++i) {
        const double t = m_start + double(m_readPos + i) / m_rate;
        for (size_t c = 0; c < channels; ++c) {
            const AiChannelConfig& ch = m_config.channels[c];
            data[c][size_t(i)] = m_core->sampleAi(ch.channel, ch.range, t + double(c) * m_skew);
        }
    }
    m_readPos += count;
    m_core->prune();
    return data;
}

// ---------------------------------------------------------------------------------------------
// AO

SimAoTask::SimAoTask(std::shared_ptr<Core> core, const AoConfig& config)
    : m_core(std::move(core)),
      m_config(config),
      m_rate(coerceRate(config.sampleRate)),
      m_kind(config.mode == SampleMode::Finite ? Kind::Finite
             : config.regenerate               ? Kind::Regenerate
                                               : Kind::Streaming),
      m_capacity(std::max<int64_t>(1, int64_t(std::ceil(m_rate))))
{
    newStreams();
    m_core->aoTask = this;
}

SimAoTask::~SimAoTask()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
    if (m_core->aoTask == this)
        m_core->aoTask = nullptr;
}

void SimAoTask::newStreams()
{
    m_streams.clear();
    for (size_t c = 0; c < m_config.channels.size(); ++c)
        m_streams.push_back(std::make_shared<SampleStream>());
}

int64_t SimAoTask::generatedLocked(double now) const
{
    return std::min(written(), ticksIn(now - m_start, m_rate));
}

void SimAoTask::finishLocked(double t, bool underflow)
{
    for (const AoChannelConfig& ch : m_config.channels)
        m_core->outputs[index(ch.channel)].stop(t);
    m_running = false;
    m_underflow = underflow;
    if (m_kind == Kind::Streaming)
        newStreams();
    m_core->changed.notify_all();
}

void SimAoTask::updateLocked(double now)
{
    if (m_running && m_kind != Kind::Regenerate && now >= endTime())
        finishLocked(endTime(), m_kind == Kind::Streaming);
}

void SimAoTask::stopLocked(double t)
{
    if (m_running)
        finishLocked(m_kind == Kind::Regenerate ? t : std::min(t, endTime()), false);
}

void SimAoTask::validate(const std::vector<std::vector<double>>& data) const
{
    if (data.size() != m_config.channels.size())
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Expected AO data for %1 channel(s), got %2")
                                                          .arg(m_config.channels.size())
                                                          .arg(data.size()));
    const size_t length = data.front().size();
    if (length == 0)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("AO data is empty"));
    for (size_t c = 0; c < data.size(); ++c) {
        if (data[c].size() != length)
            throw DeviceError(DeviceError::InvalidConfig,
                              QStringLiteral("All AO channels need the same number of samples"));
        const AoChannelConfig& ch = m_config.channels[c];
        for (double v : data[c])
            validateAoValue(ch.channel, ch.range, v);
    }
}

void SimAoTask::write(const std::vector<std::vector<double>>& data, int timeoutMs)
{
    std::unique_lock lock(m_core->mutex);
    m_core->requireConnected();
    validate(data);
    std::vector<std::vector<double>> codes = data;   // what the DAC outputs
    for (size_t c = 0; c < codes.size(); ++c)
        for (double& v : codes[c])
            v = quantizeAo(v, m_config.channels[c].range);
    updateLocked(m_core->now());
    if (m_underflow)
        throw DeviceError(DeviceError::Underflow,
                          QStringLiteral("AO buffer ran dry: samples were not written fast enough"));
    const size_t length = data.front().size();

    if (m_kind != Kind::Streaming) {
        if (m_running)
            throw DeviceError(DeviceError::NotSupported,
                              QStringLiteral("Cannot write while the AO task runs: stop, write, then start"));
        m_streams.clear();
        for (std::vector<double>& samples : codes)
            m_streams.push_back(std::make_shared<SampleStream>(std::move(samples)));
        return;
    }
    if (!m_running) {   // pre-fill: no limit, nothing is consumed yet
        for (size_t c = 0; c < codes.size(); ++c)
            m_streams[c]->append(codes[c].data(), length);
        return;
    }

    const Clock::time_point deadline = deadlineFor(timeoutMs);
    size_t done = 0;
    for (;;) {
        const double now = m_core->now();
        updateLocked(now);
        if (m_underflow)
            throw DeviceError(DeviceError::Underflow,
                              QStringLiteral("AO buffer ran dry: samples were not written fast enough"));
        if (!m_running)
            throw DeviceError(DeviceError::Generic, QStringLiteral("The AO task stopped during a write"));
        const int64_t pending = written() - generatedLocked(now);
        const size_t chunk = std::min(size_t(std::max<int64_t>(0, m_capacity - pending)), length - done);
        for (size_t c = 0; c < codes.size(); ++c)
            m_streams[c]->append(codes[c].data() + done, chunk);
        done += chunk;
        if (done == length)
            break;
        if (Clock::now() >= deadline)
            throw DeviceError(DeviceError::Timeout,
                              QStringLiteral("AO write timed out: %1 of %2 samples written").arg(done).arg(length));
        // Wake when enough of the buffer has been generated for the rest (or a full buffer).
        const int64_t needed = std::min<int64_t>(int64_t(length - done), m_capacity);
        const Clock::time_point ready =
            m_core->timePoint(m_start + double(written() - m_capacity + needed) / m_rate + kWakeSlack);
        m_core->changed.wait_until(lock, std::min(ready, deadline));
        m_core->requireConnected();
    }
    m_core->prune();
}

void SimAoTask::start()
{
    std::lock_guard lock(m_core->mutex);
    m_core->requireConnected();
    const double now = m_core->now();
    updateLocked(now);
    stopLocked(now);
    if (written() == 0)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Write samples before starting the AO task"));
    m_start = now;
    m_running = true;
    m_underflow = false;
    for (size_t c = 0; c < m_streams.size(); ++c)
        m_core->outputs[index(m_config.channels[c].channel)].play(now, m_rate, m_streams[c],
                                                                  m_kind == Kind::Regenerate,
                                                                  m_kind == Kind::Streaming);
    m_core->changed.notify_all();
    m_core->prune();
}

void SimAoTask::stop()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
    m_underflow = false;
}

bool SimAoTask::isRunning() const
{
    std::lock_guard lock(m_core->mutex);
    if (!m_core->connected || !m_running)
        return false;
    return m_kind == Kind::Regenerate || m_core->now() < endTime();
}

bool SimAoTask::waitUntilDone(int timeoutMs)
{
    std::unique_lock lock(m_core->mutex);
    m_core->requireConnected();
    const Clock::time_point deadline = deadlineFor(timeoutMs);
    for (;;) {
        const double now = m_core->now();
        // A stream that was fully generated ends normally here (not an underflow).
        if (m_running && m_kind == Kind::Streaming && now >= endTime())
            finishLocked(endTime(), false);
        updateLocked(now);
        if (m_underflow)
            throw DeviceError(DeviceError::Underflow,
                              QStringLiteral("AO buffer ran dry: samples were not written fast enough"));
        if (!m_running)
            return true;
        if (Clock::now() >= deadline)
            return false;
        Clock::time_point wake = deadline;
        if (m_kind != Kind::Regenerate)
            wake = std::min(wake, m_core->timePoint(endTime() + kWakeSlack));
        m_core->changed.wait_until(lock, wake);
        m_core->requireConnected();
    }
}

int64_t SimAoTask::spaceAvailable() const
{
    std::lock_guard lock(m_core->mutex);
    if (m_kind != Kind::Streaming)
        return 0;
    const int64_t pending = m_running ? written() - generatedLocked(m_core->now()) : written();
    return std::max<int64_t>(0, m_capacity - pending);
}

}  // namespace mydaq::sim
