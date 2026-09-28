#include "mybench/plot/plotelements.h"

#include "mybench/plot/plot.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mybench::plot {

#define MYBENCH_SET(value, member, ...)                                                            \
    do {                                                                                           \
        if (member == value)                                                                       \
            return;                                                                                \
        member = value;                                                                            \
        __VA_ARGS__;                                                                               \
        notifyChanged();                                                                           \
    } while (false)

// ---------------------------------------------------------------------------------------------
// PlotElement
// ---------------------------------------------------------------------------------------------

PlotElement::PlotElement(QObject *parent) : QObject(parent) {}

PlotElement::~PlotElement()
{
    if (m_plot)
        m_plot->removeElement(this);
}

Plot *PlotElement::plot() const
{
    return m_plot.data();
}

void PlotElement::setVisible(bool on)
{
    MYBENCH_SET(on, m_visible, emit visibleChanged());
}

void PlotElement::setZ(double z)
{
    MYBENCH_SET(z, m_z, emit zChanged());
}

void PlotElement::setPlot(Plot *plot)
{
    if (m_plot == plot)
        return;
    if (m_plot)
        m_plot->removeElement(this);
    m_plot = plot;
    if (plot)
        plot->addElement(this);
    emit plotChanged();
}

void PlotElement::componentComplete()
{
    if (!m_plot) {
        if (auto *p = qobject_cast<Plot *>(parent()))
            setPlot(p);
    }
}

// ---------------------------------------------------------------------------------------------
// PlotTrace
// ---------------------------------------------------------------------------------------------

PlotTrace::PlotTrace(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotTrace::styleChanged, this, &PlotElement::changed);
    connect(this, &PlotTrace::transformChanged, this, &PlotElement::changed);
}

void PlotTrace::setBuffer(TraceBuffer *buffer)
{
    if (m_buffer == buffer)
        return;
    if (m_buffer)
        disconnect(m_buffer, nullptr, this, nullptr);
    m_buffer = buffer;
    if (buffer) {
        connect(buffer, &TraceBuffer::changed, this, [this] {
            emit dataChanged();
            notifyChanged();
        });
    }
    emit bufferChanged();
    emit dataChanged();
    notifyChanged();
}

void PlotTrace::setName(const QString &n) { if (m_name == n) return; m_name = n; emit styleChanged(); }
void PlotTrace::setUnit(const QString &u) { if (m_unit == u) return; m_unit = u; emit styleChanged(); }
void PlotTrace::setColor(const QColor &c) { if (m_color == c) return; m_color = c; emit styleChanged(); }
void PlotTrace::resetColor() { setColor(QColor()); }
void PlotTrace::setWidth(double w) { w = std::clamp(w, 0.25, 32.0); if (m_width == w) return; m_width = w; emit styleChanged(); }
void PlotTrace::setStyle(LineStyle s) { if (m_style == s) return; m_style = s; emit styleChanged(); }
void PlotTrace::setLineVisible(bool on) { if (m_lineVisible == on) return; m_lineVisible = on; emit styleChanged(); }
void PlotTrace::setPointsVisible(bool on) { if (m_pointsVisible == on) return; m_pointsVisible = on; emit styleChanged(); }
void PlotTrace::setPointSize(double s) { s = std::clamp(s, 1.0, 64.0); if (m_pointSize == s) return; m_pointSize = s; emit styleChanged(); }
void PlotTrace::setStep(bool on)
{
    if (on)
        setInterpolationMode(Step);
    else if (m_interpolation == Step)
        setInterpolationMode(Linear);
}
void PlotTrace::setInterpolationMode(Interpolation i) { i = Interpolation(std::clamp(int(i), 0, int(CenterStepVertical))); if (m_interpolation == i) return; m_interpolation = i; emit styleChanged(); }
void PlotTrace::setPointShape(PointShape sh) { sh = PointShape(std::clamp(int(sh), 0, int(X))); if (m_pointShape == sh) return; m_pointShape = sh; emit styleChanged(); }
void PlotTrace::setFillColor(const QColor &c) { if (m_fillColor == c) return; m_fillColor = c; emit styleChanged(); }
void PlotTrace::resetFillColor() { setFillColor(QColor()); }
void PlotTrace::setFillGradient(bool on) { if (m_fillGradient == on) return; m_fillGradient = on; emit styleChanged(); }

