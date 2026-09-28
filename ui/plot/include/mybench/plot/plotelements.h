// Plot elements: declared inside a Plot (or attached with `plot: somePlot`), drawn by it.
//
//   PlotTrace   a data series from a TraceBuffer (line, points, fill, step, persistence)
//   PlotCursor  draggable measurement cursor (vertical: x, horizontal: y), with readout
//   PlotLine    draggable reference line with an edge handle (trigger level / position)
//   PlotMarker  point marker with a label (harmonic peak, −3 dB point)
//   PlotRegion  highlighted x or y span (optionally draggable: overview/scroll bar of a recording)
//   PlotLogicTrace  stacked digital lanes from a LogicBuffer (DIO timeline)
//   PlotWaterfall   spectrogram rows from a WaterfallBuffer (see waterfall.h)
#pragma once

#include "mybench/plot/logicbuffer.h"
#include "mybench/plot/tracebuffer.h"

#include <QList>
#include <QStringList>

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

namespace mybench::plot {

class Plot;

class PlotElement : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    QML_UNCREATABLE("PlotElement is the base of PlotTrace, PlotCursor, PlotLine, PlotMarker, PlotRegion")
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged FINAL)
    Q_PROPERTY(mybench::plot::Plot *plot READ plot WRITE setPlot NOTIFY plotChanged FINAL)
    Q_PROPERTY(double z READ z WRITE setZ NOTIFY zChanged FINAL)

public:
    enum LineStyle { Solid, Dash, Dot, DashDot };
    Q_ENUM(LineStyle)
    enum Orientation { Vertical, Horizontal };
    Q_ENUM(Orientation)

    explicit PlotElement(QObject *parent = nullptr);
    ~PlotElement() override;

    bool isVisible() const { return m_visible; }
    void setVisible(bool on);
    // The plot drawing this element. Defaults to the parent Plot.
    Plot *plot() const;
    void setPlot(Plot *plot);
    // Stacking among elements of the same kind (higher on top); ties keep declaration order.
    double z() const { return m_z; }
    void setZ(double z);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void visibleChanged();
    void plotChanged();
    void zChanged();
    // Any change that needs a repaint.
    void changed();

protected:
    void notifyChanged() { emit changed(); }

private:
    QPointer<Plot> m_plot;
    bool m_visible = true;
    double m_z = 0;
};

class PlotTrace : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::TraceBuffer *buffer READ buffer WRITE setBuffer NOTIFY bufferChanged FINAL)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor RESET resetColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(double width READ width WRITE setWidth NOTIFY styleChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotElement::LineStyle style READ style WRITE setStyle NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool lineVisible READ lineVisible WRITE setLineVisible NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool pointsVisible READ pointsVisible WRITE setPointsVisible NOTIFY styleChanged FINAL)
    Q_PROPERTY(double pointSize READ pointSize WRITE setPointSize NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool step READ step WRITE setStep NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool fill READ fill WRITE setFill NOTIFY styleChanged FINAL)
    Q_PROPERTY(double fillBaseline READ fillBaseline WRITE setFillBaseline NOTIFY styleChanged FINAL)
    Q_PROPERTY(double fillOpacity READ fillOpacity WRITE setFillOpacity NOTIFY styleChanged FINAL)
    Q_PROPERTY(double xScale READ xScale WRITE setXScale NOTIFY transformChanged FINAL)
    Q_PROPERTY(double xOffset READ xOffset WRITE setXOffset NOTIFY transformChanged FINAL)
    Q_PROPERTY(double yScale READ yScale WRITE setYScale NOTIFY transformChanged FINAL)
    Q_PROPERTY(double yOffset READ yOffset WRITE setYOffset NOTIFY transformChanged FINAL)
    Q_PROPERTY(int persistence READ persistence WRITE setPersistence NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool legendVisible READ legendVisible WRITE setLegendVisible NOTIFY styleChanged FINAL)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY styleChanged FINAL)

