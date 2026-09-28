#include "mybench/plot/logicbuffer.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <algorithm>

namespace mybench::plot {

// ---------------------------------------------------------------------------------------------
// LogicData
// ---------------------------------------------------------------------------------------------

size_t LogicData::edgeCount(int lane) const
{
    if (lane < 0 || size_t(lane) >= lanes.size())
        return 0;
    return lanes[size_t(lane)].end - lanes[size_t(lane)].begin;
}

double LogicData::edgeAt(int lane, size_t index) const
{
    const LogicLane &l = lanes[size_t(lane)];
    return l.at(l.begin + index);
}

size_t LogicData::lowerBound(int lane, double x) const
{
    const size_t n = edgeCount(lane);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (edgeAt(lane, mid) < x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

size_t LogicData::upperBound(int lane, double x) const
{
    const size_t n = edgeCount(lane);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (edgeAt(lane, mid) <= x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

bool LogicData::levelBefore(int lane, size_t index) const
{
    if (lane < 0 || size_t(lane) >= lanes.size())
        return false;
    return lanes[size_t(lane)].beginLevel != bool(index & 1);
}

bool LogicData::levelAt(int lane, double x) const
{
    return levelBefore(lane, upperBound(lane, x));
}

// ---------------------------------------------------------------------------------------------
// LogicBuffer
// ---------------------------------------------------------------------------------------------

LogicBuffer::LogicBuffer(QObject *parent) : QObject(parent)
{
    m_lanes.resize(size_t(m_laneCount.load()));
}

LogicBuffer::~LogicBuffer() = default;

void LogicBuffer::setLaneCount(int count)
{
    count = std::clamp(count, 1, 64);
    if (m_laneCount.load() == count)
        return;
    {
        QMutexLocker lock(&m_mutex);
        m_laneCount.store(count);
        m_lanes.assign(size_t(count), LogicLane());
        m_started = false;
        m_startX = m_endX = qQNaN();
        publishLocked();
    }
    emit laneCountChanged();
    scheduleNotify();
}

void LogicBuffer::publishLocked()
{
    m_version.store(m_version.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    m_cachedSnapshot.reset();
}

void LogicBuffer::scheduleNotify()
{
    if (m_notifyPending.exchange(true, std::memory_order_acq_rel))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_notifyPending.store(false, std::memory_order_release);
            emit changed();
        },
        Qt::QueuedConnection);
}

void LogicBuffer::reset(double x, quint64 state)
{
    {
        QMutexLocker lock(&m_mutex);
        m_lanes.assign(size_t(m_laneCount.load()), LogicLane());
        for (size_t i = 0; i < m_lanes.size(); ++i)
            m_lanes[i].beginLevel = (state >> i) & 1;
        m_started = std::isfinite(x);
        m_state = state;
        m_startX = m_endX = m_started ? x : qQNaN();
        publishLocked();
    }
    scheduleNotify();
}

void LogicBuffer::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_lanes.assign(size_t(m_laneCount.load()), LogicLane());
        m_started = false;
        m_state = 0;
        m_startX = m_endX = qQNaN();
        publishLocked();
    }
    scheduleNotify();
}

void LogicBuffer::pushEdgeLocked(int laneIndex, double x)
{
    LogicLane &lane = m_lanes[size_t(laneIndex)];
    const size_t idx = (lane.end >> LogicLane::kShift) - lane.firstChunk;
    if (idx == lane.chunks.size())
        lane.chunks.push_back(std::shared_ptr<double[]>(new double[LogicLane::kChunk]));
    lane.chunks[idx][lane.end & (LogicLane::kChunk - 1)] = x;
    ++lane.end;
}

void LogicBuffer::appendLocked(double x, quint64 state)
{
    if (!std::isfinite(x))
        return;
    if (!m_started) {
        m_started = true;
        m_state = state;
        m_startX = m_endX = x;
        for (size_t i = 0; i < m_lanes.size(); ++i)
            m_lanes[i].beginLevel = (state >> i) & 1;
        return;
    }
    if (x < m_endX)
        x = m_endX;  // time must not go backwards
    const quint64 changed = (state ^ m_state);
    if (changed) {
        const int lanes = int(m_lanes.size());
        for (int i = 0; i < lanes; ++i) {
            if ((changed >> i) & 1)
                pushEdgeLocked(i, x);
        }
        m_state = state;
    }
    m_endX = x;
}

void LogicBuffer::trimLocked()
{
    const double span = m_maximumSpan.load();
    if (!(span > 0) || !(m_endX - m_startX > span))
        return;
    const double cut = m_endX - span;
    m_startX = cut;
    for (LogicLane &lane : m_lanes) {
        // Drop edges before the cut, keeping the level right.
        size_t lo = lane.begin, hi = lane.end;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (lane.at(mid) < cut)
                lo = mid + 1;
            else
                hi = mid;
        }
        const size_t dropped = lo - lane.begin;
        if (dropped & 1)
            lane.beginLevel = !lane.beginLevel;
        lane.begin = lo;
        size_t drop = 0;
        while (drop < lane.chunks.size() && ((lane.firstChunk + drop + 1) << LogicLane::kShift) <= lane.begin)
            ++drop;
        if (drop) {
            lane.chunks.erase(lane.chunks.begin(), lane.chunks.begin() + qsizetype(drop));
            lane.firstChunk += drop;
        }
    }
}

void LogicBuffer::append(double x, quint64 state)
{
    {
        QMutexLocker lock(&m_mutex);
        appendLocked(x, state);
        trimLocked();
        publishLocked();
    }
    scheduleNotify();
}

template <typename T>
void LogicBuffer::appendSamplesT(double x0, double dx, std::span<const T> states)
{
    if (states.empty() || !std::isfinite(x0) || !(dx > 0))
        return;
    {
        QMutexLocker lock(&m_mutex);
        for (size_t k = 0; k < states.size(); ++k)
            appendLocked(x0 + dx * double(k), quint64(states[k]));
        // The last sample's state holds for one sample period.
        m_endX = std::max(m_endX, x0 + dx * double(states.size()));
        trimLocked();
        publishLocked();
    }
    scheduleNotify();
}

void LogicBuffer::appendSamples(double x0, double dx, std::span<const quint8> s) { appendSamplesT(x0, dx, s); }
void LogicBuffer::appendSamples(double x0, double dx, std::span<const quint16> s) { appendSamplesT(x0, dx, s); }
void LogicBuffer::appendSamples(double x0, double dx, std::span<const quint32> s) { appendSamplesT(x0, dx, s); }
void LogicBuffer::appendSamples(double x0, double dx, std::span<const quint64> s) { appendSamplesT(x0, dx, s); }

void LogicBuffer::setEnd(double x)
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_started || !(x > m_endX))
            return;
        m_endX = x;
        trimLocked();
        publishLocked();
    }
    scheduleNotify();
}

void LogicBuffer::setMaximumSpan(double span)
{
    span = std::max(0.0, std::isfinite(span) ? span : 0.0);
    if (m_maximumSpan.exchange(span) == span)
        return;
    {
        QMutexLocker lock(&m_mutex);
        trimLocked();
        publishLocked();
    }
    emit maximumSpanChanged();
    scheduleNotify();
}

LogicSnapshot LogicBuffer::snapshot() const
{
    QMutexLocker lock(&m_mutex);
    if (!m_cachedSnapshot) {
        auto d = std::make_shared<LogicData>();
        d->laneCount = int(m_lanes.size());
        d->startX = m_startX;
        d->endX = m_endX;
        d->version = m_version.load(std::memory_order_relaxed);
        d->lanes = m_lanes;
        m_cachedSnapshot = std::move(d);
    }
    return m_cachedSnapshot;
}

bool LogicBuffer::levelAt(int lane, double x) const
{
    return snapshot()->levelAt(lane, x);
}

double LogicBuffer::startX() const
{
    return snapshot()->startX;
}

double LogicBuffer::endX() const
{
    return snapshot()->endX;
}

}  // namespace mybench::plot