void PlotTrace::setUserStyle(TraceStyle *style)
{
    if (m_userStyle == style)
        return;
    if (m_userStyle)
        disconnect(m_userStyle, nullptr, this, nullptr);
    m_userStyle = style;
    if (style)
        connect(style, &TraceStyle::changed, this, &PlotTrace::styleChanged);
    emit userStyleChanged();
    emit styleChanged();
}

PlotTrace::Appearance PlotTrace::appearance() const
{
    Appearance a;
    a.color = m_color;
    a.fillColor = m_fillColor.isValid() ? m_fillColor : m_color;
    a.width = m_width;
    a.style = m_style;
    a.lineVisible = m_lineVisible;
    a.pointsVisible = m_pointsVisible;
    a.pointShape = m_pointShape;
    a.pointSize = m_pointSize;
    a.interpolation = m_interpolation;
    a.fill = m_fill;
    a.fillBaseline = m_fillBaseline;
    a.fillOpacity = m_fillOpacity;
    a.fillGradient = m_fillGradient;
    const TraceStyle *ts = m_userStyle.data();
    if (!ts)
        return a;
    if (ts->lineColor().isValid())
        a.color = ts->lineColor();
    a.fillColor = ts->fillColor().isValid() ? ts->fillColor() : a.color;
    a.width = ts->lineWidth() > 0 ? ts->lineWidth() : m_width;
    static_assert(int(TraceStyle::DashDotDot) == int(PlotElement::DashDotDot));
    a.style = PlotElement::LineStyle(ts->lineStyle());
    a.pointsOnly = ts->interpolation() == TraceStyle::PointsOnly || !(ts->lineWidth() > 0);
    a.lineVisible = !a.pointsOnly;
    // PointStyle (NoPoints first) maps onto PointShape.
    static constexpr PointShape shapes[] = {FilledCircle, Circle,        FilledCircle, Square, FilledSquare,
                                            Diamond,      FilledDiamond, Cross,        Plus,   X};
    a.pointsVisible = ts->pointStyle() != TraceStyle::NoPoints || a.pointsOnly;
    a.pointShape = shapes[std::clamp(int(ts->pointStyle()), 0, int(std::size(shapes)) - 1)];
    a.pointSize = std::max(5.0, (ts->lineWidth() > 0 ? ts->lineWidth() : 1.5) * 2.0 + 3.0);
    switch (ts->interpolation()) {
    case TraceStyle::Step: a.interpolation = Step; break;
    case TraceStyle::StepVertical: a.interpolation = StepVertical; break;
    case TraceStyle::CenterStep: a.interpolation = CenterStep; break;
    case TraceStyle::CenterStepVertical: a.interpolation = CenterStepVertical; break;
    default: a.interpolation = Linear; break;
    }
    a.fill = ts->fillTo() != TraceStyle::NoFill;
    a.fillBaseline = ts->fillTo() == TraceStyle::Zero           ? toDisplayY(0.0)
                   : ts->fillTo() == TraceStyle::PlusInfinity  ? qInf()
                   : ts->fillTo() == TraceStyle::MinusInfinity ? -qInf()
                                                                : m_fillBaseline;
    if (a.fill) {
        a.fillOpacity = 0.5;
        a.fillGradient = false;
    }
    return a;
}
void PlotTrace::setFill(bool on) { if (m_fill == on) return; m_fill = on; emit styleChanged(); }
void PlotTrace::setFillBaseline(double v) { if (m_fillBaseline == v || (std::isnan(v) && std::isnan(m_fillBaseline))) return; m_fillBaseline = v; emit styleChanged(); }
void PlotTrace::setFillOpacity(double o) { o = std::clamp(o, 0.0, 1.0); if (m_fillOpacity == o) return; m_fillOpacity = o; emit styleChanged(); }
void PlotTrace::setXScale(double s) { if (!std::isfinite(s) || s == 0 || m_xScale == s) return; m_xScale = s; emit transformChanged(); }
void PlotTrace::setXOffset(double o) { if (!std::isfinite(o) || m_xOffset == o) return; m_xOffset = o; emit transformChanged(); }
void PlotTrace::setYScale(double s) { if (!std::isfinite(s) || s == 0 || m_yScale == s) return; m_yScale = s; emit transformChanged(); }
void PlotTrace::setYOffset(double o) { if (!std::isfinite(o) || m_yOffset == o) return; m_yOffset = o; emit transformChanged(); }
void PlotTrace::setPersistence(int ms) { ms = std::clamp(ms, 0, 60000); if (m_persistence == ms) return; m_persistence = ms; emit styleChanged(); }
void PlotTrace::setLegendVisible(bool on) { if (m_legendVisible == on) return; m_legendVisible = on; emit styleChanged(); }