public:
    explicit PlotTrace(QObject *parent = nullptr);

    TraceBuffer *buffer() const { return m_buffer; }
    void setBuffer(TraceBuffer *buffer);
    // Legend / readout name ("CH0").
    QString name() const { return m_name; }
    void setName(const QString &n);
    // Unset: the plot's palette color for this trace's index (CH0 green, CH1 cyan, ...).
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    void resetColor();
    bool hasColor() const { return m_color.isValid(); }
    double width() const { return m_width; }          // px
    void setWidth(double w);
    LineStyle style() const { return m_style; }
    void setStyle(LineStyle s);
    bool lineVisible() const { return m_lineVisible; }
    void setLineVisible(bool on);
    // Dots at samples (only drawn while samples are sparser than ~1 per 3 px).
    bool pointsVisible() const { return m_pointsVisible; }
    void setPointsVisible(bool on);
    double pointSize() const { return m_pointSize; }  // diameter, px
    void setPointSize(double s);
    // Hold each value until the next sample (staircase).
    bool step() const { return m_step; }
    void setStep(bool on);
    // Fill between the trace and fillBaseline (display units; NaN = bottom of the plot area).
    bool fill() const { return m_fill; }
    void setFill(bool on);
    double fillBaseline() const { return m_fillBaseline; }
    void setFillBaseline(double v);
    double fillOpacity() const { return m_fillOpacity; }
    void setFillOpacity(double o);
    // display = value * scale + offset. Scope: yScale = 1 / (V/div), yOffset = position (div).
    double xScale() const { return m_xScale; }
    void setXScale(double s);
    double xOffset() const { return m_xOffset; }
    void setXOffset(double o);
    double yScale() const { return m_yScale; }
    void setYScale(double s);
    double yOffset() const { return m_yOffset; }
    void setYOffset(double o);
    // Persistence (afterglow) in ms: previous records fade out over this time. 0 = off.
    int persistence() const { return m_persistence; }
    void setPersistence(int ms);
    bool legendVisible() const { return m_legendVisible; }
    void setLegendVisible(bool on);
    // Unit of the trace's values for readouts (defaults to the y axis unit when empty).
    QString unit() const { return m_unit; }
    void setUnit(const QString &u);

    // Value of the trace at data x (linear interpolation, untransformed data units).
    Q_INVOKABLE double valueAt(double x) const;
    // Nearest sample (data units) to data x.
    Q_INVOKABLE QPointF nearestSample(double x) const;
    // Display <-> data conversions of the transform.
    Q_INVOKABLE double toDisplayY(double value) const { return value * m_yScale + m_yOffset; }
    Q_INVOKABLE double fromDisplayY(double display) const
    {
        return m_yScale != 0 ? (display - m_yOffset) / m_yScale : qQNaN();
    }
    Q_INVOKABLE double toDisplayX(double value) const { return value * m_xScale + m_xOffset; }
    Q_INVOKABLE double fromDisplayX(double display) const
    {
        return m_xScale != 0 ? (display - m_xOffset) / m_xScale : qQNaN();
    }

signals:
    void bufferChanged();
    void styleChanged();
    void transformChanged();
    // The buffer's data changed (forwarded, on the GUI thread).
    void dataChanged();

private:
    QPointer<TraceBuffer> m_buffer;
    QString m_name;
    QString m_unit;
    QColor m_color;
    double m_width = 1.5;
    LineStyle m_style = Solid;
    bool m_lineVisible = true;
    bool m_pointsVisible = false;
    double m_pointSize = 4.5;
    bool m_step = false;
    bool m_fill = false;
    double m_fillBaseline = qQNaN();
    double m_fillOpacity = 0.22;
    double m_xScale = 1.0;
    double m_xOffset = 0.0;
    double m_yScale = 1.0;
    double m_yOffset = 0.0;
    int m_persistence = 0;
    bool m_legendVisible = true;
};

class PlotCursor : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::PlotElement::Orientation orientation READ orientation WRITE setOrientation NOTIFY cursorChanged FINAL)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY cursorChanged FINAL)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY cursorChanged FINAL)
    Q_PROPERTY(bool draggable READ draggable WRITE setDraggable NOTIFY cursorChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotTrace *trace READ trace WRITE setTrace NOTIFY cursorChanged FINAL)
    Q_PROPERTY(bool snap READ snap WRITE setSnap NOTIFY cursorChanged FINAL)
    Q_PROPERTY(bool readoutVisible READ readoutVisible WRITE setReadoutVisible NOTIFY cursorChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotElement::LineStyle style READ style WRITE setStyle NOTIFY cursorChanged FINAL)
    Q_PROPERTY(double value READ value NOTIFY valueChanged FINAL)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged FINAL)
    Q_PROPERTY(bool selected READ selected WRITE setSelected NOTIFY selectedChanged FINAL)

