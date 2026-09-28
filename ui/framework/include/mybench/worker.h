// Worker-thread helpers for InstrumentController (see instruments/README.md).
//
// An instrument's acquisition/generation loop runs in a dedicated thread. The controller builds
// the loop on the UI thread (InstrumentController::createWorker(), capturing a snapshot of the
// settings by value) and the framework runs it with a WorkerContext:
//
//     return [cfg = snapshot(), live = m_live](mybench::WorkerContext& ctx) {
//         auto task = ctx.device().createAiTask(cfg.ai);         // throws mydaq::DeviceError
//         task->start();
//         while (!ctx.stopRequested()) {
//             if (auto changed = live->take()) { ... apply ... }   // live settings (Mailbox)
//             auto data = task->read(cfg.samples, 200);            // short timeouts: stays responsive
//             ctx.addSamples(cfg.samples);
//             ctx.publish([result = analyse(data)] { ... update UI-thread state ... });
//             if (ctx.once()) break;                               // Single
//         }
//     };   // tasks are destroyed here, before the framework releases the resources
//
// Rules: never touch QObject state of the controller from the loop (post/publish lambdas run on
// the UI thread and may); throw mydaq::DeviceError for device failures (the framework stops the
// run, shows the error, never crashes); use timeouts of at most a few hundred ms so Stop is quick.
#pragma once

#include <mydaq/device.h>

#include <QString>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>

namespace mybench {

namespace detail {
struct RunState;
}

// Latest-value mailbox between the UI thread and a worker: post() replaces any value not yet
// taken, take() returns (and clears) the most recent one. Thread-safe. Typical use: live
// settings changes while running (the controller posts a new snapshot from its setters, the
// worker takes it at its next safe point).
template <typename T>
class Mailbox
{
public:
    void post(T value)
    {
        std::lock_guard lock(m_mutex);
        m_value = std::move(value);
    }
    std::optional<T> take()
    {
        std::lock_guard lock(m_mutex);
        std::optional<T> value = std::move(m_value);
        m_value.reset();
        return value;
    }
    bool hasValue() const
    {
        std::lock_guard lock(m_mutex);
        return m_value.has_value();
    }
    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_value.reset();
    }

private:
    mutable std::mutex m_mutex;
    std::optional<T> m_value;
};

// What a worker loop gets. Every method is callable from the worker thread; the delivery
// methods (post, publish, setStatus, ...) queue work to the UI thread and are dropped once the
// controller is gone.
class WorkerContext
{
public:
    explicit WorkerContext(std::shared_ptr<detail::RunState> state);

    // Stop was requested (Stop button, pause by the arbiter, device change, app exit).
    bool stopRequested() const;
    std::stop_token stopToken() const;
    // Sleeps, waking up early on stop. Returns false if stop was requested.
    bool sleepFor(std::chrono::milliseconds duration) const;
    bool sleepMs(int milliseconds) const { return sleepFor(std::chrono::milliseconds(milliseconds)); }

    // The device selected when the run started (kept alive for the whole run).
    mydaq::Device& device() const;
    const mydaq::DevicePtr& devicePtr() const;

    // Run mode, live: Single pressed while running continuously turns continuous() false, so the
    // loop ends after the current acquisition (and Run while a Single is in progress turns it
    // back to continuous).
    bool continuous() const;
    bool once() const { return !continuous(); }

    // Runs `fn` on the UI thread, in order with every other post/publish of this run.
    void post(std::function<void()> fn) const;
    // Like post(), but coalescing: if an earlier fn for the same `slot` has not been delivered
    // yet it is replaced. Use it for display updates that may come faster than the UI draws.
    void publish(std::function<void()> fn, int slot = 0) const;

    // Conveniences (UI-thread setters of InstrumentController, delivered in order).
    void setStatus(const QString& text) const;         // run-bar status text
    void setTimedOut(bool timedOut) const;             // run-bar timeout LED
    void setSampleRate(double samplesPerSecond) const; // status bar
    void addSamples(qint64 samples) const;             // throughput accounting (cheap, atomic)
    void acquisitionDone() const;                      // acquisitionCount++ and hasData = true
    void warn(const QString& message) const;           // non-fatal: event log + toast, keeps running

private:
    std::shared_ptr<detail::RunState> d;
};

}  // namespace mybench
