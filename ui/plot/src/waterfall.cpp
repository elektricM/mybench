#include "mybench/plot/waterfall.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace mybench::plot {

// ---------------------------------------------------------------------------------------------
// WaterfallBuffer
// ---------------------------------------------------------------------------------------------

WaterfallBuffer::WaterfallBuffer(QObject *parent) : QObject(parent) {}

void WaterfallBuffer::scheduleNotify()
{
    if (m_notifyPending.exchange(true))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_notifyPending.store(false);
            emit changed();
        },
        Qt::QueuedConnection);
}

void WaterfallBuffer::setColumns(double x0, double dx, int columns)
{
    columns = std::max(0, columns);
    bool resized = false;
    {
        QMutexLocker lock(&m_mutex);
        const bool same = columns == m_columns && x0 == m_x0 && dx == m_dx;
        if (same)
            return;
        m_x0 = std::isfinite(x0) ? x0 : 0.0;
        m_dx = (std::isfinite(dx) && dx != 0) ? dx : 1.0;
        if (columns != m_columns) {
            m_columns = columns;
            m_ring.assign(size_t(m_capacity) * size_t(columns), 0.0f);
            m_rows = 0;
            ++m_generation;
            resized = true;
        }
    }
    Q_UNUSED(resized);
    QMetaObject::invokeMethod(this, &WaterfallBuffer::columnsChanged, Qt::QueuedConnection);
    scheduleNotify();
}

void WaterfallBuffer::appendRow(std::span<const double> values)
{
    std::vector<float> v(values.begin(), values.end());
    appendRow(std::span<const float>(v));
}

void WaterfallBuffer::appendRow(std::span<const float> values)
{
    bool resized = false;
    {
        QMutexLocker lock(&m_mutex);
        if (int(values.size()) != m_columns) {
            m_columns = int(values.size());
            m_ring.assign(size_t(m_capacity) * size_t(m_columns), 0.0f);
            m_rows = 0;
            ++m_generation;
            resized = true;
        }
        if (m_columns == 0)
            return;
        const size_t slot = size_t(m_rows % m_capacity);
        std::copy(values.begin(), values.end(), m_ring.begin() + qsizetype(slot * size_t(m_columns)));
        ++m_rows;
    }
    if (resized)
        QMetaObject::invokeMethod(this, &WaterfallBuffer::columnsChanged, Qt::QueuedConnection);
    scheduleNotify();
}

void WaterfallBuffer::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_rows = 0;
        std::fill(m_ring.begin(), m_ring.end(), 0.0f);
        ++m_generation;
    }
    scheduleNotify();
}

int WaterfallBuffer::columns() const
{
    QMutexLocker lock(&m_mutex);
    return m_columns;
}

int WaterfallBuffer::capacity() const
{
    QMutexLocker lock(&m_mutex);
    return m_capacity;
}

void WaterfallBuffer::setCapacity(int rows)
{
    rows = std::clamp(rows, 1, 16384);
    {
        QMutexLocker lock(&m_mutex);
        if (rows == m_capacity)
            return;
        m_capacity = rows;
        m_ring.assign(size_t(m_capacity) * size_t(m_columns), 0.0f);
        m_rows = 0;
        ++m_generation;
    }
    emit columnsChanged();
    scheduleNotify();
}

qint64 WaterfallBuffer::rowCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_rows;
}

double WaterfallBuffer::x0() const
{
    QMutexLocker lock(&m_mutex);
    return m_x0;
}

double WaterfallBuffer::dx() const
{
    QMutexLocker lock(&m_mutex);
    return m_dx;
}

qint64 WaterfallBuffer::readRows(qint64 firstRow, qint64 count, std::vector<float> &out, int *columns) const
{
    QMutexLocker lock(&m_mutex);
    *columns = m_columns;
    const qint64 oldest = std::max<qint64>(0, m_rows - m_capacity);
    firstRow = std::max(firstRow, oldest);
    const qint64 last = std::min(m_rows, firstRow + std::max<qint64>(0, count));
    out.clear();
    if (last <= firstRow || m_columns == 0)
        return firstRow;
    out.resize(size_t(last - firstRow) * size_t(m_columns));
    for (qint64 r = firstRow; r < last; ++r) {
        const size_t slot = size_t(r % m_capacity);
        std::copy_n(m_ring.begin() + qsizetype(slot * size_t(m_columns)), m_columns,
                    out.begin() + qsizetype(size_t(r - firstRow) * size_t(m_columns)));
    }
    return firstRow;
}

// ---------------------------------------------------------------------------------------------
// PlotWaterfall
// ---------------------------------------------------------------------------------------------

PlotWaterfall::PlotWaterfall(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotWaterfall::styleChanged, this, &PlotElement::changed);
}