public:
    explicit PlotCursor(QObject *parent = nullptr);

    // Vertical: a vertical line at x = position. Horizontal: a horizontal line at y = position.
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation o);
    // Data units of the axis (display units: after a trace's transform).
    double position() const { return m_position; }
    void setPosition(double p);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    QString label() const { return m_label; }
    void setLabel(const QString &l);
    bool draggable() const { return m_draggable; }
    void setDraggable(bool on);
    // Trace read at the cursor (vertical cursors): value, dot and value chip.
    PlotTrace *trace() const { return m_trace; }
    void setTrace(PlotTrace *t);
    // While dragging, jump to the nearest sample of `trace`.
    bool snap() const { return m_snap; }
    void setSnap(bool on);
    bool readoutVisible() const { return m_readoutVisible; }
    void setReadoutVisible(bool on);
    LineStyle style() const { return m_style; }
    void setStyle(LineStyle s);
    // Vertical with trace: trace value (data units) at position; horizontal: the position.
    double value() const;
    bool dragging() const { return m_dragging; }
    bool selected() const { return m_selected; }
    void setSelected(bool on);

    void setDragging(bool on);

signals:
    void cursorChanged();
    void positionChanged();
    void valueChanged();
    void draggingChanged();
    void selectedChanged();
    // The user moved the cursor (drag or keyboard) to `position`.
    void moved(double position);

private:
    Orientation m_orientation = Vertical;
    double m_position = 0.0;
    QColor m_color = QColor(0xf5, 0xb8, 0x41);
    QString m_label;
    bool m_draggable = true;
    QPointer<PlotTrace> m_trace;
    bool m_snap = false;
    bool m_readoutVisible = true;
    LineStyle m_style = Solid;
    bool m_dragging = false;
    bool m_selected = false;
};

class PlotLine : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::PlotElement::Orientation orientation READ orientation WRITE setOrientation NOTIFY lineChanged FINAL)
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY lineChanged FINAL)
    Q_PROPERTY(double width READ width WRITE setWidth NOTIFY lineChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotElement::LineStyle style READ style WRITE setStyle NOTIFY lineChanged FINAL)
    Q_PROPERTY(bool draggable READ draggable WRITE setDraggable NOTIFY lineChanged FINAL)
    Q_PROPERTY(Handle handle READ handle WRITE setHandle NOTIFY lineChanged FINAL)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY lineChanged FINAL)
    Q_PROPERTY(LineMode lineMode READ lineMode WRITE setLineMode NOTIFY lineChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotTrace *trace READ trace WRITE setTrace NOTIFY lineChanged FINAL)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged FINAL)

public:
    enum Handle { NoHandle, Start, End };
    Q_ENUM(Handle)
    enum LineMode { Always, WhileDragging, Never };
    Q_ENUM(LineMode)

    explicit PlotLine(QObject *parent = nullptr);

    // Horizontal: y = value (e.g. trigger level). Vertical: x = value (trigger position).
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation o);
    // Axis units, or the data units of `trace` (then mapped through its transform).
    double value() const { return m_value; }
    void setValue(double v);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    double width() const { return m_width; }
    void setWidth(double w);
    LineStyle style() const { return m_style; }
    void setStyle(LineStyle s);
    bool draggable() const { return m_draggable; }
    void setDraggable(bool on);
    // Handle on the plot edge: Start = left (horizontal line) / top (vertical line); End =
    // right / bottom. The handle is the drag grip and shows `label` ("T").
    Handle handle() const { return m_handle; }
    void setHandle(Handle h);
    QString label() const { return m_label; }
    void setLabel(const QString &l);
    LineMode lineMode() const { return m_lineMode; }
    void setLineMode(LineMode m);
    PlotTrace *trace() const { return m_trace; }
    void setTrace(PlotTrace *t);
    bool dragging() const { return m_dragging; }
    void setDragging(bool on);

    // Axis (display) coordinate of the line.
    double displayValue() const;
    double fromDisplay(double display) const;

