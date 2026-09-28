// LogicBuffer: thread-safe recording of digital lines (DIO 0..7, up to 64 lanes) as per-lane
// edge lists (the times at which each line toggles), for PlotLogicTrace.
//
// Storage is O(edges), not O(samples): a line that stays high for an hour costs nothing, and
// drawing at any zoom level costs O(pixels · log edges) (binary searches per pixel column).
//
//     // worker thread (software-timed DIO polling):
//     m_dio->append(t, mask);                 // state of all lines at time t; edges extracted
//     m_dio->setEnd(now);                     // extend the last state to "now"
//     // or uniformly sampled states (e.g. a logic capture):
//     m_dio->appendSamples(t0, 1.0 / rate, std::span<const quint8>(bytes));
//     // QML:
//     Plot { PlotLogicTrace { buffer: controller.dio; laneNames: ["DIO0", ...] } }
//
// Like TraceBuffer, readers take an immutable snapshot() and changed() is coalesced on the
// buffer's thread.
#pragma once

#include <QMutex>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

namespace mybench::plot {

// Edge times of one lane, chunked so appends never move data a snapshot can see.
struct LogicLane {
    static constexpr int kShift = 14;                    // 16384 edges per chunk
    static constexpr size_t kChunk = size_t(1) << kShift;
    std::vector<std::shared_ptr<double[]>> chunks;
    size_t firstChunk = 0;   // absolute chunk number of chunks[0]
    size_t begin = 0;        // absolute index of the first kept edge
    size_t end = 0;          // absolute index one past the last edge
    bool beginLevel = false; // level before edge `begin`

    double at(size_t absolute) const
    {
        return chunks[(absolute >> kShift) - firstChunk][absolute & (kChunk - 1)];
    }
};

class LogicData
{
public:
    int laneCount = 8;
    double startX = qQNaN();   // first time covered (NaN: empty)
    double endX = qQNaN();     // last time covered
    quint64 version = 0;
    std::vector<LogicLane> lanes;

    bool empty() const { return !(endX >= startX); }
    size_t edgeCount(int lane) const;
    // Index (0-based within the lane) of the first edge at time >= x / > x.
    size_t lowerBound(int lane, double x) const;
    size_t upperBound(int lane, double x) const;
    double edgeAt(int lane, size_t index) const;
    // Level just before edge `index` (index == edgeCount: the final level).
    bool levelBefore(int lane, size_t index) const;
    // Level at time x (after any edge exactly at x).
    bool levelAt(int lane, double x) const;
};

using LogicSnapshot = std::shared_ptr<const LogicData>;

class LogicBuffer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int laneCount READ laneCount WRITE setLaneCount NOTIFY laneCountChanged FINAL)
    Q_PROPERTY(quint64 version READ version NOTIFY changed FINAL)
    Q_PROPERTY(double maximumSpan READ maximumSpan WRITE setMaximumSpan NOTIFY maximumSpanChanged FINAL)

public:
    explicit LogicBuffer(QObject *parent = nullptr);
    ~LogicBuffer() override;

    // Number of lanes (bits of the state word), 1..64. Changing it clears the buffer.
    int laneCount() const { return m_laneCount.load(); }
    void setLaneCount(int count);

    // ---- thread-safe writers ------------------------------------------------------------------
    // Clears and starts a recording at time x with the given state (bit i = lane i).
    void reset(double x, quint64 state);
    // State of all lanes at time x (non-decreasing). Bits that changed become edges at x; the
    // first call after construction/clear() starts the recording.
    void append(double x, quint64 state);
    // Uniformly sampled states: sample k at x0 + k * dx.
    void appendSamples(double x0, double dx, std::span<const quint8> states);
    void appendSamples(double x0, double dx, std::span<const quint16> states);
    void appendSamples(double x0, double dx, std::span<const quint32> states);
    void appendSamples(double x0, double dx, std::span<const quint64> states);
    // Extends the current state up to x (e.g. the current time while no edge happens).
    void setEnd(double x);
    Q_INVOKABLE void clear();

    // Rolling window: history older than end - maximumSpan is dropped (0 = keep everything).
    double maximumSpan() const { return m_maximumSpan.load(); }
    void setMaximumSpan(double span);

    // ---- thread-safe readers ------------------------------------------------------------------
    LogicSnapshot snapshot() const;
    quint64 version() const { return m_version.load(std::memory_order_acquire); }

    // QML conveniences.
    Q_INVOKABLE bool levelAt(int lane, double x) const;
    Q_INVOKABLE double startX() const;
    Q_INVOKABLE double endX() const;

signals:
    void changed();
    void laneCountChanged();
    void maximumSpanChanged();

private:
    template <typename T>
    void appendSamplesT(double x0, double dx, std::span<const T> states);
    void appendLocked(double x, quint64 state);
    void pushEdgeLocked(int lane, double x);
    void trimLocked();
    void publishLocked();
    void scheduleNotify();

    mutable QMutex m_mutex;
    std::atomic<int> m_laneCount{8};
    std::atomic<double> m_maximumSpan{0.0};
    // Guarded by m_mutex:
    std::vector<LogicLane> m_lanes;
    bool m_started = false;
    quint64 m_state = 0;
    double m_startX = qQNaN();
    double m_endX = qQNaN();
    mutable LogicSnapshot m_cachedSnapshot;

    std::atomic<quint64> m_version{0};
    std::atomic<bool> m_notifyPending{false};
};

}  // namespace mybench::plot
