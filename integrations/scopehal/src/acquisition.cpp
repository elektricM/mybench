#include "acquisition.h"

#include "mydaq/dsp/trigger.h"

#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

using namespace Qt::StringLiterals;

namespace scopehal_bridge {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kIdlePoll = std::chrono::milliseconds(5);   // read period when no data is waiting
constexpr auto kRetryDelay = std::chrono::seconds(1);      // after a failed AI start / read

// Samples of AI0 and AI1 since the task started, addressed by absolute sample number.
class History {
public:
    void clear()
    {
        m_base = 0;
        for (auto& c : m_channels)
            c.clear();
    }
    void append(const std::vector<std::vector<double>>& data)
    {
        for (size_t c = 0; c < m_channels.size() && c < data.size(); ++c)
            m_channels[c].insert(m_channels[c].end(), data[c].begin(), data[c].end());
    }
    int64_t base() const { return m_base; }
    int64_t size() const { return int64_t(m_channels[0].size()); }
    int64_t end() const { return m_base + size(); }
    const std::vector<double>& channel(int c) const { return m_channels[size_t(c)]; }
    void dropBefore(int64_t index)
    {
        const int64_t n = std::clamp<int64_t>(index - m_base, 0, size());
        if (n == 0)
            return;
        for (auto& c : m_channels)
            c.erase(c.begin(), c.begin() + std::ptrdiff_t(n));
        m_base += n;
    }

private:
    int64_t m_base = 0;
    std::array<std::vector<double>, kChannelCount> m_channels;
};

// Edge search on the growing history with mydaq::dsp::findEdge. findEdge starts disarmed at
// `from`, so each slope resumes at the last sample that armed it (rising: below level minus
// hysteresis) instead of at the end of the data already searched; an edge armed in one chunk
// and crossing in the next is found. A signal parked inside the hysteresis band for longer than
// `maxDwell` samples loses its arming (bounds the rescan and the history kept).
class TriggerSearch {
public:
    void reset(int64_t at) { m_from = {at, at}; }

    // Earliest crossing at or after the resume points (absolute fractional sample index).
    std::optional<double> next(const History& h, const Settings& s, double hysteresis, int64_t maxDwell)
    {
        const std::vector<double>& x = h.channel(s.triggerChannel);
        std::optional<double> best;
        for (int k = 0; k < 2; ++k) {
            const bool rising = k == 0;
            if (s.slope == (rising ? EdgeSlope::Falling : EdgeSlope::Rising))
                continue;
            int64_t& from = m_from[size_t(k)];
            from = std::max(from, h.base());
            if (from >= h.end())
                continue;
            const mydaq::dsp::EdgeTrigger trigger{s.triggerLevel,
                                                  rising ? mydaq::dsp::Slope::Rising : mydaq::dsp::Slope::Falling,
                                                  hysteresis};
            if (const auto c = mydaq::dsp::findEdge(x, trigger, size_t(from - h.base()))) {
                const double crossing = double(h.base()) + *c;
                if (!best || crossing < *best)
                    best = crossing;
                continue;
            }
            const double armLevel = rising ? s.triggerLevel - hysteresis : s.triggerLevel + hysteresis;
            int64_t p = h.end() - 1;
            while (p >= from) {
                const double v = x[size_t(p - h.base())];
                if (rising ? v < armLevel : v > armLevel)
                    break;
                --p;
            }
            from = p >= from ? p : h.end();
            if (h.end() - from > maxDwell)
                from = h.end();
        }
        return best;
    }

    // Resume after a crossing (used or not): the detector must arm again.
    void skipPast(double crossing)
    {
        const int64_t after = int64_t(std::floor(crossing)) + 1;
        for (int64_t& f : m_from)
            f = std::max(f, after);
    }

    int64_t resumePoint() const { return std::min(m_from[0], m_from[1]); }

private:
    std::array<int64_t, 2> m_from{0, 0};   // rising, falling
};

std::unique_ptr<mydaq::AiTask> createTask(const mydaq::DevicePtr& device, const Settings& s)
{
    if (!device)
        throw mydaq::DeviceError(mydaq::DeviceError::NotConnected, u"no myDAQ connected"_s);
    mydaq::AiConfig config;
    // Both inputs always: the trigger source may be a channel that is not displayed, and the
    // myDAQ reaches its maximum rate per channel with both on.
    config.channels = {{mydaq::AiChannel::AI0, hardwareRange(s.channels[0])},
                       {mydaq::AiChannel::AI1, hardwareRange(s.channels[1])}};
    config.sampleRate = double(s.rate);
    config.mode = mydaq::SampleMode::Continuous;
    config.samplesPerChannel = std::max<int64_t>(16, int64_t(s.rate / 10));   // ~100 ms bursts
    auto task = device->createAiTask(config);
    task->start();
    return task;
}

QString rangeText(double range) { return range < 5 ? u"±2 V"_s : u"±10 V"_s; }

}  // namespace