signals:
    void lineChanged();
    void valueChanged();
    void draggingChanged();
    // The user dragged the line to `value` (same units as `value`).
    void moved(double value);

private:
    Orientation m_orientation = Horizontal;
    double m_value = 0.0;
    QColor m_color = QColor(0xf5, 0xb8, 0x41);
    double m_width = 1.0;
    LineStyle m_style = Dash;
    bool m_draggable = true;
    Handle m_handle = Start;
    QString m_label;
    LineMode m_lineMode = Always;
    QPointer<PlotTrace> m_trace;
    bool m_dragging = false;
};

class PlotMarker : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double x READ x WRITE setX NOTIFY markerChanged FINAL)
    Q_PROPERTY(double y READ y WRITE setY NOTIFY markerChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotTrace *trace READ trace WRITE setTrace NOTIFY markerChanged FINAL)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY markerChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor RESET resetColor NOTIFY markerChanged FINAL)
    Q_PROPERTY(Shape shape READ shape WRITE setShape NOTIFY markerChanged FINAL)
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY markerChanged FINAL)
    Q_PROPERTY(LabelPosition labelPosition READ labelPosition WRITE setLabelPosition NOTIFY markerChanged FINAL)
    Q_PROPERTY(bool guides READ guides WRITE setGuides NOTIFY markerChanged FINAL)

public:
    enum Shape { Circle, Diamond, TriangleDown, TriangleUp, Square, Ring };
    Q_ENUM(Shape)
    enum LabelPosition { Above, Below, Left, Right };
    Q_ENUM(LabelPosition)

    explicit PlotMarker(QObject *parent = nullptr);

    // Display coordinates. With `trace` and y unset (NaN), y follows the trace at x.
    double x() const { return m_x; }
    void setX(double v);
    double y() const { return m_y; }
    void setY(double v);
    PlotTrace *trace() const { return m_trace; }
    void setTrace(PlotTrace *t);
    QString label() const { return m_label; }
    void setLabel(const QString &l);
    // Unset: the trace color, else the plot text color.
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    void resetColor();
    Shape shape() const { return m_shape; }
    void setShape(Shape s);
    double size() const { return m_size; }
    void setSize(double s);
    LabelPosition labelPosition() const { return m_labelPosition; }
    void setLabelPosition(LabelPosition p);
    // Dashed guide lines to the x and y axes (e.g. the −3 dB point).
    bool guides() const { return m_guides; }
    void setGuides(bool on);

    // Resolved display y (from the trace if y is NaN).
    double displayY() const;

signals:
    void markerChanged();

private:
    double m_x = 0.0;
    double m_y = qQNaN();
    QPointer<PlotTrace> m_trace;
    QString m_label;
    QColor m_color;
    Shape m_shape = Circle;
    double m_size = 8.0;
    LabelPosition m_labelPosition = Above;
    bool m_guides = false;
};

class PlotRegion : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::PlotElement::Orientation orientation READ orientation WRITE setOrientation NOTIFY regionChanged FINAL)
    Q_PROPERTY(double from READ from WRITE setFrom NOTIFY regionChanged FINAL)
    Q_PROPERTY(double to READ to WRITE setTo NOTIFY regionChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY regionChanged FINAL)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY regionChanged FINAL)
    Q_PROPERTY(bool edgesVisible READ edgesVisible WRITE setEdgesVisible NOTIFY regionChanged FINAL)
    Q_PROPERTY(bool draggable READ draggable WRITE setDraggable NOTIFY regionChanged FINAL)
    Q_PROPERTY(bool resizable READ resizable WRITE setResizable NOTIFY regionChanged FINAL)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged FINAL)

