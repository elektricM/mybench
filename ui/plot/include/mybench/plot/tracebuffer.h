// TraceBuffer: thread-safe, versioned sample storage feeding PlotTrace (and anything else that
// draws samples). Controllers own TraceBuffer objects and expose them as QObject* properties;
// worker threads call the setters directly (no QML arrays, no copies through the event loop).
//
//     // controller.h
//     Q_PROPERTY(mybench::plot::TraceBuffer *ch0 READ ch0 CONSTANT)
//     // worker thread, per acquired record:
//     m_ch0->setUniform(t0, 1.0 / rate, samples);          // std::span<const double|float>
//     // QML:
//     Plot { PlotTrace { buffer: controller.ch0; color: "#39d98a" } }
//
// Forms:
//   uniform:  x = x0 + i * dx      setUniform() replaces, appendSamples() records (continues x)
//   explicit: x[i], y[i]           setXY() replaces, append() records (trends, Bode, XY mode)
// Explicit x should be non-decreasing; unsorted data (XY mode) is detected and drawn as a path.
//
// Recording: appendSamples()/append() add to the end in amortized O(1) without copying earlier
// data, so a buffer can hold hours of samples (10^8+; 8 bytes per sample, +8 with explicit x).
// With maximumCount > 0 the oldest samples are dropped (rolling window), else it grows.
//
// Storage is chunked (65536 samples) with a min/max pyramid per chunk (levels of 16x
// reduction, updated incrementally on append), so the min/max of any index range costs
// O(log16 n) and a plot renders any zoom level in O(pixels), independent of the length.
//
// Every change increments version(); readers take an immutable snapshot() (lock held only to
// copy the chunk list; appends never modify samples a snapshot can see). changed() is emitted on
// the buffer's own thread, coalesced: many updates from a worker between two GUI event-loop
// iterations produce one signal.
#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace mybench::plot {

// One storage chunk: up to kChunkSize samples plus their min/max pyramid.
struct TraceChunk {
    static constexpr int kShift = 16;                  // 65536 samples per chunk
    static constexpr size_t kSize = size_t(1) << kShift;
    static constexpr int kLevels = 4;                  // bucket sizes 16, 256, 4096, 65536
    static constexpr int kFanShift = 4;

    TraceChunk(size_t capacity, bool explicitX);
    size_t capacity = 0;                               // allocated samples (<= kSize)
    std::unique_ptr<double[]> y;
    std::unique_ptr<double[]> x;                       // explicit x only
    std::array<std::unique_ptr<float[]>, kLevels> mn;  // level l: bucket 16^(l+1)
    std::array<std::unique_ptr<float[]>, kLevels> mx;

    static size_t bucketShift(int level) { return size_t(kFanShift * (level + 1)); }
    // Recomputes the pyramid entries covering samples [from, to) (samples [0, to) written).
    void updatePyramid(size_t from, size_t to);
    // Min/max of samples [from, to) using the pyramid (all of [from, to) written).
    void rangeMinMax(size_t from, size_t to, double &mn, double &mx) const;
};

// Immutable view of the samples (as seen through a snapshot). Indices are 0..size()-1.
class TraceData
{
public:
    bool uniform = true;
    bool sorted = true;          // explicit x is non-decreasing (always true for uniform)
    double x0 = 0.0;             // uniform: x of the first sample ever recorded (absolute 0)
    double dx = 1.0;             // uniform: x step (> 0)
    quint64 version = 0;

    size_t size() const { return m_end - m_begin; }
    bool empty() const { return m_end == m_begin; }
    double xAt(size_t i) const
    {
        const size_t a = m_begin + i;
        if (uniform)
            return x0 + dx * double(a);
        return chunkFor(a).x[a & (TraceChunk::kSize - 1)];
    }
    double yAt(size_t i) const
    {
        const size_t a = m_begin + i;
        return chunkFor(a).y[a & (TraceChunk::kSize - 1)];
    }