double hardwareRange(const ChannelSettings& channel)
{
    const double reach = std::abs(channel.center) + std::abs(channel.span) / 2;
    return reach <= 2.0 + 1e-9 ? 2.0 : 10.0;
}

double triggerHysteresis(const ChannelSettings& channel) { return std::max(0.01 * std::abs(channel.span), 1e-3); }

int64_t clampTriggerDelay(int64_t delayFs, uint64_t rate, uint64_t depth)
{
    const double record = double(depth) * double(kFsPerSecond) / double(rate);
    const double limit = 9.0e18;   // int64 range
    const double clamped = std::clamp(double(delayFs), std::max(-10.0 * record, -limit), std::min(record, limit));
    return int64_t(clamped);
}

struct Acquisition::Worker {
    struct Pending {
        double crossing = 0;   // absolute fractional sample index of the trigger
        int64_t start = 0;     // first sample of the frame
    };
    std::unique_ptr<mydaq::AiTask> task;
    uint64_t taskGeneration = 0;
    uint64_t armGeneration = 0;
    History history;
    TriggerSearch search;
    std::optional<Pending> pending;
    int64_t fsPerSample = 0;
    double skewFs = 0;   // AI1 is sampled this long after AI0 within a scan
    Clock::time_point armedAt;
    Clock::time_point retryAt;
    QString lastError;
    // verbose statistics
    int framesSinceReport = 0;
    Clock::time_point reportAt;
};

Acquisition::Acquisition(DeviceSource device, FrameSink sink, Logger log, Options options)
    : m_device(std::move(device)), m_sink(std::move(sink)), m_log(std::move(log)), m_options(options)
{
    m_thread = std::thread([this] { run(); });
}

Acquisition::~Acquisition()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

uint64_t Acquisition::beginSession()
{
    std::lock_guard lock(m_mutex);
    m_settings = Settings{};
    m_mode = Mode::Stopped;
    m_forcePending = false;
    m_waitingDelivery = false;
    m_dataConnected = false;
    ++m_session;
    ++m_hwGeneration;
    ++m_armGeneration;
    m_wake.notify_all();
    return m_session;
}

void Acquisition::endSession()
{
    std::lock_guard lock(m_mutex);
    m_mode = Mode::Stopped;
    m_forcePending = false;
    m_waitingDelivery = false;
    m_dataConnected = false;
    ++m_session;   // frames still on their way belong to nobody
    m_wake.notify_all();
}

void Acquisition::setDataConnected(bool connected)
{
    std::lock_guard lock(m_mutex);
    m_dataConnected = connected;
    if (!connected)
        m_waitingDelivery = false;
    m_wake.notify_all();
}

void Acquisition::frameDelivered(uint64_t session)
{
    std::lock_guard lock(m_mutex);
    if (session != m_session || !m_waitingDelivery)
        return;
    m_waitingDelivery = false;
    ++m_armGeneration;   // re-arm: the next frame comes from a trigger after this point
    m_wake.notify_all();
}

Settings Acquisition::settings() const
{
    std::lock_guard lock(m_mutex);
    return m_settings;
}

void Acquisition::configure(const std::function<void(Settings&)>& change)
{
    std::lock_guard lock(m_mutex);
    const Settings before = m_settings;
    Settings& s = m_settings;
    change(s);
    s.rate = nearestRate(s.rate);
    s.depth = nearestDepth(s.depth);
    s.triggerChannel = std::clamp(s.triggerChannel, 0, kChannelCount - 1);
    for (ChannelSettings& ch : s.channels) {
        if (!(ch.attenuation > 0) || !std::isfinite(ch.attenuation))
            ch.attenuation = 1.0;
    }

    bool hardware = s.rate != before.rate;
    for (int c = 0; c < kChannelCount; ++c)
        hardware = hardware || hardwareRange(s.channels[size_t(c)]) != hardwareRange(before.channels[size_t(c)]);
    const bool trigger = s.depth != before.depth || s.triggerChannel != before.triggerChannel
                         || s.triggerLevel != before.triggerLevel || s.slope != before.slope
                         || s.triggerDelayFs != before.triggerDelayFs
                         || triggerHysteresis(s.channels[size_t(s.triggerChannel)])
                                != triggerHysteresis(before.channels[size_t(before.triggerChannel)]);
    if (hardware)
        ++m_hwGeneration;
    if (trigger)
        ++m_armGeneration;
    m_wake.notify_all();   // also for channel on/off: the worker may have to start or stop
}