double PlotTrace::valueAt(double x) const
{
    return m_buffer ? m_buffer->valueAt(x) : qQNaN();
}

QPointF PlotTrace::nearestSample(double x) const
{
    return m_buffer ? m_buffer->nearestSample(x) : QPointF(qQNaN(), qQNaN());
}

// ---------------------------------------------------------------------------------------------
// PlotCursor
// ---------------------------------------------------------------------------------------------

PlotCursor::PlotCursor(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotCursor::cursorChanged, this, &PlotElement::changed);
    connect(this, &PlotCursor::positionChanged, this, &PlotElement::changed);
    connect(this, &PlotCursor::positionChanged, this, &PlotCursor::valueChanged);
}

void PlotCursor::setOrientation(Orientation o) { if (m_orientation == o) return; m_orientation = o; emit cursorChanged(); emit valueChanged(); }
void PlotCursor::setPosition(double p) { if (!std::isfinite(p) || m_position == p) return; m_position = p; emit positionChanged(); }
void PlotCursor::setColor(const QColor &c) { if (m_color == c) return; m_color = c; emit cursorChanged(); }
void PlotCursor::setLabel(const QString &l) { if (m_label == l) return; m_label = l; emit cursorChanged(); }
void PlotCursor::setDraggable(bool on) { if (m_draggable == on) return; m_draggable = on; emit cursorChanged(); }
void PlotCursor::setSnap(bool on) { if (m_snap == on) return; m_snap = on; emit cursorChanged(); }
void PlotCursor::setReadoutVisible(bool on) { if (m_readoutVisible == on) return; m_readoutVisible = on; emit cursorChanged(); }
void PlotCursor::setLineVisible(bool on) { if (m_lineVisible == on) return; m_lineVisible = on; emit cursorChanged(); }
void PlotCursor::setLineWidth(qreal width)
{
    width = std::max<qreal>(0.5, width);
    if (qFuzzyCompare(m_lineWidth, width))
        return;
    m_lineWidth = width;
    emit cursorChanged();
}
void PlotCursor::setStyle(LineStyle s) { if (m_style == s) return; m_style = s; emit cursorChanged(); }
void PlotCursor::setDragging(bool on) { if (m_dragging == on) return; m_dragging = on; emit draggingChanged(); notifyChanged(); }
void PlotCursor::setSelected(bool on) { if (m_selected == on) return; m_selected = on; emit selectedChanged(); notifyChanged(); }

void PlotCursor::setTrace(PlotTrace *t)
{
    if (m_trace == t)
        return;
    if (m_trace)
        disconnect(m_trace, nullptr, this, nullptr);
    m_trace = t;
    if (t) {
        connect(t, &PlotTrace::dataChanged, this, &PlotCursor::valueChanged);
        connect(t, &PlotTrace::transformChanged, this, &PlotCursor::valueChanged);
    }
    emit cursorChanged();
    emit valueChanged();
}

double PlotCursor::value() const
{
    if (m_orientation == Horizontal)
        return m_trace ? m_trace->fromDisplayY(m_position) : m_position;
    if (!m_trace)
        return qQNaN();
    return m_trace->valueAt(m_trace->fromDisplayX(m_position));
}

// ---------------------------------------------------------------------------------------------
// PlotLine
// ---------------------------------------------------------------------------------------------

PlotLine::PlotLine(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotLine::lineChanged, this, &PlotElement::changed);
    connect(this, &PlotLine::valueChanged, this, &PlotElement::changed);
}

