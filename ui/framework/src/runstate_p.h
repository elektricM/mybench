// Private: state shared between an InstrumentController run and its worker thread.
#pragma once

#include <mybench/worker.h>

#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace mybench {

class InstrumentController;

namespace detail {

struct RunState {
    InstrumentController* owner = nullptr;   // valid while the thread runs (the owner joins it)
    quint64 generation = 0;
    mydaq::DevicePtr device;
    std::stop_source stopSource;
    std::atomic<bool> continuous{true};
    std::atomic<qint64> samples{0};
    // Results of this run are stale (restart(), discardPendingResults(), shutdown): undelivered
    // post()/publish() callbacks are dropped.
    std::atomic<bool> discarded{false};

    // Runs that must end (and release the hardware) before this one touches the device:
    // instruments it preempted and its own previous run.
    std::vector<std::shared_ptr<RunState>> predecessors;

    // Coalescing publish(): latest pending callback per slot.
    std::mutex publishMutex;
    std::map<int, std::function<void()>> pending;

    std::thread thread;

    // Interruptible sleeps.
    std::mutex sleepMutex;
    std::condition_variable_any sleepCv;

    // Set once the loop returned and its tasks were destroyed.
    void markFinished()
    {
        {
            std::lock_guard lock(finishedMutex);
            finished = true;
        }
        finishedCv.notify_all();
    }
    bool isFinished()
    {
        std::lock_guard lock(finishedMutex);
        return finished;
    }
    // Waits until finished, `timeout` elapsed or `token` is stopped. Returns isFinished().
    bool waitFinished(std::chrono::milliseconds timeout, std::stop_token token = {})
    {
        std::unique_lock lock(finishedMutex);
        finishedCv.wait_for(lock, token, timeout, [this] { return finished; });
        return finished;
    }

private:
    std::mutex finishedMutex;
    std::condition_variable_any finishedCv;
    bool finished = false;
};

}  // namespace detail
}  // namespace mybench