void Acquisition::start()
{
    std::lock_guard lock(m_mutex);
    m_forcePending = false;
    if (m_mode == Mode::Normal)
        return;   // already running
    m_mode = Mode::Normal;
    ++m_armGeneration;
    m_wake.notify_all();
}

void Acquisition::single()
{
    std::lock_guard lock(m_mutex);
    m_forcePending = false;
    m_mode = Mode::Single;
    ++m_armGeneration;
    m_wake.notify_all();
}

void Acquisition::force()
{
    std::lock_guard lock(m_mutex);
    m_forcePending = true;
    if (m_mode == Mode::Stopped)
        m_mode = Mode::Single;   // runs the AI until the forced frame is out
    m_wake.notify_all();
}

void Acquisition::stop()
{
    std::lock_guard lock(m_mutex);
    m_mode = Mode::Stopped;
    m_forcePending = false;
    m_waitingDelivery = false;
    m_wake.notify_all();
}

bool Acquisition::isArmed() const
{
    std::lock_guard lock(m_mutex);
    return m_mode != Mode::Stopped;
}

bool Acquisition::wantTask() const
{
    const bool anyChannel = std::any_of(m_settings.channels.begin(), m_settings.channels.end(),
                                        [](const ChannelSettings& c) { return c.enabled; });
    return m_mode != Mode::Stopped && m_dataConnected && anyChannel;
}

