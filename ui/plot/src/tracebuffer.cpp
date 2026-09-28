#include "mybench/plot/tracebuffer.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QtLogging>

#include <algorithm>
#include <cmath>

namespace mybench::plot {

// ---------------------------------------------------------------------------------------------
// TraceChunk
// ---------------------------------------------------------------------------------------------

TraceChunk::TraceChunk(size_t cap, bool explicitX)
    : capacity(cap), y(std::make_unique_for_overwrite<double[]>(std::max<size_t>(cap, 1)))
{
    if (explicitX)
        x = std::make_unique_for_overwrite<double[]>(std::max<size_t>(cap, 1));
    for (int l = 0; l < kLevels; ++l) {
        const size_t s = bucketShift(l);
        const size_t entries = (cap + (size_t(1) << s) - 1) >> s;
        if (entries == 0)
            continue;
        mn[l] = std::make_unique_for_overwrite<float[]>(entries);
        mx[l] = std::make_unique_for_overwrite<float[]>(entries);
    }
}

namespace {

// Pyramid entries are floats: round outwards so an envelope always contains its samples.
inline float floorFloat(double v)
{
    float f = float(v);
    return double(f) > v ? std::nextafter(f, -std::numeric_limits<float>::infinity()) : f;
}

inline float ceilFloat(double v)
{
    float f = float(v);
    return double(f) < v ? std::nextafter(f, std::numeric_limits<float>::infinity()) : f;
}

}  // namespace

void TraceChunk::updatePyramid(size_t from, size_t to)
{
    if (to <= from)
        return;
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr size_t fan = size_t(1) << kFanShift;
    // Level 0 from the samples.
    {
        const double *yv = y.get();
        float *lo0 = mn[0].get(), *hi0 = mx[0].get();
        const size_t b0 = from >> kFanShift, b1 = (to - 1) >> kFanShift;
        for (size_t b = b0; b <= b1; ++b) {
            const size_t s = b << kFanShift, e = std::min(s + fan, to);
            double lo = inf, hi = -inf;
            for (size_t i = s; i < e; ++i) {
                const double v = yv[i];
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }
            lo0[b] = floorFloat(lo);
            hi0[b] = ceilFloat(hi);
        }
    }
    // Higher levels from the level below.
    for (int l = 1; l < kLevels; ++l) {
        if (!mn[l])
            break;
        const float *clo = mn[l - 1].get(), *chi = mx[l - 1].get();
        float *plo = mn[l].get(), *phi = mx[l].get();
        const size_t shift = bucketShift(l), childShift = bucketShift(l - 1);
        const size_t childValid = (to + (size_t(1) << childShift) - 1) >> childShift;
        const size_t b0 = from >> shift, b1 = (to - 1) >> shift;
        for (size_t b = b0; b <= b1; ++b) {
            const size_t s = b << kFanShift, e = std::min(s + fan, childValid);
            float lo = inf, hi = -inf;
            for (size_t i = s; i < e; ++i) {
                lo = clo[i] < lo ? clo[i] : lo;
                hi = chi[i] > hi ? chi[i] : hi;
            }
            plo[b] = lo;
            phi[b] = hi;
        }
    }
}

void TraceChunk::rangeMinMax(size_t p, size_t end, double &lo, double &hi) const
{
    if (end <= p)
        return;
    // Coarsest level with at least one whole bucket inside [p, end): scan its buckets, then the
    // two partial ends with finer levels (each shorter than one bucket, so depth <= kLevels).
    for (int l = kLevels - 1; l >= 0; --l) {
        if (!mn[l])
            continue;
        const size_t shift = bucketShift(l);
        const size_t bucket = size_t(1) << shift;
        const size_t a = (p + bucket - 1) & ~(bucket - 1);
        const size_t b = end & ~(bucket - 1);
        if (a + bucket > end || b <= a)
            continue;
        const float *blo = mn[l].get(), *bhi = mx[l].get();
        for (size_t i = a >> shift, n = b >> shift; i < n; ++i) {
            const double vlo = blo[i], vhi = bhi[i];
            lo = vlo < lo ? vlo : lo;
            hi = vhi > hi ? vhi : hi;
        }
        rangeMinMax(p, a, lo, hi);
        rangeMinMax(b, end, lo, hi);
        return;
    }
    const double *yv = y.get();
    for (size_t i = p; i < end; ++i) {
        const double v = yv[i];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
}

// ---------------------------------------------------------------------------------------------
// TraceData
// ---------------------------------------------------------------------------------------------

size_t TraceData::lowerBound(double value) const
{
    const size_t n = size();
    if (n == 0)
        return 0;
    if (uniform) {
        const double first = xAt(0);
        if (!(value > first))
            return 0;
        const double i = std::ceil((value - first) / dx);
        return i >= double(n) ? n : size_t(i);
    }
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (xAt(mid) < value)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

size_t TraceData::upperBound(double value) const
{
    const size_t n = size();
    if (n == 0)
        return 0;
    if (uniform) {
        const double first = xAt(0);
        if (value < first)
            return 0;
        const double i = std::floor((value - first) / dx) + 1.0;
        return i >= double(n) ? n : size_t(i);
    }
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (xAt(mid) <= value)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

bool TraceData::rangeMinMax(size_t i, size_t j, double *yMin, double *yMax) const
{
    j = std::min(j, size());
    if (i >= j)
        return false;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    size_t a = m_begin + i;
    const size_t aEnd = m_begin + j;
    while (a < aEnd) {
        const size_t k = a >> TraceChunk::kShift;
        const size_t chunkStart = k << TraceChunk::kShift;
        const size_t stop = std::min(aEnd, chunkStart + TraceChunk::kSize);
        m_chunks[k - m_firstChunk]->rangeMinMax(a - chunkStart, stop - chunkStart, lo, hi);
        a = stop;
    }
    if (!(lo <= hi))
        return false;
    *yMin = lo;
    *yMax = hi;
    return true;
}

double TraceData::interpolate(double xv) const
{
    const size_t n = size();
    if (n == 0 || !std::isfinite(xv))
        return qQNaN();
    if (n == 1)
        return xv == xAt(0) ? yAt(0) : qQNaN();
    if (xv < xFirst() || xv > xLast())
        return qQNaN();
    size_t i = lowerBound(xv);
    if (i >= n)
        i = n - 1;
    if (i == 0)
        return yAt(0);
    const double xa = xAt(i - 1), xb = xAt(i);
    const double ya = yAt(i - 1), yb = yAt(i);
    if (xb <= xa)
        return yb;
    return ya + (yb - ya) * ((xv - xa) / (xb - xa));
}

size_t TraceData::nearest(double xv) const
{
    const size_t n = size();
    if (n == 0)
        return 0;
    if (!sorted) {
        size_t best = 0;
        double bestD = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < n; ++i) {
            const double d = std::abs(xAt(i) - xv);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    }
    const size_t i = lowerBound(xv);
    if (i >= n)
        return n - 1;
    if (i > 0 && std::abs(xAt(i - 1) - xv) <= std::abs(xAt(i) - xv))
        return i - 1;
    return i;
}

bool TraceData::yRange(double xMin, double xMax, double *yMin, double *yMax) const
{
    const size_t n = size();
    if (n == 0)
        return false;
    if (sorted)
        return rangeMinMax(lowerBound(xMin), upperBound(xMax), yMin, yMax);
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < n; ++i) {
        const double xv = xAt(i), v = yAt(i);
        if (xv < xMin || xv > xMax || !std::isfinite(v))
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (!(lo <= hi))
        return false;
    *yMin = lo;
    *yMax = hi;
    return true;
}

// ---------------------------------------------------------------------------------------------
// TraceBuffer
// ---------------------------------------------------------------------------------------------

struct TraceBuffer::State {
    bool uniform = true;
    bool sorted = true;
    double x0 = 0.0;
    double dx = 1.0;
    size_t begin = 0;
    size_t end = 0;
    size_t firstChunk = 0;
    double lastX = -std::numeric_limits<double>::infinity();
    std::vector<std::shared_ptr<TraceChunk>> chunks;
};

TraceBuffer::TraceBuffer(QObject *parent) : QObject(parent), m_state(std::make_unique<State>()) {}

TraceBuffer::~TraceBuffer() = default;

namespace {

template <typename T>
void fillChunks(std::vector<std::shared_ptr<TraceChunk>> &chunks, bool explicitX, const T *x, const T *y, size_t n)
{
    for (size_t start = 0; start < n; start += TraceChunk::kSize) {
        const size_t count = std::min(TraceChunk::kSize, n - start);
        auto chunk = std::make_shared<TraceChunk>(count, explicitX);
        for (size_t i = 0; i < count; ++i)
            chunk->y[i] = double(y[start + i]);
        if (explicitX) {
            for (size_t i = 0; i < count; ++i)
                chunk->x[i] = double(x[start + i]);
        }
        chunk->updatePyramid(0, count);
        chunks.push_back(std::move(chunk));
    }
}

template <typename T>
bool sortedX(const T *x, size_t n)
{
    for (size_t i = 1; i < n; ++i) {
        if (x[i] < x[i - 1])
            return false;
    }
    return true;
}

}  // namespace

void TraceBuffer::replace(std::unique_ptr<State> state)
{
    {
        QMutexLocker lock(&m_mutex);
        m_state = std::move(state);
        publishLocked();
    }
    scheduleNotify();
}

void TraceBuffer::publishLocked()
{
    m_version.store(m_version.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    m_cachedSnapshot.reset();
}

void TraceBuffer::scheduleNotify()
{
    if (m_notifyPending.exchange(true, std::memory_order_acq_rel))
        return;  // a notification is already queued; it will see the latest data
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_notifyPending.store(false, std::memory_order_release);
            emit changed();
        },
        Qt::QueuedConnection);
}

void TraceBuffer::setUniform(double x0, double dx, std::span<const double> y)
{
    auto s = std::make_unique<State>();
    s->x0 = std::isfinite(x0) ? x0 : 0.0;
    s->dx = (std::isfinite(dx) && dx > 0) ? dx : 1.0;
    fillChunks<double>(s->chunks, false, nullptr, y.data(), y.size());
    s->end = y.size();
    replace(std::move(s));
}

void TraceBuffer::setUniform(double x0, double dx, std::span<const float> y)
{
    auto s = std::make_unique<State>();
    s->x0 = std::isfinite(x0) ? x0 : 0.0;
    s->dx = (std::isfinite(dx) && dx > 0) ? dx : 1.0;
    fillChunks<float>(s->chunks, false, nullptr, y.data(), y.size());
    s->end = y.size();
    replace(std::move(s));
}

void TraceBuffer::setXY(std::span<const double> x, std::span<const double> y)
{
    const size_t n = std::min(x.size(), y.size());
    auto s = std::make_unique<State>();
    s->uniform = false;
    s->sorted = sortedX(x.data(), n);
    fillChunks<double>(s->chunks, true, x.data(), y.data(), n);
    s->end = n;
    s->lastX = n ? x[n - 1] : s->lastX;
    replace(std::move(s));
}

void TraceBuffer::setXY(std::span<const float> x, std::span<const float> y)
{
    const size_t n = std::min(x.size(), y.size());
    auto s = std::make_unique<State>();
    s->uniform = false;
    s->sorted = sortedX(x.data(), n);
    fillChunks<float>(s->chunks, true, x.data(), y.data(), n);
    s->end = n;
    s->lastX = n ? double(x[n - 1]) : s->lastX;
    replace(std::move(s));
}

void TraceBuffer::clear()
{
    auto s = std::make_unique<State>();
    {
        QMutexLocker lock(&m_mutex);
        s->uniform = m_state->uniform;
        s->x0 = m_state->x0;
        s->dx = m_state->dx;
    }
    replace(std::move(s));
}

void TraceBuffer::startUniform(double x0, double dx)
{
    auto s = std::make_unique<State>();
    s->x0 = std::isfinite(x0) ? x0 : 0.0;
    s->dx = (std::isfinite(dx) && dx > 0) ? dx : 1.0;
    replace(std::move(s));
}

void TraceBuffer::convertToExplicitLocked()
{
    State &s = *m_state;
    for (size_t idx = 0; idx < s.chunks.size(); ++idx) {
        const TraceChunk &old = *s.chunks[idx];
        auto chunk = std::make_shared<TraceChunk>(old.capacity, true);
        const size_t chunkStart = (s.firstChunk + idx) << TraceChunk::kShift;
        const size_t count = std::min(old.capacity, s.end - chunkStart);
        std::copy(old.y.get(), old.y.get() + count, chunk->y.get());
        for (size_t i = 0; i < count; ++i)
            chunk->x[i] = s.x0 + s.dx * double(chunkStart + i);
        chunk->updatePyramid(0, count);
        s.chunks[idx] = std::move(chunk);
    }
    if (s.end > s.begin)
        s.lastX = s.x0 + s.dx * double(s.end - 1);
    s.uniform = false;
}

void TraceBuffer::appendLocked(const double *x, const double *y, size_t n)
{
    State &s = *m_state;
    size_t written = 0;
    while (written < n) {
        const size_t a = s.end;
        const size_t k = a >> TraceChunk::kShift;
        const size_t off = a & (TraceChunk::kSize - 1);
        const size_t idx = k - s.firstChunk;
        if (idx == s.chunks.size()) {
            s.chunks.push_back(std::make_shared<TraceChunk>(TraceChunk::kSize, !s.uniform));
        } else if (s.chunks[idx]->capacity < TraceChunk::kSize) {
            // A chunk from a setter is sized to fit: grow it by copying (readers keep the old one).
            const TraceChunk &old = *s.chunks[idx];
            auto bigger = std::make_shared<TraceChunk>(TraceChunk::kSize, !s.uniform);
            std::copy(old.y.get(), old.y.get() + off, bigger->y.get());
            if (bigger->x)
                std::copy(old.x.get(), old.x.get() + off, bigger->x.get());
            bigger->updatePyramid(0, off);
            s.chunks[idx] = std::move(bigger);
        }
        TraceChunk &c = *s.chunks[idx];
        const size_t m = std::min(n - written, TraceChunk::kSize - off);
        std::copy(y + written, y + written + m, c.y.get() + off);
        if (x)
            std::copy(x + written, x + written + m, c.x.get() + off);
        c.updatePyramid(off, off + m);
        s.end += m;
        written += m;
    }
    trimLocked();
}

void TraceBuffer::trimLocked()
{
    State &s = *m_state;
    const qsizetype maxCount = m_maximumCount.load();
    if (maxCount <= 0 || s.end - s.begin <= size_t(maxCount))
        return;
    s.begin = s.end - size_t(maxCount);
    size_t drop = 0;
    while (drop < s.chunks.size() && ((s.firstChunk + drop + 1) << TraceChunk::kShift) <= s.begin)
        ++drop;
    if (drop) {
        s.chunks.erase(s.chunks.begin(), s.chunks.begin() + qsizetype(drop));
        s.firstChunk += drop;
    }
}

void TraceBuffer::appendSamples(std::span<const double> y)
{
    if (y.empty())
        return;
    {
        QMutexLocker lock(&m_mutex);
        State &s = *m_state;
        if (!s.uniform) {
            if (s.end > s.begin) {
                qWarning("TraceBuffer::appendSamples: buffer holds explicit x data; use append(x, y)");
                return;
            }
            s.uniform = true;
            s.sorted = true;
        }
        appendLocked(nullptr, y.data(), y.size());
        publishLocked();
    }
    scheduleNotify();
}

void TraceBuffer::appendSamples(std::span<const float> y)
{
    std::vector<double> v(y.begin(), y.end());
    appendSamples(std::span<const double>(v));
}

void TraceBuffer::append(double x, double y)
{
    append(std::span<const double>(&x, 1), std::span<const double>(&y, 1));
}

void TraceBuffer::append(std::span<const double> x, std::span<const double> y)
{
    const size_t n = std::min(x.size(), y.size());
    if (n == 0)
        return;
    {
        QMutexLocker lock(&m_mutex);
        State &s = *m_state;
        if (s.uniform) {
            if (s.end > s.begin)
                convertToExplicitLocked();
            else
                s.uniform = false;
        }
        if (s.sorted && (x[0] < s.lastX || !sortedX(x.data(), n)))
            s.sorted = false;
        s.lastX = x[n - 1];
        appendLocked(x.data(), y.data(), n);
        publishLocked();
    }
    scheduleNotify();
}

TraceSnapshot TraceBuffer::snapshot() const
{
    QMutexLocker lock(&m_mutex);
    if (!m_cachedSnapshot) {
        auto d = std::make_shared<TraceData>();
        const State &s = *m_state;
        d->uniform = s.uniform;
        d->sorted = s.sorted;
        d->x0 = s.x0;
        d->dx = s.dx;
        d->version = m_version.load(std::memory_order_relaxed);
        d->m_begin = s.begin;
        d->m_end = s.end;
        d->m_firstChunk = s.firstChunk;
        d->m_chunks = s.chunks;
        m_cachedSnapshot = std::move(d);
    }
    return m_cachedSnapshot;
}

qsizetype TraceBuffer::count() const
{
    QMutexLocker lock(&m_mutex);
    return qsizetype(m_state->end - m_state->begin);
}

bool TraceBuffer::isUniform() const
{
    QMutexLocker lock(&m_mutex);
    return m_state->uniform;
}

void TraceBuffer::setMaximumCount(qsizetype count)
{
    count = std::max<qsizetype>(0, count);
    if (m_maximumCount.exchange(count) == count)
        return;
    {
        QMutexLocker lock(&m_mutex);
        const size_t before = m_state->begin;
        trimLocked();
        if (m_state->begin != before)
            publishLocked();
    }
    emit maximumCountChanged();
    scheduleNotify();
}

double TraceBuffer::valueAt(double x) const
{
    return snapshot()->interpolate(x);
}

QPointF TraceBuffer::nearestSample(double x) const
{
    const TraceSnapshot s = snapshot();
    if (s->empty())
        return QPointF(qQNaN(), qQNaN());
    const size_t i = s->nearest(x);
    return QPointF(s->xAt(i), s->yAt(i));
}

double TraceBuffer::xMinimum() const
{
    return snapshot()->xFirst();
}

double TraceBuffer::xMaximum() const
{
    return snapshot()->xLast();
}

}  // namespace mybench::plot