void PlotWaterfall::setBuffer(WaterfallBuffer *b)
{
    if (m_buffer == b)
        return;
    if (m_buffer)
        disconnect(m_buffer, nullptr, this, nullptr);
    m_buffer = b;
    if (b) {
        connect(b, &WaterfallBuffer::changed, this, &PlotElement::changed);
        connect(b, &WaterfallBuffer::columnsChanged, this, &PlotElement::changed);
    }
    emit bufferChanged();
    notifyChanged();
}

void PlotWaterfall::setColormap(Colormap c) { if (m_colormap == c) return; m_colormap = c; emit styleChanged(); }
void PlotWaterfall::setMinimum(double v) { if (!std::isfinite(v) || m_minimum == v) return; m_minimum = v; emit styleChanged(); }
void PlotWaterfall::setMaximum(double v) { if (!std::isfinite(v) || m_maximum == v) return; m_maximum = v; emit styleChanged(); }
void PlotWaterfall::setYOrigin(double v) { if (!std::isfinite(v) || m_yOrigin == v) return; m_yOrigin = v; emit styleChanged(); }
void PlotWaterfall::setRowHeight(double h) { if (!std::isfinite(h) || h == 0 || m_rowHeight == h) return; m_rowHeight = h; emit styleChanged(); }
void PlotWaterfall::setSmooth(bool on) { if (m_smooth == on) return; m_smooth = on; emit styleChanged(); }

namespace {

struct Stop {
    double t;
    quint32 rgb;
};

std::array<quint32, 256> buildLut(std::initializer_list<Stop> stops)
{
    std::array<quint32, 256> lut{};
    const std::vector<Stop> s(stops);
    for (int i = 0; i < 256; ++i) {
        const double t = i / 255.0;
        size_t k = 0;
        while (k + 2 < s.size() && t > s[k + 1].t)
            ++k;
        const Stop &a = s[k], &b = s[k + 1];
        const double f = std::clamp((t - a.t) / (b.t - a.t), 0.0, 1.0);
        auto ch = [&](int shift) {
            const double ca = (a.rgb >> shift) & 0xff, cb = (b.rgb >> shift) & 0xff;
            return quint32(std::lround(ca + (cb - ca) * f)) & 0xff;
        };
        // Memory order r, g, b, a (opaque).
        lut[size_t(i)] = ch(16) | (ch(8) << 8) | (ch(0) << 16) | 0xff000000u;
    }
    return lut;
}

}  // namespace

const std::array<quint32, 256> &PlotWaterfall::lut(Colormap c)
{
    // Stops sampled from the matplotlib colormaps (CC0) and simple ramps.
    static const std::array<quint32, 256> inferno = buildLut({
        {0.0, 0x000004}, {0.125, 0x1b0c41}, {0.25, 0x4a0c6b}, {0.375, 0x781c6d}, {0.5, 0xa52c60},
        {0.625, 0xcf4446}, {0.75, 0xed6925}, {0.875, 0xfb9b06}, {1.0, 0xfcffa4}});
    static const std::array<quint32, 256> viridis = buildLut({
        {0.0, 0x440154}, {0.125, 0x482878}, {0.25, 0x3e4989}, {0.375, 0x31688e}, {0.5, 0x26828e},
        {0.625, 0x1f9e89}, {0.75, 0x35b779}, {0.875, 0x6ece58}, {1.0, 0xfde725}});
    static const std::array<quint32, 256> magma = buildLut({
        {0.0, 0x000004}, {0.125, 0x1c1044}, {0.25, 0x4f127b}, {0.375, 0x812581}, {0.5, 0xb5367a},
        {0.625, 0xe55064}, {0.75, 0xfb8761}, {0.875, 0xfec287}, {1.0, 0xfcfdbf}});
    static const std::array<quint32, 256> gray = buildLut({{0.0, 0x000000}, {1.0, 0xffffff}});
    static const std::array<quint32, 256> phosphor = buildLut({
        {0.0, 0x020604}, {0.35, 0x0b3d1f}, {0.7, 0x2fd67a}, {0.9, 0xb6ffd2}, {1.0, 0xffffff}});
    static const std::array<quint32, 256> ice = buildLut({
        {0.0, 0x04060c}, {0.3, 0x0d2a5c}, {0.6, 0x1f7fd1}, {0.85, 0x7fe3ff}, {1.0, 0xffffff}});
    switch (c) {
    case Viridis: return viridis;
    case Magma: return magma;
    case Grayscale: return gray;
    case Phosphor: return phosphor;
    case Ice: return ice;
    case Inferno:
    default: return inferno;
    }
}

QColor PlotWaterfall::colorAt(double t) const
{
    const quint32 v = lut(m_colormap)[size_t(std::clamp(int(std::lround(t * 255.0)), 0, 255))];
    return QColor(int(v & 0xff), int((v >> 8) & 0xff), int((v >> 16) & 0xff));
}

}  // namespace mybench::plot