void Acquisition::run()
{
    Worker w;
    std::unique_lock lock(m_mutex);
    while (!m_quit) {
        if (!wantTask()) {
            if (w.task) {
                auto task = std::move(w.task);
                lock.unlock();
                task.reset();
                lock.lock();
                m_log(u"analog input stopped"_s);
                continue;
            }
            m_wake.wait(lock);
            continue;
        }

        if (!w.task || w.taskGeneration != m_hwGeneration) {
            if (Clock::now() < w.retryAt) {
                m_wake.wait_until(lock, w.retryAt);
                continue;
            }
            const Settings s = m_settings;
            const uint64_t generation = m_hwGeneration;
            auto old = std::move(w.task);
            lock.unlock();
            old.reset();
            std::unique_ptr<mydaq::AiTask> task;
            QString error;
            try {
                task = createTask(m_device(), s);
            } catch (const std::exception& e) {
                error = QString::fromUtf8(e.what());
            }
            lock.lock();
            if (!task) {
                if (error != w.lastError)
                    m_log(u"cannot start the analog input: %1 (retrying every second)"_s.arg(error));
                w.lastError = error;
                w.retryAt = Clock::now() + kRetryDelay;
                continue;
            }
            w.lastError.clear();
            w.task = std::move(task);
            w.taskGeneration = generation;
            w.history.clear();
            w.pending.reset();
            w.search.reset(0);
            w.armGeneration = m_armGeneration;
            w.armedAt = Clock::now();
            w.fsPerSample = std::llround(double(kFsPerSecond) / w.task->sampleRate());
            w.skewFs = w.task->channelSkew() * double(kFsPerSecond);
            m_log(u"analog input running: AI0 %1, AI1 %2, %3 S/s per channel"_s.arg(
                rangeText(hardwareRange(s.channels[0])), rangeText(hardwareRange(s.channels[1])))
                      .arg(w.task->sampleRate()));
            continue;
        }

        lock.unlock();
        std::vector<std::vector<double>> data;
        QString error;
        bool overflow = false;
        try {
            data = w.task->read(-1, 0);
        } catch (const mydaq::DeviceError& e) {
            error = e.message();
            overflow = e.code() == mydaq::DeviceError::Overflow;
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        lock.lock();
        if (!error.isEmpty()) {
            if (overflow) {
                m_log(u"samples were lost (%1): restarting the analog input"_s.arg(error));
                w.taskGeneration = 0;
            } else {
                m_log(u"analog input failed: %1 (retrying every second)"_s.arg(error));
                w.lastError = error;
                auto task = std::move(w.task);
                lock.unlock();
                task.reset();
                lock.lock();
                w.retryAt = Clock::now() + kRetryDelay;
            }
            continue;
        }

        const bool got = data.size() == size_t(kChannelCount) && !data[0].empty();
        if (got)
            w.history.append(data);
        if (w.armGeneration != m_armGeneration) {
            w.armGeneration = m_armGeneration;
            w.search.reset(w.history.end());
            w.pending.reset();
            w.armedAt = Clock::now();
        }
        produce(w);
        trim(w);
        if (!got)
            m_wake.wait_for(lock, kIdlePoll);
    }
    lock.unlock();
    w.task.reset();
}

void Acquisition::produce(Worker& w)
{
    if (m_waitingDelivery || m_mode == Mode::Stopped)
        return;
    const Settings& s = m_settings;
    const History& h = w.history;
    const int64_t depth = int64_t(s.depth);

    if (m_forcePending) {
        if (h.size() >= depth) {
            send(w, h.end() - depth, 0.0);
            m_forcePending = false;
            m_mode = Mode::Stopped;
        }
        return;
    }

    const double period = double(w.fsPerSample);
    const int64_t delay = clampTriggerDelay(s.triggerDelayFs, s.rate, s.depth);
    const double delaySamples = double(delay) / period;
    if (!w.pending) {
        const double hysteresis = triggerHysteresis(s.channels[size_t(s.triggerChannel)]);
        const int64_t maxDwell = std::max<int64_t>(depth, int64_t(s.rate));
        while (const auto crossing = w.search.next(h, s, hysteresis, maxDwell)) {
            // First sample such that the crossing lies delaySamples (or up to one sample less)
            // after it: the trigger phase below is then in [0, one period).
            const int64_t start = h.base() + int64_t(std::ceil(*crossing - double(h.base()) - delaySamples));
            if (start < h.base()) {   // its pre-trigger samples were never acquired
                w.search.skipPast(*crossing);
                continue;
            }
            w.pending = Worker::Pending{*crossing, start};
            break;
        }
    }

    if (w.pending && h.end() >= w.pending->start + depth) {
        // Sample i of channel c is shown at i * period + phase_c, and the crossing (measured on
        // the trigger channel, sampled triggerChannel * skew after AI0) must land on the delay.
        const double offset = w.pending->crossing - double(w.pending->start);
        const double phase = double(delay) - offset * period - double(s.triggerChannel) * w.skewFs;
        send(w, w.pending->start, phase);
        w.search.skipPast(w.pending->crossing);
        w.pending.reset();
    } else if (!w.pending && m_mode == Mode::Normal && m_options.autoTriggerMs > 0 && h.size() >= depth
               && Clock::now() - w.armedAt >= std::chrono::milliseconds(m_options.autoTriggerMs)) {
        send(w, h.end() - depth, 0.0);   // auto: no trigger in time, show the latest record
    } else {
        return;
    }
    if (m_mode == Mode::Single)
        m_mode = Mode::Stopped;
    else
        m_waitingDelivery = true;
}

void Acquisition::send(Worker& w, int64_t start, double phaseFs)
{
    const Settings& s = m_settings;
    const History& h = w.history;
    std::vector<FrameChannel> channels;
    for (int c = 0; c < kChannelCount; ++c) {
        const ChannelSettings& ch = s.channels[size_t(c)];
        if (!ch.enabled)
            continue;
        FrameChannel fc;
        fc.index = c;
        fc.triggerPhaseFs = float(phaseFs + double(c) * w.skewFs);
        fc.samples = h.channel(c).data() + (start - h.base());
        fc.scale = ch.attenuation;
        channels.push_back(fc);
    }
    m_sink(m_session, encodeFrame(w.fsPerSample, s.depth, channels));

    if (m_options.verbose) {
        const auto now = Clock::now();
        ++w.framesSinceReport;
        if (now >= w.reportAt) {
            m_log(u"%1 frame(s) of %2 samples sent"_s.arg(w.framesSinceReport).arg(s.depth));
            w.framesSinceReport = 0;
            w.reportAt = now + std::chrono::seconds(1);
        }
    }
}

void Acquisition::trim(Worker& w)
{
    const Settings& s = m_settings;
    History& h = w.history;
    const int64_t depth = int64_t(s.depth);
    const double period = std::max(1.0, double(w.fsPerSample));
    const double delaySamples = double(clampTriggerDelay(s.triggerDelayFs, s.rate, s.depth)) / period;
    const int64_t pre = int64_t(std::ceil(std::max(0.0, delaySamples))) + 1;

    int64_t keep = h.end() - depth;   // FORCE and auto frames take the latest record
    if (m_waitingDelivery)
        keep = std::min(keep, h.end() - pre);   // the search restarts at the end once re-armed
    else if (w.pending)
        keep = std::min(keep, w.pending->start);
    else
        keep = std::min(keep, w.search.resumePoint() - pre);
    // Hard bound: never more than a record, the trigger delay and two seconds of data.
    const int64_t cap = depth + int64_t(std::ceil(std::abs(delaySamples))) + 2 * int64_t(s.rate) + 1;
    keep = std::max(keep, h.end() - cap);
    if (keep - h.base() > std::max<int64_t>(65536, h.size() / 2))
        w.history.dropBefore(keep);
}

}  // namespace scopehal_bridge