void PlotLine::setOrientation(Orientation o) { if (m_orientation == o) return; m_orientation = o; emit lineChanged(); }
void PlotLine::setValue(double v) { if (!std::isfinite(v) || m_value == v) return; m_value = v; emit valueChanged(); }
void PlotLine::setColor(const QColor &c) { if (m_color == c) return; m_color = c; emit lineChanged(); }
void PlotLine::setWidth(double w) { w = std::clamp(w, 0.25, 16.0); if (m_width == w) return; m_width = w; emit lineChanged(); }
void PlotLine::setStyle(LineStyle s) { if (m_style == s) return; m_style = s; emit lineChanged(); }
void PlotLine::setDraggable(bool on) { if (m_draggable == on) return; m_draggable = on; emit lineChanged(); }
void PlotLine::setHandle(Handle h) { if (m_handle == h) return; m_handle = h; emit lineChanged(); }
void PlotLine::setLabel(const QString &l) { if (m_label == l) return; m_label = l; emit lineChanged(); }
void PlotLine::setLineMode(LineMode m) { if (m_lineMode == m) return; m_lineMode = m; emit lineChanged(); }
void PlotLine::setDragging(bool on) { if (m_dragging == on) return; m_dragging = on; emit draggingChanged(); notifyChanged(); }

void PlotLine::setTrace(PlotTrace *t)
{
    if (m_trace == t)
        return;
    if (m_trace)
        disconnect(m_trace, nullptr, this, nullptr);
    m_trace = t;
    if (t)
        connect(t, &PlotTrace::transformChanged, this, &PlotElement::changed);
    emit lineChanged();
}

double PlotLine::displayValue() const
{
    if (!m_trace)
        return m_value;
    return m_orientation == Horizontal ? m_trace->toDisplayY(m_value) : m_trace->toDisplayX(m_value);
}

double PlotLine::fromDisplay(double display) const
{
    if (!m_trace)
        return display;
    return m_orientation == Horizontal ? m_trace->fromDisplayY(display) : m_trace->fromDisplayX(display);
}

// ---------------------------------------------------------------------------------------------
// PlotMarker
// ---------------------------------------------------------------------------------------------

PlotMarker::PlotMarker(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotMarker::markerChanged, this, &PlotElement::changed);
}

void PlotMarker::setX(double v) { if (m_x == v) return; m_x = v; emit markerChanged(); }
void PlotMarker::setY(double v) { if (m_y == v || (std::isnan(v) && std::isnan(m_y))) return; m_y = v; emit markerChanged(); }
void PlotMarker::setLabel(const QString &l) { if (m_label == l) return; m_label = l; emit markerChanged(); }
void PlotMarker::setColor(const QColor &c) { if (m_color == c) return; m_color = c; emit markerChanged(); }
void PlotMarker::resetColor() { setColor(QColor()); }
void PlotMarker::setShape(Shape s) { if (m_shape == s) return; m_shape = s; emit markerChanged(); }
void PlotMarker::setSize(double s) { s = std::clamp(s, 2.0, 64.0); if (m_size == s) return; m_size = s; emit markerChanged(); }
void PlotMarker::setLabelPosition(LabelPosition p) { if (m_labelPosition == p) return; m_labelPosition = p; emit markerChanged(); }
void PlotMarker::setGuides(bool on) { if (m_guides == on) return; m_guides = on; emit markerChanged(); }

void PlotMarker::setTrace(PlotTrace *t)
{
    if (m_trace == t)
        return;
    if (m_trace)
        disconnect(m_trace, nullptr, this, nullptr);
    m_trace = t;
    if (t) {
        connect(t, &PlotTrace::dataChanged, this, &PlotElement::changed);
        connect(t, &PlotTrace::transformChanged, this, &PlotElement::changed);
        connect(t, &PlotTrace::styleChanged, this, &PlotElement::changed);
    }
    emit markerChanged();
}

double PlotMarker::displayY() const
{
    if (!std::isnan(m_y) || !m_trace)
        return m_y;
    return m_trace->toDisplayY(m_trace->valueAt(m_trace->fromDisplayX(m_x)));
}

// ---------------------------------------------------------------------------------------------
// PlotRegion
// ---------------------------------------------------------------------------------------------

PlotRegion::PlotRegion(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotRegion::regionChanged, this, &PlotElement::changed);
}

