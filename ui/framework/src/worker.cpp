#include "mybench/worker.h"

#include "mybench/instrumentcontroller.h"
#include "mybench/eventlog.h"
#include "mybench/notifications.h"
#include "runstate_p.h"

#include <QMetaObject>

namespace mybench {

WorkerContext::WorkerContext(std::shared_ptr<detail::RunState> state) : d(std::move(state)) {}

bool WorkerContext::stopRequested() const
{
    return d->stopSource.stop_requested();
}

std::stop_token WorkerContext::stopToken() const
{
    return d->stopSource.get_token();
}

bool WorkerContext::sleepFor(std::chrono::milliseconds duration) const
{
    std::unique_lock lock(d->sleepMutex);
    const std::stop_token token = d->stopSource.get_token();
    d->sleepCv.wait_for(lock, token, duration, [] { return false; });
    return !token.stop_requested();
}

mydaq::Device& WorkerContext::device() const
{
    return *d->device;
}

const mydaq::DevicePtr& WorkerContext::devicePtr() const
{
    return d->device;
}

bool WorkerContext::continuous() const
{
    return d->continuous.load(std::memory_order_relaxed);
}

void WorkerContext::post(std::function<void()> fn) const
{
    if (!fn)
        return;
    QMetaObject::invokeMethod(d->owner, std::move(fn), Qt::QueuedConnection);
}

void WorkerContext::publish(std::function<void()> fn, int slot) const
{
    if (!fn)
        return;
    bool schedule = false;
    {
        std::lock_guard lock(d->publishMutex);
        auto it = d->pending.find(slot);
        if (it == d->pending.end()) {
            d->pending.emplace(slot, std::move(fn));
            schedule = true;
        } else {
            it->second = std::move(fn);   // replaces the undelivered one; its event is pending
        }
    }
    if (!schedule)
        return;
    // The state is kept alive by the event: the callback may be delivered after the run ended.
    std::weak_ptr<detail::RunState> weak = d;
    QMetaObject::invokeMethod(d->owner, [weak, slot] {
        const std::shared_ptr<detail::RunState> state = weak.lock();
        if (!state)
            return;
        std::function<void()> latest;
        {
            std::lock_guard lock(state->publishMutex);
            auto it = state->pending.find(slot);
            if (it == state->pending.end())
                return;
            latest = std::move(it->second);
            state->pending.erase(it);
        }
        latest();
    }, Qt::QueuedConnection);
}

void WorkerContext::setStatus(const QString& text) const
{
    InstrumentController* owner = d->owner;
    post([owner, text] { owner->setStatusText(text); });
}

void WorkerContext::setTimedOut(bool timedOut) const
{
    InstrumentController* owner = d->owner;
    post([owner, timedOut] { owner->setTimedOut(timedOut); });
}

void WorkerContext::setSampleRate(double samplesPerSecond) const
{
    InstrumentController* owner = d->owner;
    post([owner, samplesPerSecond] { owner->setSampleRate(samplesPerSecond); });
}

void WorkerContext::addSamples(qint64 samples) const
{
    d->samples.fetch_add(samples, std::memory_order_relaxed);
}

void WorkerContext::acquisitionDone() const
{
    InstrumentController* owner = d->owner;
    post([owner] {
        owner->incrementAcquisitionCount();
        owner->setHasData(true);
    });
}

void WorkerContext::warn(const QString& message) const
{
    InstrumentController* owner = d->owner;
    post([owner, message] { owner->reportWarning(message); });
}

}  // namespace mybench