    // Index of the first sample with x >= value / x > value (size() if none). Sorted data.
    size_t lowerBound(double value) const;
    size_t upperBound(double value) const;
    // Finite min/max of y over samples [i, j). False if there is none. O(log n) via the pyramid.
    bool rangeMinMax(size_t i, size_t j, double *yMin, double *yMax) const;
    // Linear interpolation of y at x (sorted data). NaN outside the data or when empty.
    double interpolate(double x) const;
    // Index of the sample whose x is nearest to `x`; size() when empty.
    size_t nearest(double x) const;
    double xFirst() const { return empty() ? qQNaN() : xAt(0); }
    double xLast() const { return empty() ? qQNaN() : xAt(size() - 1); }
    // Finite y range over samples with x in [xMin, xMax] (sorted) or all samples (unsorted).
    bool yRange(double xMin, double xMax, double *yMin, double *yMax) const;

    // Construction (TraceBuffer).
    size_t m_begin = 0;          // absolute index of sample 0
    size_t m_end = 0;            // absolute index one past the last sample
    size_t m_firstChunk = 0;     // absolute chunk number of m_chunks[0]
    std::vector<std::shared_ptr<TraceChunk>> m_chunks;

private:
    const TraceChunk &chunkFor(size_t absolute) const
    {
        return *m_chunks[(absolute >> TraceChunk::kShift) - m_firstChunk];
    }
};

using TraceSnapshot = std::shared_ptr<const TraceData>;

class TraceBuffer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qsizetype count READ count NOTIFY changed FINAL)
    Q_PROPERTY(quint64 version READ version NOTIFY changed FINAL)
    Q_PROPERTY(bool uniform READ isUniform NOTIFY changed FINAL)
    Q_PROPERTY(qsizetype maximumCount READ maximumCount WRITE setMaximumCount NOTIFY maximumCountChanged FINAL)

public:
    explicit TraceBuffer(QObject *parent = nullptr);
    ~TraceBuffer() override;

    // ---- replace: thread-safe, callable from any thread ---------------------------------------
    void setUniform(double x0, double dx, std::span<const double> y);
    void setUniform(double x0, double dx, std::span<const float> y);
    // Explicit x/y (sizes must match; the shorter length is used otherwise).
    void setXY(std::span<const double> x, std::span<const double> y);
    void setXY(std::span<const float> x, std::span<const float> y);
    Q_INVOKABLE void clear();
    // QML convenience for small previews: setUniform() from a list of numbers.
    Q_INVOKABLE void setUniformValues(double x0, double dx, const QList<double> &y);

    // ---- record: thread-safe, amortized O(1) per sample -----------------------------------------
    // Clears and starts a uniform recording: sample k (from 0) is at x0 + k * dx.
    void startUniform(double x0, double dx);
    // Appends uniform samples (continuing x). On an explicit buffer this is ignored (warning).
    void appendSamples(std::span<const double> y);
    void appendSamples(std::span<const float> y);
    // Appends explicit points. A non-empty uniform buffer is converted to explicit first (O(n)).
    void append(double x, double y);
    void append(std::span<const double> x, std::span<const double> y);

    // ---- read: thread-safe --------------------------------------------------------------------
    TraceSnapshot snapshot() const;
    qsizetype count() const;
    quint64 version() const { return m_version.load(std::memory_order_acquire); }
    bool isUniform() const;

    // Rolling window for recordings: keep at most this many samples (0 = unlimited).
    qsizetype maximumCount() const { return m_maximumCount.load(); }
    void setMaximumCount(qsizetype count);

    // QML conveniences (take a snapshot; for readouts, not for bulk data).
    Q_INVOKABLE double valueAt(double x) const;         // interpolated y (NaN outside)
    Q_INVOKABLE QPointF nearestSample(double x) const;  // (x, y) of the nearest sample
    Q_INVOKABLE double xMinimum() const;                // x of the first sample
    Q_INVOKABLE double xMaximum() const;                // x of the last sample

signals:
    // Emitted on this object's thread after the data changed (coalesced).
    void changed();
    void maximumCountChanged();

private:
    struct State;
    void replace(std::unique_ptr<State> state);
    void appendLocked(const double *x, const double *y, size_t n);
    void convertToExplicitLocked();
    void trimLocked();
    void publishLocked();
    void scheduleNotify();

    mutable QMutex m_mutex;
    std::unique_ptr<State> m_state;                      // guarded by m_mutex
    mutable TraceSnapshot m_cachedSnapshot;              // guarded by m_mutex
    std::atomic<quint64> m_version{0};
    std::atomic<bool> m_notifyPending{false};
    std::atomic<qsizetype> m_maximumCount{0};
};

}  // namespace mybench::plot