void PlotRegion::setOrientation(Orientation o) { if (m_orientation == o) return; m_orientation = o; emit regionChanged(); }
void PlotRegion::setFrom(double v) { if (!std::isfinite(v) || m_from == v) return; m_from = v; emit regionChanged(); }
void PlotRegion::setTo(double v) { if (!std::isfinite(v) || m_to == v) return; m_to = v; emit regionChanged(); }
void PlotRegion::setColor(const QColor &c) { if (m_color == c) return; m_color = c; emit regionChanged(); }
void PlotRegion::setLabel(const QString &l) { if (m_label == l) return; m_label = l; emit regionChanged(); }
void PlotRegion::setEdgesVisible(bool on) { if (m_edgesVisible == on) return; m_edgesVisible = on; emit regionChanged(); }
void PlotRegion::setDraggable(bool on) { if (m_draggable == on) return; m_draggable = on; emit regionChanged(); }
void PlotRegion::setResizable(bool on) { if (m_resizable == on) return; m_resizable = on; emit regionChanged(); }
void PlotRegion::setDragging(bool on) { if (m_dragging == on) return; m_dragging = on; emit draggingChanged(); notifyChanged(); }

// ---------------------------------------------------------------------------------------------
// PlotLogicTrace
// ---------------------------------------------------------------------------------------------

PlotLogicTrace::PlotLogicTrace(QObject *parent) : PlotElement(parent)
{
    connect(this, &PlotLogicTrace::styleChanged, this, &PlotElement::changed);
}

void PlotLogicTrace::setBuffer(LogicBuffer *buffer)
{
    if (m_buffer == buffer)
        return;
    if (m_buffer)
        disconnect(m_buffer, nullptr, this, nullptr);
    m_buffer = buffer;
    if (buffer) {
        connect(buffer, &LogicBuffer::changed, this, &PlotElement::changed);
        connect(buffer, &LogicBuffer::laneCountChanged, this, &PlotLogicTrace::styleChanged);
    }
    emit bufferChanged();
    notifyChanged();
}

void PlotLogicTrace::setLanes(const QList<int> &lanes) { if (m_lanes == lanes) return; m_lanes = lanes; emit styleChanged(); }
void PlotLogicTrace::setLaneNames(const QStringList &names) { if (m_laneNames == names) return; m_laneNames = names; emit styleChanged(); }
void PlotLogicTrace::setLaneColors(const QList<QColor> &colors) { if (m_laneColors == colors) return; m_laneColors = colors; emit styleChanged(); }
void PlotLogicTrace::setColor(const QColor &c) { if (m_color == c) return; m_color = c; emit styleChanged(); }
void PlotLogicTrace::setFirstRow(double r) { if (m_firstRow == r) return; m_firstRow = r; emit styleChanged(); }
void PlotLogicTrace::setLaneHeight(double h) { h = std::clamp(h, 0.05, 1.0); if (m_laneHeight == h) return; m_laneHeight = h; emit styleChanged(); }
void PlotLogicTrace::setWidth(double w) { w = std::clamp(w, 0.25, 8.0); if (m_width == w) return; m_width = w; emit styleChanged(); }
void PlotLogicTrace::setFillOpacity(double o) { o = std::clamp(o, 0.0, 1.0); if (m_fillOpacity == o) return; m_fillOpacity = o; emit styleChanged(); }
void PlotLogicTrace::setLabelsVisible(bool on) { if (m_labelsVisible == on) return; m_labelsVisible = on; emit styleChanged(); }

int PlotLogicTrace::rowCount() const
{
    if (!m_lanes.isEmpty())
        return int(m_lanes.size());
    return m_buffer ? m_buffer->laneCount() : 0;
}

int PlotLogicTrace::laneForRow(int row) const
{
    if (!m_lanes.isEmpty())
        return row >= 0 && row < m_lanes.size() ? m_lanes[row] : -1;
    return row;
}

QString PlotLogicTrace::nameForRow(int row) const
{
    if (row >= 0 && row < m_laneNames.size() && !m_laneNames[row].isEmpty())
        return m_laneNames[row];
    return QStringLiteral("D%1").arg(laneForRow(row));
}

QColor PlotLogicTrace::colorForRow(int row) const
{
    if (row >= 0 && row < m_laneColors.size() && m_laneColors[row].isValid())
        return m_laneColors[row];
    return m_color;
}

#undef MYBENCH_SET

}  // namespace mybench::plot