public:
    explicit PlotRegion(QObject *parent = nullptr);

    // Drag the body to move the span (e.g. the visible window of a recording in an overview
    // plot); with resizable, drag the edges to change it. Emits moved(from, to).
    bool draggable() const { return m_draggable; }
    void setDraggable(bool on);
    bool resizable() const { return m_resizable; }
    void setResizable(bool on);
    bool dragging() const { return m_dragging; }
    void setDragging(bool on);

    // Vertical: x span [from, to] over the full height. Horizontal: y span over the full width.
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation o);
    double from() const { return m_from; }
    void setFrom(double v);
    double to() const { return m_to; }
    void setTo(double v);
    // Fill color; give it alpha (default translucent accent).
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    QString label() const { return m_label; }
    void setLabel(const QString &l);
    bool edgesVisible() const { return m_edgesVisible; }
    void setEdgesVisible(bool on);

signals:
    void regionChanged();
    void draggingChanged();
    // The user dragged the region to [from, to].
    void moved(double from, double to);

private:
    Orientation m_orientation = Vertical;
    double m_from = 0.0;
    double m_to = 0.0;
    QColor m_color = QColor(76, 141, 255, 38);
    QString m_label;
    bool m_edgesVisible = true;
    bool m_draggable = false;
    bool m_resizable = true;
    bool m_dragging = false;
};

class PlotLogicTrace : public PlotElement
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::LogicBuffer *buffer READ buffer WRITE setBuffer NOTIFY bufferChanged FINAL)
    Q_PROPERTY(QList<int> lanes READ lanes WRITE setLanes NOTIFY styleChanged FINAL)
    Q_PROPERTY(QStringList laneNames READ laneNames WRITE setLaneNames NOTIFY styleChanged FINAL)
    Q_PROPERTY(QList<QColor> laneColors READ laneColors WRITE setLaneColors NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(double firstRow READ firstRow WRITE setFirstRow NOTIFY styleChanged FINAL)
    Q_PROPERTY(double laneHeight READ laneHeight WRITE setLaneHeight NOTIFY styleChanged FINAL)
    Q_PROPERTY(double width READ width WRITE setWidth NOTIFY styleChanged FINAL)
    Q_PROPERTY(double fillOpacity READ fillOpacity WRITE setFillOpacity NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool labelsVisible READ labelsVisible WRITE setLabelsVisible NOTIFY styleChanged FINAL)

public:
    explicit PlotLogicTrace(QObject *parent = nullptr);

    LogicBuffer *buffer() const { return m_buffer; }
    void setBuffer(LogicBuffer *buffer);
    // Lanes (bit numbers) to show, top row first. Empty: all lanes of the buffer, 0 first.
    QList<int> lanes() const { return m_lanes; }
    void setLanes(const QList<int> &lanes);
    // Row labels (by row); empty entries / missing: "D<bit>".
    QStringList laneNames() const { return m_laneNames; }
    void setLaneNames(const QStringList &names);
    // Row colors (by row); missing: `color`.
    QList<QColor> laneColors() const { return m_laneColors; }
    void setLaneColors(const QList<QColor> &colors);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    // Row r occupies y (display units) [firstRow + r, firstRow + r + 1] (invert the y axis to
    // have row 0 on top). laneHeight is the fraction of the row between low and high levels.
    double firstRow() const { return m_firstRow; }
    void setFirstRow(double r);
    double laneHeight() const { return m_laneHeight; }
    void setLaneHeight(double h);
    double width() const { return m_width; }
    void setWidth(double w);
    double fillOpacity() const { return m_fillOpacity; }
    void setFillOpacity(double o);
    // Lane names in the plot's left margin (like y tick labels).
    bool labelsVisible() const { return m_labelsVisible; }
    void setLabelsVisible(bool on);

    // Resolved per-row values.
    int rowCount() const;
    int laneForRow(int row) const;
    QString nameForRow(int row) const;
    QColor colorForRow(int row) const;

signals:
    void bufferChanged();
    void styleChanged();

private:
    QPointer<LogicBuffer> m_buffer;
    QList<int> m_lanes;
    QStringList m_laneNames;
    QList<QColor> m_laneColors;
    QColor m_color = QColor(0x3d, 0xdc, 0x84);
    double m_firstRow = 0.0;
    double m_laneHeight = 0.62;
    double m_width = 1.5;
    double m_fillOpacity = 0.16;
    bool m_labelsVisible = true;
};

}  // namespace mybench::plot
