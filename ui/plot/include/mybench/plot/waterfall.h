// Spectrogram / waterfall: rolling rows of spectra drawn as a GPU texture with a colormap.
//
//     // worker thread, per spectrum:
//     m_waterfall->setColumns(0.0, df, bins);          // once (or when the bins change)
//     m_waterfall->appendRow(std::span<const double>(dbv));
//     // QML (inside a Plot; x = frequency, y = rows, newest at y = 0 going down to -capacity):
//     Plot { PlotWaterfall { buffer: controller.waterfall; minimum: -120; maximum: 0 } }
//
// Only new rows are colorized and uploaded (sub-rectangle texture uploads into a ring), so
// scrolling costs O(columns) per row. Log frequency axes are handled by tessellating the quad at
// bin edges.
#pragma once

#include "mybench/plot/plotelements.h"

#include <QMutex>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <atomic>
#include <span>
#include <vector>

namespace mybench::plot {

class WaterfallBuffer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int columns READ columns NOTIFY columnsChanged FINAL)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY columnsChanged FINAL)
    Q_PROPERTY(qint64 rowCount READ rowCount NOTIFY changed FINAL)
    Q_PROPERTY(double x0 READ x0 NOTIFY columnsChanged FINAL)
    Q_PROPERTY(double dx READ dx NOTIFY columnsChanged FINAL)

public:
    explicit WaterfallBuffer(QObject *parent = nullptr);

    // Thread-safe. Column k is centered at x0 + k * dx. Changing the column count clears the rows.
    void setColumns(double x0, double dx, int columns);
    // Thread-safe. A row of `columns` values (a different length resizes the columns and clears).
    void appendRow(std::span<const double> values);
    void appendRow(std::span<const float> values);
    Q_INVOKABLE void clear();

    int columns() const;
    // Rows kept (history). Default 256. Thread-safe; clears when changed.
    int capacity() const;
    void setCapacity(int rows);
    // Rows appended since the last clear (monotonic).
    qint64 rowCount() const;
    double x0() const;
    double dx() const;
    quint64 generation() const { return m_generation.load(); }  // changes on clear/resize

    // Copies rows [firstRow, firstRow + count) (clamped to what is kept) into `out`
    // (row-major, columns() floats per row). Returns the first row actually copied.
    qint64 readRows(qint64 firstRow, qint64 count, std::vector<float> &out, int *columns) const;

signals:
    void changed();
    void columnsChanged();

private:
    void scheduleNotify();

    mutable QMutex m_mutex;
    int m_columns = 0;
    int m_capacity = 256;
    double m_x0 = 0.0;
    double m_dx = 1.0;
    qint64 m_rows = 0;
    std::vector<float> m_ring;   // capacity x columns
    std::atomic<quint64> m_generation{0};
    std::atomic<bool> m_notifyPending{false};
};

class PlotWaterfall : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::WaterfallBuffer *buffer READ buffer WRITE setBuffer NOTIFY bufferChanged FINAL)
    Q_PROPERTY(Colormap colormap READ colormap WRITE setColormap NOTIFY styleChanged FINAL)
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY styleChanged FINAL)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY styleChanged FINAL)
    Q_PROPERTY(double yOrigin READ yOrigin WRITE setYOrigin NOTIFY styleChanged FINAL)
    Q_PROPERTY(double rowHeight READ rowHeight WRITE setRowHeight NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool smooth READ smooth WRITE setSmooth NOTIFY styleChanged FINAL)

public:
    enum Colormap { Inferno, Viridis, Magma, Grayscale, Phosphor, Ice };
    Q_ENUM(Colormap)

    explicit PlotWaterfall(QObject *parent = nullptr);

    WaterfallBuffer *buffer() const { return m_buffer; }
    void setBuffer(WaterfallBuffer *b);
    Colormap colormap() const { return m_colormap; }
    void setColormap(Colormap c);
    // Values mapped to the ends of the colormap (e.g. dBV).
    double minimum() const { return m_minimum; }
    void setMinimum(double v);
    double maximum() const { return m_maximum; }
    void setMaximum(double v);
    // The newest row spans y (display units) [yOrigin - rowHeight, yOrigin]; older rows below.
    double yOrigin() const { return m_yOrigin; }
    void setYOrigin(double v);
    double rowHeight() const { return m_rowHeight; }
    void setRowHeight(double h);
    // Linear filtering between bins/rows (false: crisp cells).
    bool smooth() const { return m_smooth; }
    void setSmooth(bool on);

    // 256-entry RGBA lookup table of a colormap.
    static const std::array<quint32, 256> &lut(Colormap c);
    // Colormap color at t in [0, 1].
    Q_INVOKABLE QColor colorAt(double t) const;

signals:
    void bufferChanged();
    void styleChanged();

private:
    QPointer<WaterfallBuffer> m_buffer;
    Colormap m_colormap = Inferno;
    double m_minimum = -120.0;
    double m_maximum = 0.0;
    double m_yOrigin = 0.0;
    double m_rowHeight = 1.0;
    bool m_smooth = true;
};

}  // namespace mybench::plot
