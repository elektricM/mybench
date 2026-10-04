// Acquisition engine of the bridge: runs a continuous AI task on AI0 and AI1 while the client has
// armed the trigger, finds trigger events in software and turns them into waveform frames.
//
// Threading: one worker thread owns the AI task and the sample history. Every public method is
// thread-safe and returns at once; frames leave through the FrameSink, called on the worker
// thread. After a frame in normal (START) mode the engine re-arms only once frameDelivered() says
// the previous frame has been handed to the socket, so a slow client throttles the frame rate
// instead of piling up stale frames.
#pragma once

#include "protocol.h"

#include "mydaq/device.h"

#include <QByteArray>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace scopehal_bridge {

enum class EdgeSlope { Rising, Falling, Any };

struct ChannelSettings {
    bool enabled = false;      // :Cn:ON / :Cn:OFF (the client starts with all channels off)
    double span = 10.0;        // :Cn:RANGE, V full scale (peak to peak) of the client's window
    double center = 0.0;       // :Cn:OFFS, V at the middle of that window
    double attenuation = 1.0;  // :Cn:ATTEN, probe factor applied to the samples sent
};

struct Settings {
    uint64_t rate = sampleRates().front();   // S/s per channel
    uint64_t depth = sampleDepths().front(); // samples per channel and frame
    std::array<ChannelSettings, kChannelCount> channels;
    int triggerChannel = 0;
    double triggerLevel = 0.0;               // V at the input (the client removes the attenuation)
    EdgeSlope slope = EdgeSlope::Rising;
    int64_t triggerDelayFs = 0;              // from the first sample of the frame to the trigger
};

// AI range for a channel: +-2 V when the client's window fits in it, else +-10 V.
double hardwareRange(const ChannelSettings& channel);
// Trigger hysteresis: 1 % of the trigger channel's window, at least 1 mV.
double triggerHysteresis(const ChannelSettings& channel);
// TRIG:DELAY limits: up to one record after the trigger (the client clamps there too), down to
// ten records before it (negative: the frame starts after the trigger).
int64_t clampTriggerDelay(int64_t delayFs, uint64_t rate, uint64_t depth);

class Acquisition {
public:
    using DeviceSource = std::function<mydaq::DevicePtr()>;   // current device, nullptr if absent
    using FrameSink = std::function<void(uint64_t session, QByteArray frame)>;
    using Logger = std::function<void(const QString& message)>;

    struct Options {
        int autoTriggerMs = 0;   // > 0: in START mode, send an untriggered frame after this long
                                 // without a trigger (an "auto" mode); never in SINGLE mode
        bool verbose = false;    // log frame statistics
    };

    Acquisition(DeviceSource device, FrameSink sink, Logger log, Options options);
    ~Acquisition();   // stops the worker and releases the AI task

    Acquisition(const Acquisition&) = delete;
    Acquisition& operator=(const Acquisition&) = delete;

    // A new client: default settings, stopped. Returns the session id stamped on its frames.
    uint64_t beginSession();
    void endSession();   // stopped, AI task released, frames of the old session dropped

    void setDataConnected(bool connected);   // frames are produced only with a data connection
    void frameDelivered(uint64_t session);   // the last frame left the socket buffer

    Settings settings() const;
    void configure(const std::function<void(Settings&)>& change);

    void start();    // normal mode: a frame per trigger, re-armed after each one
    void single();   // one frame on the next trigger, then stopped
    void force();    // one frame of the latest samples now, then stopped
    void stop();
    bool isArmed() const;

private:
    enum class Mode { Stopped, Normal, Single };
    struct Worker;

    // Worker-thread helpers, called with m_mutex held.
    void run();
    bool wantTask() const;
    void produce(Worker& w);
    void send(Worker& w, int64_t start, double phaseFs);
    void trim(Worker& w);

    DeviceSource m_device;
    FrameSink m_sink;
    Logger m_log;
    const Options m_options;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    Settings m_settings;
    Mode m_mode = Mode::Stopped;
    bool m_forcePending = false;
    bool m_dataConnected = false;
    bool m_waitingDelivery = false;   // normal mode: a frame was sent, re-arm once delivered
    bool m_quit = false;
    uint64_t m_session = 0;
    uint64_t m_hwGeneration = 1;      // bumped when the AI task must be recreated (rate, ranges)
    uint64_t m_armGeneration = 1;     // bumped on (re-)arm and trigger changes: restart the search

    std::thread m_thread;
};

}  // namespace scopehal_bridge
