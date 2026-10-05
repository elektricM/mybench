// Plot: properties, elements, mapping, view control, interaction and export.
// Rendering (layout, geometry, scene graph) is in plotrender.cpp.
#include "plot_p.h"

#include <QClipboard>
#include <QCursor>
#include <QFile>
#include <QGuiApplication>
#include <QNativeGestureEvent>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace mybench::plot {

// ---------------------------------------------------------------------------------------------
// PlotPrivate: style
// ---------------------------------------------------------------------------------------------

PlotPrivate::PlotPrivate(Plot *plot) : q(plot) {}

QColor PlotPrivate::color(const std::optional<QColor> &value, QRgb dark, QRgb light, int darkAlpha,
                          int lightAlpha) const
{
    if (value && value->isValid())
        return *value;
    QColor c = scheme == Plot::Dark ? QColor(dark) : QColor(light);
    c.setAlpha(scheme == Plot::Dark ? darkAlpha : lightAlpha);
    return c;
}

// Neutral fallbacks only: MyBench binds every role from its Theme (BenchPlot).
QColor PlotPrivate::backgroundColor() const { return color(background, 0x121418, 0xffffff); }
QColor PlotPrivate::areaColor() const { return color(plotAreaColor, 0x0b0d10, 0xfcfcfd); }
QColor PlotPrivate::gridColor() const { return color(grid, 0xffffff, 0x0b1220, 26, 30); }
QColor PlotPrivate::minorGridColor() const { return color(minorGrid, 0xffffff, 0x0b1220, 11, 13); }
QColor PlotPrivate::frameColor() const { return color(frame, 0xffffff, 0x0b1220, 40, 48); }
QColor PlotPrivate::textColor() const { return color(text, 0x8e97a3, 0x5c6672); }
QColor PlotPrivate::titleColor() const { return color(title, 0xc5ccd6, 0x2a3038); }
QColor PlotPrivate::accentColor() const { return color(accent, 0x4c8dff, 0x2563eb); }
QColor PlotPrivate::panelColor() const { return color(panel, 0x1e1e1e, 0xffffff); }

QColor PlotPrivate::palette(int index) const
{
    if (!series.isEmpty()) {
        const int n = int(series.size());
        return series.at(((index % n) + n) % n);
    }
    // Fallback: green, cyan, amber, violet, then pink, blue, orange, lime.
    static constexpr QRgb dark[] = {0x3ddc84, 0x22d3ee, 0xfbbf24, 0xa78bfa, 0xf472b6, 0x60a5fa, 0xfb923c, 0xa3e635};
    static constexpr QRgb light[] = {0x16a34a, 0x0891b2, 0xd97706, 0x7c3aed, 0xdb2777, 0x2563eb, 0xea580c, 0x65a30d};
    const int i = ((index % 8) + 8) % 8;
    return QColor(scheme == Plot::Dark ? dark[i] : light[i]);
}

QColor PlotPrivate::traceColor(const PlotTrace *trace) const
{
    if (!trace)
        return palette(0);
    const QColor own = trace->appearance().color;
    if (own.isValid())
        return own;
    int index = 0;
    for (const Entry &e : elements) {
        if (e.element == trace)
            break;
        if (qobject_cast<PlotTrace *>(e.element.data()))
            ++index;
    }
    return palette(index);
}

QFont PlotPrivate::labelFont() const
{
    QFont f = font;
    f.setFeature(QFont::Tag("tnum"), 1);
    return f;
}

QFont PlotPrivate::chipFont() const
{
    QFont f = labelFont();
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont PlotPrivate::titleFont() const
{
    QFont f = font;
    f.setWeight(QFont::Medium);
    return f;
}

void PlotPrivate::sortElements()
{
    std::stable_sort(elements.begin(), elements.end(), [](const Entry &a, const Entry &b) {
        const double za = a.element ? a.element->z() : 0, zb = b.element ? b.element->z() : 0;
        if (za != zb)
            return za < zb;
        return a.order < b.order;
    });
}

// ---------------------------------------------------------------------------------------------
// Plot: construction, axes, style properties
// ---------------------------------------------------------------------------------------------

Plot::Plot(QQuickItem *parent) : QQuickItem(parent), d(std::make_unique<PlotPrivate>(this))
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(false);
    setClip(false);
    d->font = QGuiApplication::font();
    d->font.setPixelSize(11);
    d->ownX = new PlotAxis(this);
    d->ownY = new PlotAxis(this);
    d->ownY->setTickSpacing(56);
    d->xAxis = d->ownX;
    d->yAxis = d->ownY;
    connectAxis(d->ownX);
    connectAxis(d->ownY);
    connect(this, &Plot::styleChanged, this, &Plot::requestUpdate);
    connect(this, &Plot::marginsChanged, this, &Plot::requestUpdate);
    connect(this, &Plot::interactionChanged, this, &Plot::requestUpdate);
}

Plot::~Plot()
{
    for (auto &e : d->elements) {
        if (e.element)
            disconnect(e.element, nullptr, this, nullptr);
    }
}

void Plot::requestUpdate()
{
    polish();
    update();
}

void Plot::connectAxis(PlotAxis *axis)
{
    if (!axis)
        return;
    connect(axis, &PlotAxis::rangeChanged, this, &Plot::requestUpdate);
    connect(axis, &PlotAxis::scaleChanged, this, &Plot::requestUpdate);
    connect(axis, &PlotAxis::appearanceChanged, this, &Plot::requestUpdate);
    connect(axis, &PlotAxis::autoScaleChanged, this, &Plot::requestUpdate);
    connect(axis, &PlotAxis::followChanged, this, &Plot::requestUpdate);
    // A user zoom/pan of an axis ends its autoscale lock.
    connect(axis, &PlotAxis::viewChangedByUser, this, [this, axis] { d->unlock(axis == d->xAxis, axis == d->yAxis); });
}

PlotAxis *Plot::xAxis() const { return d->xAxis; }
PlotAxis *Plot::yAxis() const { return d->yAxis; }

void Plot::setXAxis(PlotAxis *axis)
{
    if (!axis)
        axis = d->ownX;
    if (d->xAxis == axis)
        return;
    if (d->xAxis)
        disconnect(d->xAxis, nullptr, this, nullptr);
    d->xAxis = axis;
    connectAxis(axis);
    emit xAxisChanged();
    requestUpdate();
}

void Plot::setYAxis(PlotAxis *axis)
{
    if (!axis)
        axis = d->ownY;
    if (d->yAxis == axis)
        return;
    if (d->yAxis)
        disconnect(d->yAxis, nullptr, this, nullptr);
    d->yAxis = axis;
    connectAxis(axis);
    emit yAxisChanged();
    requestUpdate();
}

Plot::ColorScheme Plot::colorScheme() const { return d->scheme; }
void Plot::setColorScheme(ColorScheme scheme)
{
    if (d->scheme == scheme)
        return;
    d->scheme = scheme;
    d->staticDirty = true;
    emit styleChanged();
}

#define MYBENCH_PLOT_COLOR_IMPL(name, Name, member)                                                \
    QColor Plot::name() const { return d->name(); }                                                \
    void Plot::set##Name(const QColor &c)                                                          \
    {                                                                                              \
        if (d->member && *d->member == c)                                                          \
            return;                                                                                \
        d->member = c;                                                                             \
        d->staticDirty = true;                                                                     \
        emit styleChanged();                                                                       \
    }                                                                                              \
    void Plot::reset##Name()                                                                       \
    {                                                                                              \
        if (!d->member)                                                                            \
            return;                                                                                \
        d->member.reset();                                                                         \
        d->staticDirty = true;                                                                     \
        emit styleChanged();                                                                       \
    }

MYBENCH_PLOT_COLOR_IMPL(backgroundColor, BackgroundColor, background)
MYBENCH_PLOT_COLOR_IMPL(gridColor, GridColor, grid)
MYBENCH_PLOT_COLOR_IMPL(minorGridColor, MinorGridColor, minorGrid)
MYBENCH_PLOT_COLOR_IMPL(frameColor, FrameColor, frame)
MYBENCH_PLOT_COLOR_IMPL(textColor, TextColor, text)
MYBENCH_PLOT_COLOR_IMPL(titleColor, TitleColor, title)
MYBENCH_PLOT_COLOR_IMPL(accentColor, AccentColor, accent)
MYBENCH_PLOT_COLOR_IMPL(panelColor, PanelColor, panel)
#undef MYBENCH_PLOT_COLOR_IMPL

QList<QColor> Plot::seriesColors() const { return d->series; }
void Plot::setSeriesColors(const QList<QColor> &colors)
{
    if (d->series == colors)
        return;
    d->series = colors;
    d->staticDirty = true;
    emit styleChanged();
}
void Plot::resetSeriesColors()
{
    if (d->series.isEmpty())
        return;
    d->series.clear();
    d->staticDirty = true;
    emit styleChanged();
}

QColor Plot::plotAreaColor() const { return d->areaColor(); }
void Plot::setPlotAreaColor(const QColor &c)
{
    if (d->plotAreaColor && *d->plotAreaColor == c)
        return;
    d->plotAreaColor = c;
    d->staticDirty = true;
    emit styleChanged();
}
void Plot::resetPlotAreaColor()
{
    if (!d->plotAreaColor)
        return;
    d->plotAreaColor.reset();
    d->staticDirty = true;
    emit styleChanged();
}

Plot::Graticule Plot::graticule() const { return d->graticule; }
void Plot::setGraticule(Graticule g) { if (d->graticule == g) return; d->graticule = g; d->staticDirty = true; emit styleChanged(); }
PlotElement::LineStyle Plot::gridStyle() const { return d->gridStyle; }
void Plot::setGridStyle(PlotElement::LineStyle s) { if (d->gridStyle == s) return; d->gridStyle = s; d->staticDirty = true; emit styleChanged(); }
bool Plot::frameVisible() const { return d->frameVisible; }
void Plot::setFrameVisible(bool on) { if (d->frameVisible == on) return; d->frameVisible = on; d->staticDirty = true; emit styleChanged(); }
QFont Plot::font() const { return d->font; }
void Plot::setFont(const QFont &f)
{
    if (d->font == f)
        return;
    d->font = f;
    d->fontSet = true;
    d->layoutCache.clear();
    d->staticDirty = true;
    emit styleChanged();
}
bool Plot::xLabelsVisible() const { return d->xLabels; }
void Plot::setXLabelsVisible(bool on) { if (d->xLabels == on) return; d->xLabels = on; d->staticDirty = true; emit styleChanged(); }
bool Plot::yLabelsVisible() const { return d->yLabels; }
void Plot::setYLabelsVisible(bool on) { if (d->yLabels == on) return; d->yLabels = on; d->staticDirty = true; emit styleChanged(); }
QString Plot::placeholderText() const { return d->placeholder; }
void Plot::setPlaceholderText(const QString &t) { if (d->placeholder == t) return; d->placeholder = t; emit styleChanged(); }

qreal Plot::leftMargin() const { return d->margins[0]; }
void Plot::setLeftMargin(qreal m) { if (d->margins[0] == m) return; d->margins[0] = m; d->staticDirty = true; emit marginsChanged(); }
qreal Plot::rightMargin() const { return d->margins[1]; }
void Plot::setRightMargin(qreal m) { if (d->margins[1] == m) return; d->margins[1] = m; d->staticDirty = true; emit marginsChanged(); }
qreal Plot::topMargin() const { return d->margins[2]; }
void Plot::setTopMargin(qreal m) { if (d->margins[2] == m) return; d->margins[2] = m; d->staticDirty = true; emit marginsChanged(); }
qreal Plot::bottomMargin() const { return d->margins[3]; }
void Plot::setBottomMargin(qreal m) { if (d->margins[3] == m) return; d->margins[3] = m; d->staticDirty = true; emit marginsChanged(); }
QRectF Plot::plotArea() const { return d->area; }
qreal Plot::implicitLeftMargin() const { return d->implicitLeft; }

bool Plot::interactive() const { return d->interactive; }
void Plot::setInteractive(bool on) { if (d->interactive == on) return; d->interactive = on; emit interactionChanged(); }
Plot::DragMode Plot::dragMode() const { return d->dragMode; }
void Plot::setDragMode(DragMode m) { if (d->dragMode == m) return; d->dragMode = m; emit interactionChanged(); }
bool Plot::hoverReadout() const { return d->hoverReadout; }
void Plot::setHoverReadout(bool on) { if (d->hoverReadout == on) return; d->hoverReadout = on; emit interactionChanged(); }
bool Plot::cursorDeltaVisible() const { return d->cursorDeltaVisible; }
void Plot::setCursorDeltaVisible(bool on) { if (d->cursorDeltaVisible == on) return; d->cursorDeltaVisible = on; emit interactionChanged(); }
bool Plot::legendVisible() const { return d->legendVisible; }
void Plot::setLegendVisible(bool on) { if (d->legendVisible == on) return; d->legendVisible = on; emit styleChanged(); }
Plot::Corner Plot::legendPosition() const { return d->legendPosition; }
void Plot::setLegendPosition(Corner c) { if (d->legendPosition == c) return; d->legendPosition = c; emit styleChanged(); }
Plot::ZoomAxes Plot::zoomAxes() const { return d->zoomAxes; }
void Plot::setZoomAxes(ZoomAxes axes) { if (d->zoomAxes == axes) return; d->zoomAxes = axes; emit interactionChanged(); }
bool Plot::canUndoZoom() const { return !d->zoomHistory.empty(); }
bool Plot::xAutoscaleLock() const { return d->xLock; }
void Plot::setXAutoscaleLock(bool on)
{
    if (d->xLock == on)
        return;
    d->xLock = on;
    emit autoscaleLockChanged();
    requestUpdate();
}
bool Plot::yAutoscaleLock() const { return d->yLock; }
void Plot::setYAutoscaleLock(bool on)
{
    if (d->yLock == on)
        return;
    d->yLock = on;
    emit autoscaleLockChanged();
    requestUpdate();
}
bool Plot::containsMouse() const { return d->hovering; }
QPointF Plot::hoverValue() const
{
    if (!d->hovering || !d->mapsValid())
        return QPointF(qQNaN(), qQNaN());
    return mapToValue(d->hoverPos);
}

Plot::FrameStats Plot::frameStats() const { return d->stats; }

// ---------------------------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------------------------

void Plot::addElement(PlotElement *element)
{
    if (!element)
        return;
    for (const auto &e : d->elements) {
        if (e.element == element)
            return;
    }
    // Declaration order: index among this plot's children, else attach order after them.
    quint64 order = (quint64(1) << 32) + d->attachCounter++;
    const int childIndex = int(children().indexOf(element));
    if (childIndex >= 0)
        order = quint64(childIndex);
    d->elements.push_back({element, order});
    d->sortElements();
    connect(element, &PlotElement::changed, this, &Plot::requestUpdate);
    connect(element, &PlotElement::visibleChanged, this, &Plot::requestUpdate);
    connect(element, &PlotElement::zChanged, this, [this] {
        d->sortElements();
        d->traceListChanged = true;
        requestUpdate();
    });
    if (element->plot() != this)
        element->setPlot(this);
    d->traceListChanged = true;
    requestUpdate();
}

void Plot::removeElement(PlotElement *element)
{
    auto it = std::find_if(d->elements.begin(), d->elements.end(),
                           [element](const PlotPrivate::Entry &e) { return e.element == element; });
    if (it == d->elements.end())
        return;
    d->elements.erase(it);
    if (element)
        disconnect(element, nullptr, this, nullptr);
    if (d->dragElement == element)
        d->drag = PlotPrivate::Drag::None;
    d->traceListChanged = true;
    requestUpdate();
}

QList<PlotElement *> Plot::elements() const
{
    QList<PlotElement *> out;
    for (const auto &e : d->elements) {
        if (e.element)
            out.append(e.element);
    }
    return out;
}

QColor Plot::traceColor(PlotTrace *trace) const { return d->traceColor(trace); }
QColor Plot::paletteColor(int index) const { return d->palette(index); }

void Plot::componentComplete()
{
    QQuickItem::componentComplete();
    // Elements declared as children attach in their own componentComplete; catch any that
    // completed before us (children complete first) and fix the declaration order.
    for (QObject *child : children()) {
        if (auto *e = qobject_cast<PlotElement *>(child)) {
            if (!e->plot())
                e->setPlot(this);
        }
    }
    for (auto &e : d->elements) {
        const int idx = int(children().indexOf(e.element.data()));
        if (idx >= 0)
            e.order = quint64(idx);
    }
    d->sortElements();
    requestUpdate();
}

void Plot::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        d->staticDirty = true;
        requestUpdate();
    }
}

void Plot::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) {
        d->staticDirty = true;
        requestUpdate();
    }
}

// ---------------------------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------------------------

QPointF Plot::mapToValue(QPointF point) const { return QPointF(pixelToX(point.x()), pixelToY(point.y())); }
QPointF Plot::mapFromValue(QPointF value) const { return QPointF(xToPixel(value.x()), yToPixel(value.y())); }

double Plot::xToPixel(double x) const
{
    if (!d->xAxis)
        return qQNaN();
    const QRectF a = d->area.isValid() ? d->area : boundingRect();
    return d->xAxis->map(a.left(), a.width()).toPixel(x);
}

double Plot::yToPixel(double y) const
{
    if (!d->yAxis)
        return qQNaN();
    const QRectF a = d->area.isValid() ? d->area : boundingRect();
    return d->yAxis->map(a.bottom(), -a.height()).toPixel(y);
}

double Plot::pixelToX(double px) const
{
    if (!d->xAxis)
        return qQNaN();
    const QRectF a = d->area.isValid() ? d->area : boundingRect();
    return d->xAxis->map(a.left(), a.width()).fromPixel(px);
}

double Plot::pixelToY(double py) const
{
    if (!d->yAxis)
        return qQNaN();
    const QRectF a = d->area.isValid() ? d->area : boundingRect();
    return d->yAxis->map(a.bottom(), -a.height()).fromPixel(py);
}

QString Plot::format(double value, const QString &unit, int digits) const
{
    return formatEngineering(value, unit, digits);
}

// ---------------------------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------------------------

bool PlotPrivate::dataXRange(double *lo, double *hi) const
{
    double a = std::numeric_limits<double>::infinity(), b = -a;
    for (PlotTrace *t : elementsOf<PlotTrace>()) {
        if (!t->isVisible() || !t->buffer())
            continue;
        const TraceSnapshot s = t->buffer()->snapshot();
        if (s->empty())
            continue;
        double x0 = t->toDisplayX(s->xFirst()), x1 = t->toDisplayX(s->xLast());
        if (!s->sorted) {
            x0 = std::numeric_limits<double>::infinity();
            x1 = -x0;
            for (size_t i = 0; i < s->size(); ++i) {
                const double v = t->toDisplayX(s->xAt(i));
                x0 = std::min(x0, v);
                x1 = std::max(x1, v);
            }
        }
        if (xAxis && xAxis->isLog() && s->sorted && !(x0 > 0) && s->size() > 1)
            x0 = t->toDisplayX(s->xAt(1));
        a = std::min({a, x0, x1});
        b = std::max({b, x0, x1});
    }
    for (PlotLogicTrace *t : elementsOf<PlotLogicTrace>()) {
        if (!t->isVisible() || !t->buffer())
            continue;
        const LogicSnapshot s = t->buffer()->snapshot();
        if (s->empty())
            continue;
        a = std::min(a, s->startX);
        b = std::max(b, s->endX);
    }
    if (!(a <= b))
        return false;
    *lo = a;
    *hi = b;
    return true;
}

bool PlotPrivate::dataYRange(double xLo, double xHi, double *lo, double *hi) const
{
    double a = std::numeric_limits<double>::infinity(), b = -a;
    for (PlotTrace *t : elementsOf<PlotTrace>()) {
        if (!t->isVisible() || !t->buffer())
            continue;
        const TraceSnapshot s = t->buffer()->snapshot();
        double y0, y1;
        const double d0 = t->fromDisplayX(xLo), d1 = t->fromDisplayX(xHi);
        if (!s->yRange(std::min(d0, d1), std::max(d0, d1), &y0, &y1))
            continue;
        y0 = t->toDisplayY(y0);
        y1 = t->toDisplayY(y1);
        if (yAxis && yAxis->isLog()) {
            if (!(std::max(y0, y1) > 0))
                continue;
            if (!(std::min(y0, y1) > 0))
                y0 = y1 = std::max(y0, y1);
        }
        a = std::min({a, y0, y1});
        b = std::max({b, y0, y1});
    }
    if (!(a <= b))
        return false;
    *lo = a;
    *hi = b;
    return true;
}

namespace {

// Linear axes: widen [*outLo, *outHi] symmetrically around `center` (the data centre) to at least
// the axis' minimumSpan, so near-constant data (a DMM trend) does not magnify noise to full height.
void enforceMinimumSpan(const PlotAxis *axis, double center, double *outLo, double *outHi)
{
    const double minSpan = axis->minimumSpan();
    if (axis->isLog() || !(minSpan > 0) || *outHi - *outLo >= minSpan)
        return;
    const double half = 0.5 * std::max(minSpan, 2.0 * std::max(*outHi - center, center - *outLo));
    *outLo = center - half;
    *outHi = center + half;
}

void paddedRange(const PlotAxis *axis, double lo, double hi, double margin, double *outLo, double *outHi)
{
    if (axis->isLog()) {
        if (!(lo > 0))
            lo = hi / 1000.0;
        const double l0 = std::log10(lo), l1 = std::log10(hi);
        const double pad = std::max(0.05, (l1 - l0) * margin);
        *outLo = std::pow(10.0, l0 - pad);
        *outHi = std::pow(10.0, l1 + pad);
        return;
    }
    double span = hi - lo;
    if (!(span > 0)) {
        span = std::max(std::abs(hi) * 0.2, 1e-9);
        lo -= span / 2;
        hi += span / 2;
        span = hi - lo;
    }
    *outLo = lo - span * margin;
    *outHi = hi + span * margin;
    enforceMinimumSpan(axis, 0.5 * (lo + hi), outLo, outHi);
}

}  // namespace

void PlotPrivate::fitX()
{
    double lo, hi;
    if (!xAxis || !dataXRange(&lo, &hi))
        return;
    if (!(hi > lo))
        hi = lo + 1;
    enforceMinimumSpan(xAxis, 0.5 * (lo + hi), &lo, &hi);
    xAxis->setViewRange(lo, hi);
}

void PlotPrivate::fitY()
{
    double lo, hi;
    if (!yAxis || !xAxis)
        return;
    if (!dataYRange(xAxis->min(), xAxis->max(), &lo, &hi))
        return;
    double a, b;
    paddedRange(yAxis, lo, hi, yAxis->autoScaleMargin(), &a, &b);
    yAxis->setViewRange(a, b);
}

void Plot::autoscaleX()
{
    d->pushHistory();
    d->fitX();
}

void Plot::autoscaleY()
{
    d->pushHistory();
    d->fitY();
}

void Plot::autoscale()
{
    d->pushHistory();
    d->fitX();
    d->fitY();
}

void PlotPrivate::applyAutoScale()
{
    // limitToData: the extent of all data bounds interactive pan/zoom.
    if (xAxis && xAxis->limitToData()) {
        double lo, hi;
        if (dataXRange(&lo, &hi))
            xAxis->setDataExtent(lo, hi);
    }
    if (yAxis && yAxis->limitToData()) {
        double lo, hi;
        if (dataYRange(-1e300, 1e300, &lo, &hi))
            yAxis->setDataExtent(lo, hi);
    }
    if (xLock) {
        fitX();
    } else if (xAxis && xAxis->follow()) {
        double lo, hi;
        if (dataXRange(&lo, &hi)) {
            const double span = xAxis->max() - xAxis->min();
            if (xAxis->isLog()) {
                const double f = xAxis->max() / xAxis->min();
                xAxis->applyRange(hi / f, hi, true);
            } else if (span > 0) {
                xAxis->applyRange(hi - span, hi, true);
            }
        }
    } else if (xAxis && xAxis->autoScale()) {
        double lo, hi;
        if (dataXRange(&lo, &hi) && hi > lo) {
            enforceMinimumSpan(xAxis, 0.5 * (lo + hi), &lo, &hi);
            xAxis->applyRange(lo, hi, true);
        }
    }
    if (yLock) {
        fitY();
    } else if (yAxis && yAxis->autoScale() && xAxis) {
        double lo, hi;
        if (dataYRange(xAxis->min(), xAxis->max(), &lo, &hi)) {
            double a, b;
            paddedRange(yAxis, lo, hi, yAxis->autoScaleMargin(), &a, &b);
            // Hysteresis: keep the range while the data stays inside and fills most of it.
            const double cur0 = yAxis->min(), cur1 = yAxis->max();
            const double curSpan = cur1 - cur0, newSpan = b - a;
            const bool inside = lo >= cur0 && hi <= cur1;
            if (!(inside && newSpan > curSpan * 0.7) || yAxis->isLog())
                yAxis->applyRange(a, b, true);
        }
    }
}

PlotPrivate::ViewState PlotPrivate::viewState() const
{
    return {xAxis ? xAxis->min() : 0.0, xAxis ? xAxis->max() : 0.0, yAxis ? yAxis->min() : 0.0,
            yAxis ? yAxis->max() : 0.0};
}

void PlotPrivate::pushHistory(bool coalesce)
{
    if (historyGuard > 0 || !xAxis || !yAxis)
        return;
    if (coalesce) {
        const bool burst = coalesceTimer.isValid() && coalesceTimer.elapsed() < 600;
        coalesceTimer.restart();
        if (burst)
            return;
    } else {
        coalesceTimer.invalidate();
    }
    const ViewState v = viewState();
    if (!zoomHistory.empty() && zoomHistory.back() == v)
        return;
    const bool wasEmpty = zoomHistory.empty();
    zoomHistory.push_back(v);
    if (zoomHistory.size() > 32)
        zoomHistory.erase(zoomHistory.begin());
    if (wasEmpty)
        emit q->zoomHistoryChanged();
}

void PlotPrivate::unlock(bool x, bool y)
{
    bool changed = false;
    if (x && xLock) {
        xLock = false;
        changed = true;
    }
    if (y && yLock) {
        yLock = false;
        changed = true;
    }
    if (changed)
        emit q->autoscaleLockChanged();
}

void Plot::undoZoom()
{
    if (d->zoomHistory.empty())
        return;
    const PlotPrivate::ViewState v = d->zoomHistory.back();
    d->zoomHistory.pop_back();
    d->coalesceTimer.invalidate();
    d->unlock(true, true);
    if (d->xAxis && d->xAxis->interactive()) {
        if (d->xAxis->follow())
            d->xAxis->setFollow(false);
        d->xAxis->setViewRange(v.x0, v.x1);
    }
    if (d->yAxis && d->yAxis->interactive())
        d->yAxis->setViewRange(v.y0, v.y1);
    if (d->zoomHistory.empty())
        emit zoomHistoryChanged();
    requestUpdate();
}

void Plot::clearZoomHistory()
{
    if (d->zoomHistory.empty())
        return;
    d->zoomHistory.clear();
    emit zoomHistoryChanged();
}

void Plot::resetView()
{
    d->pushHistory();
    d->unlock(true, true);
    if (d->xAxis && d->xAxis->interactive())
        d->xAxis->reset();
    if (d->yAxis && d->yAxis->interactive())
        d->yAxis->reset();
    emit viewReset();
}

void Plot::zoomAt(QPointF point, double factorX, double factorY)
{
    if ((factorX != 1.0 && d->xAxis && d->xAxis->interactive()) || (factorY != 1.0 && d->yAxis && d->yAxis->interactive()))
        d->pushHistory();
    if (factorX != 1.0 && d->xAxis && d->xAxis->interactive()) {
        double anchor = pixelToX(point.x());
        if (d->xAxis->follow()) {
            double lo, hi;
            if (d->dataXRange(&lo, &hi))
                anchor = hi;
        }
        d->xAxis->zoom(factorX, anchor);
    }
    if (factorY != 1.0 && d->yAxis && d->yAxis->interactive())
        d->yAxis->zoom(factorY, pixelToY(point.y()));
}

// ---------------------------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------------------------

const HitArea *PlotPrivate::hitTest(QPointF p) const
{
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (it->element && it->rect.contains(p))
            return &*it;
    }
    return nullptr;
}

double PlotPrivate::snapCursor(PlotCursor *cursor, double position) const
{
    PlotTrace *t = cursor->trace();
    if (!cursor->snap() || !t || cursor->orientation() != PlotElement::Vertical)
        return position;
    const QPointF s = t->nearestSample(t->fromDisplayX(position));
    return std::isfinite(s.x()) ? t->toDisplayX(s.x()) : position;
}

void Plot::mousePressEvent(QMouseEvent *event)
{
    const QPointF p = event->position();
    d->pressPos = d->lastPos = p;
    d->moved = false;
    d->drag = PlotPrivate::Drag::None;
    d->dragElement = nullptr;
    d->pressXMap = d->xMap;
    d->pressYMap = d->yMap;
    d->panPushed = false;

    if (event->button() == Qt::LeftButton) {
        if (const HitArea *hit = d->hitTest(p)) {
            PlotElement *e = hit->element;
            d->dragElement = e;
            if (auto *c = qobject_cast<PlotCursor *>(e)) {
                if (d->selected && d->selected != c)
                    if (auto *old = qobject_cast<PlotCursor *>(d->selected.data()))
                        old->setSelected(false);
                d->selected = c;
                c->setSelected(true);
                if (c->draggable()) {
                    d->drag = PlotPrivate::Drag::Cursor;
                    const double v = c->orientation() == PlotElement::Vertical ? pixelToX(p.x()) : pixelToY(p.y());
                    d->grabOffset = c->position() - v;
                    if (d->xAxis && d->xAxis->isLog() && c->orientation() == PlotElement::Vertical)
                        d->grabOffset = 0;
                    c->setDragging(true);
                }
            } else if (auto *l = qobject_cast<PlotLine *>(e)) {
                d->selected = l;
                if (l->draggable()) {
                    d->drag = PlotPrivate::Drag::Line;
                    const double v = l->orientation() == PlotElement::Horizontal ? pixelToY(p.y()) : pixelToX(p.x());
                    d->grabOffset = l->displayValue() - v;
                    l->setDragging(true);
                }
            } else if (auto *r = qobject_cast<PlotRegion *>(e)) {
                d->drag = hit->kind == HitArea::RegionFrom ? PlotPrivate::Drag::RegionFrom
                        : hit->kind == HitArea::RegionTo   ? PlotPrivate::Drag::RegionTo
                                                           : PlotPrivate::Drag::RegionBody;
                d->grabFrom = r->from();
                d->grabTo = r->to();
                r->setDragging(true);
            }
            if (d->drag != PlotPrivate::Drag::None) {
                setKeepMouseGrab(true);
                forceActiveFocus(Qt::MouseFocusReason);
                event->accept();
                requestUpdate();
                return;
            }
        }
    }

    if (!d->interactive || !d->area.contains(p)) {
        if (d->area.contains(p)) {
            forceActiveFocus(Qt::MouseFocusReason);
            event->accept();  // still report clicks
            return;
        }
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    const bool zoomDrag = event->button() == Qt::RightButton
                          || (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier))
                          || d->dragMode == Zoom;
    if (event->button() == Qt::MiddleButton || (!zoomDrag && d->dragMode == Pan))
        d->drag = PlotPrivate::Drag::Pan;
    else if (zoomDrag && d->dragMode != NoDrag) {
        d->drag = PlotPrivate::Drag::Zoom;
        d->dragAxes = d->zoomAxes;
    }
    if (d->drag == PlotPrivate::Drag::Pan)
        setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void Plot::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF p = event->position();
    if ((p - d->pressPos).manhattanLength() >= 3)
        d->moved = true;
    d->lastPos = p;
    PlotElement *e = d->dragElement;
    switch (d->drag) {
    case PlotPrivate::Drag::Cursor:
        if (auto *c = qobject_cast<PlotCursor *>(e)) {
            const bool vertical = c->orientation() == PlotElement::Vertical;
            const QRectF a = d->area;
            const double px = std::clamp(vertical ? p.x() : p.y(), vertical ? a.left() : a.top(),
                                         vertical ? a.right() : a.bottom());
            double v = (vertical ? pixelToX(px) : pixelToY(px)) + d->grabOffset;
            v = d->snapCursor(c, v);
            c->setPosition(v);
            emit c->moved(c->position());
        }
        break;
    case PlotPrivate::Drag::Line:
        if (auto *l = qobject_cast<PlotLine *>(e)) {
            const bool horizontal = l->orientation() == PlotElement::Horizontal;
            const QRectF a = d->area;
            const double px = std::clamp(horizontal ? p.y() : p.x(), horizontal ? a.top() : a.left(),
                                         horizontal ? a.bottom() : a.right());
            const double display = (horizontal ? pixelToY(px) : pixelToX(px)) + d->grabOffset;
            l->setValue(l->fromDisplay(display));
            emit l->moved(l->value());
        }
        break;
    case PlotPrivate::Drag::RegionBody:
    case PlotPrivate::Drag::RegionFrom:
    case PlotPrivate::Drag::RegionTo:
        if (auto *r = qobject_cast<PlotRegion *>(e)) {
            const bool vertical = r->orientation() == PlotElement::Vertical;
            const ScaleMap &m = vertical ? d->pressXMap : d->pressYMap;
            const double v0 = m.fromPixel(vertical ? d->pressPos.x() : d->pressPos.y());
            const double v1 = m.fromPixel(vertical ? p.x() : p.y());
            double from = r->from(), to = r->to();
            if (d->drag == PlotPrivate::Drag::RegionBody) {
                if (m.isLog()) {
                    const double f = v1 / v0;
                    from = d->grabFrom * f;
                    to = d->grabTo * f;
                } else {
                    from = d->grabFrom + (v1 - v0);
                    to = d->grabTo + (v1 - v0);
                }
                // Keep inside the axis range.
                const double lo = m.min, hi = m.max, span = to - from;
                if (!m.isLog() && span <= hi - lo) {
                    if (from < lo) {
                        from = lo;
                        to = lo + span;
                    }
                    if (to > hi) {
                        to = hi;
                        from = hi - span;
                    }
                }
            } else if (d->drag == PlotPrivate::Drag::RegionFrom) {
                from = std::min(v1, to);
            } else {
                to = std::max(v1, from);
            }
            r->setFrom(from);
            r->setTo(to);
            emit r->moved(r->from(), r->to());
        }
        break;
    case PlotPrivate::Drag::Pan: {
        const double dxPx = p.x() - d->pressPos.x();
        const double dyPx = p.y() - d->pressPos.y();
        if (!d->panPushed && (dxPx != 0 || dyPx != 0)) {
            d->pushHistory();
            d->panPushed = true;
        }
        if (d->xAxis && d->xAxis->interactive() && d->pressXMap.isValid()) {
            const double a = d->pressXMap.fromPixel(d->area.left() - dxPx);
            const double b = d->pressXMap.fromPixel(d->area.right() - dxPx);
            d->xAxis->setViewRange(a, b);
            if (d->xAxis->follow() && std::abs(dxPx) > 2)
                d->xAxis->setFollow(false);
            if (dxPx != 0)
                emit d->xAxis->viewChangedByUser();
        }
        if (d->yAxis && d->yAxis->interactive() && d->pressYMap.isValid()) {
            const double a = d->pressYMap.fromPixel(d->area.bottom() - dyPx);
            const double b = d->pressYMap.fromPixel(d->area.top() - dyPx);
            d->yAxis->setViewRange(a, b);
            if (dyPx != 0)
                emit d->yAxis->viewChangedByUser();
        }
        break;
    }
    case PlotPrivate::Drag::Zoom:
        d->rubberRect = QRectF(d->pressPos, p).normalized().intersected(d->area);
        // A near-horizontal or near-vertical drag zooms only that axis (axis lock).
        d->dragAxes = d->zoomAxes;
        if (d->dragAxes == ZoomXY) {
            const double ax = std::abs(p.x() - d->pressPos.x()), ay = std::abs(p.y() - d->pressPos.y());
            if (std::max(ax, ay) >= 12) {
                if (ay < 0.3 * ax)
                    d->dragAxes = ZoomX;
                else if (ax < 0.3 * ay)
                    d->dragAxes = ZoomY;
            }
        }
        if (d->dragAxes == ZoomX)
            d->rubberRect = QRectF(d->rubberRect.left(), d->area.top(), d->rubberRect.width(), d->area.height());
        else if (d->dragAxes == ZoomY)
            d->rubberRect = QRectF(d->area.left(), d->rubberRect.top(), d->area.width(), d->rubberRect.height());
        requestUpdate();
        break;
    case PlotPrivate::Drag::None:
        break;
    }
    event->accept();
}

void Plot::mouseReleaseEvent(QMouseEvent *event)
{
    const QPointF p = event->position();
    const PlotPrivate::Drag drag = d->drag;
    d->drag = PlotPrivate::Drag::None;
    setKeepMouseGrab(false);
    if (auto *c = qobject_cast<PlotCursor *>(d->dragElement.data()))
        c->setDragging(false);
    if (auto *l = qobject_cast<PlotLine *>(d->dragElement.data()))
        l->setDragging(false);
    if (auto *r = qobject_cast<PlotRegion *>(d->dragElement.data()))
        r->setDragging(false);
    d->dragElement = nullptr;
    unsetCursor();

    if (drag == PlotPrivate::Drag::Zoom) {
        const QRectF r = d->rubberRect;
        d->rubberRect = QRectF();
        const bool zx = d->dragAxes != ZoomY && r.width() > 4 && d->xAxis && d->xAxis->interactive();
        const bool zy = d->dragAxes != ZoomX && r.height() > 4 && d->yAxis && d->yAxis->interactive();
        if (zx || zy)
            d->pushHistory();
        if (zx) {
            d->xAxis->setViewRange(pixelToX(r.left()), pixelToX(r.right()));
            if (d->xAxis->follow())
                d->xAxis->setFollow(false);
            emit d->xAxis->viewChangedByUser();
        }
        if (zy) {
            d->yAxis->setViewRange(pixelToY(r.bottom()), pixelToY(r.top()));
            emit d->yAxis->viewChangedByUser();
        }
        requestUpdate();
    }
    if (!d->moved && d->area.contains(p) && event->button() == Qt::LeftButton
        && (drag == PlotPrivate::Drag::Pan || drag == PlotPrivate::Drag::None || drag == PlotPrivate::Drag::Zoom)) {
        if (d->selected) {
            if (auto *c = qobject_cast<PlotCursor *>(d->selected.data()))
                c->setSelected(false);
            d->selected = nullptr;
        }
        emit clicked(pixelToX(p.x()), pixelToY(p.y()));
    }
    if (!d->moved && d->area.contains(p) && event->button() == Qt::RightButton
        && (drag == PlotPrivate::Drag::None || drag == PlotPrivate::Drag::Zoom))
        emit contextMenuRequested(p);
    requestUpdate();
    event->accept();
}

void Plot::mouseUngrabEvent()
{
    if (auto *c = qobject_cast<PlotCursor *>(d->dragElement.data()))
        c->setDragging(false);
    if (auto *l = qobject_cast<PlotLine *>(d->dragElement.data()))
        l->setDragging(false);
    if (auto *r = qobject_cast<PlotRegion *>(d->dragElement.data()))
        r->setDragging(false);
    d->dragElement = nullptr;
    d->drag = PlotPrivate::Drag::None;
    d->rubberRect = QRectF();
    unsetCursor();
    requestUpdate();
}

void Plot::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPointF p = event->position();
    if (!d->area.contains(p) || d->hitTest(p)) {
        event->ignore();
        return;
    }
    emit doubleClicked(pixelToX(p.x()), pixelToY(p.y()));
    if (d->interactive)
        resetView();
    event->accept();
}

void Plot::wheelEvent(QWheelEvent *event)
{
    if (!d->interactive || !d->area.contains(event->position())) {
        event->ignore();
        return;
    }
    const QPointF p = event->position();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool shift = mods & Qt::ShiftModifier;
    const bool both = mods & (Qt::ControlModifier | Qt::MetaModifier);
    const bool alt = mods & Qt::AltModifier;
    const QPoint pixel = event->pixelDelta();
    QPoint angle = event->angleDelta();
    // macOS turns Shift+wheel into horizontal scrolling: treat it as vertical.
    if (shift && angle.y() == 0)
        angle = QPoint(0, angle.x());
    const bool trackpad = !pixel.isNull() && event->source() != Qt::MouseEventNotSynthesized ? true : !pixel.isNull();

    double panPx = 0.0;
    double zoomSteps = 0.0;  // > 0 zooms in
    if (trackpad && !shift) {
        panPx = -pixel.x();
        zoomSteps = pixel.y() / 60.0;
    } else {
        zoomSteps = angle.y() / 120.0;
        panPx = -angle.x() / 120.0 * 40.0;
    }
    if (alt) {
        panPx += -(angle.y() / 120.0) * 40.0;
        zoomSteps = 0;
    }
    if (panPx != 0.0 || zoomSteps != 0.0)
        d->pushHistory(true);
    ++d->historyGuard;
    if (panPx != 0.0 && d->xAxis && d->xAxis->interactive() && d->area.width() > 0) {
        d->xAxis->pan(panPx / d->area.width());
        if (d->xAxis->follow())
            d->xAxis->setFollow(false);
    }
    if (zoomSteps != 0.0) {
        const double factor = std::pow(1.2, zoomSteps);
        zoomAt(p, shift && !both ? 1.0 : factor, (shift || both) ? factor : 1.0);
    }
    --d->historyGuard;
    event->accept();
}

bool Plot::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(event);
        if (d->interactive && g->gestureType() == Qt::ZoomNativeGesture) {
            const double factor = 1.0 + g->value();
            const bool shift = g->modifiers() & Qt::ShiftModifier;
            d->pushHistory(true);
            ++d->historyGuard;
            zoomAt(g->position(), shift ? 1.0 : factor, shift ? factor : 1.0);
            --d->historyGuard;
            event->accept();
            return true;
        }
        if (g->gestureType() == Qt::SmartZoomNativeGesture && d->interactive) {
            resetView();
            event->accept();
            return true;
        }
    }
    return QQuickItem::event(event);
}

void Plot::hoverEnterEvent(QHoverEvent *event)
{
    d->hovering = true;
    d->hoverPos = event->position();
    emit hoverChanged();
    if (d->hoverReadout)
        requestUpdate();
}

void Plot::hoverMoveEvent(QHoverEvent *event)
{
    d->hoverPos = event->position();
    d->hovering = true;
    const HitArea *hit = d->hitTest(d->hoverPos);
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (hit) {
        bool vertical = true;
        if (auto *c = qobject_cast<PlotCursor *>(hit->element.data()))
            vertical = c->orientation() == PlotElement::Vertical;
        else if (auto *l = qobject_cast<PlotLine *>(hit->element.data()))
            vertical = l->orientation() == PlotElement::Vertical;
        else if (auto *r = qobject_cast<PlotRegion *>(hit->element.data()))
            vertical = r->orientation() == PlotElement::Vertical;
        if (hit->kind == HitArea::RegionBody)
            shape = Qt::OpenHandCursor;
        else
            shape = vertical ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    } else if (d->area.contains(d->hoverPos) && d->interactive && d->dragMode == Zoom) {
        shape = Qt::CrossCursor;
    }
    if (d->drag == PlotPrivate::Drag::None) {
        if (shape == Qt::ArrowCursor)
            unsetCursor();
        else
            setCursor(shape);
    }
    emit hoverChanged();
    if (d->hoverReadout)
        requestUpdate();
}

void Plot::hoverLeaveEvent(QHoverEvent *)
{
    d->hovering = false;
    if (d->drag == PlotPrivate::Drag::None)
        unsetCursor();
    emit hoverChanged();
    if (d->hoverReadout)
        requestUpdate();
}

void Plot::keyPressEvent(QKeyEvent *event)
{
    const bool fast = event->modifiers() & Qt::ShiftModifier;
    const double stepPx = fast ? 10.0 : 1.0;
    PlotElement *sel = d->selected;
    auto nudge = [&](bool alongX, double sign) {
        if (auto *c = qobject_cast<PlotCursor *>(sel)) {
            const bool vertical = c->orientation() == PlotElement::Vertical;
            if (vertical != alongX)
                return false;
            const double px = vertical ? xToPixel(c->position()) : yToPixel(c->position());
            const double v = vertical ? pixelToX(px + sign * stepPx) : pixelToY(px - sign * stepPx);
            c->setPosition(d->snapCursor(c, v));
            if (c->snap() && c->position() == c->position() && vertical) {
                // Snapping may keep the cursor on the same sample: step to the next one.
                PlotTrace *t = c->trace();
                if (t && t->buffer()) {
                    const TraceSnapshot s = t->buffer()->snapshot();
                    if (!s->empty()) {
                        size_t i = s->nearest(t->fromDisplayX(c->position()));
                        const double cur = xToPixel(t->toDisplayX(s->xAt(i)));
                        if (std::abs(cur - px) < 0.5) {
                            if (sign > 0 && i + 1 < s->size())
                                ++i;
                            else if (sign < 0 && i > 0)
                                --i;
                            c->setPosition(t->toDisplayX(s->xAt(i)));
                        }
                    }
                }
            }
            emit c->moved(c->position());
            return true;
        }
        if (auto *l = qobject_cast<PlotLine *>(sel)) {
            const bool horizontal = l->orientation() == PlotElement::Horizontal;
            if (horizontal == alongX || !l->draggable())
                return false;
            const double px = horizontal ? yToPixel(l->displayValue()) : xToPixel(l->displayValue());
            const double v = horizontal ? pixelToY(px - sign * stepPx) : pixelToX(px + sign * stepPx);
            l->setValue(l->fromDisplay(v));
            emit l->moved(l->value());
            return true;
        }
        return false;
    };
    bool handled = false;
    switch (event->key()) {
    case Qt::Key_Left: handled = nudge(true, -1); break;
    case Qt::Key_Right: handled = nudge(true, 1); break;
    case Qt::Key_Up: handled = nudge(false, 1); break;
    case Qt::Key_Down: handled = nudge(false, -1); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (d->interactive) {
            zoomAt(d->area.center(), 1.25, 1.0);
            handled = true;
        }
        break;
    case Qt::Key_Minus:
        if (d->interactive) {
            zoomAt(d->area.center(), 0.8, 1.0);
            handled = true;
        }
        break;
    case Qt::Key_Home:
    case Qt::Key_0:
        if (d->interactive) {
            resetView();
            handled = true;
        }
        break;
    case Qt::Key_Escape:
        if (auto *c = qobject_cast<PlotCursor *>(sel))
            c->setSelected(false);
        handled = d->selected != nullptr;
        d->selected = nullptr;
        requestUpdate();
        break;
    default:
        break;
    }
    if (handled)
        event->accept();
    else
        QQuickItem::keyPressEvent(event);
}

// ---------------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------------

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

}  // namespace

bool Plot::grabImage(std::function<void(const QImage &)> done)
{
    if (!window())
        return false;
    QSharedPointer<QQuickItemGrabResult> result;
    if (window()->isVisible())
        result = grabToImage();
    if (!result) {
        // Offscreen (QQuickRenderControl) windows are never "visible": grab the whole window via
        // mybench::OffscreenWindow's grabber (found by name, no link dependency), else grabWindow().
        QImage frame;
        if (QObject *grabber = window()->findChild<QObject *>(QStringLiteral("mybench_offscreen_grabber"),
                                                               Qt::FindDirectChildrenOnly))
            QMetaObject::invokeMethod(grabber, "grab", Qt::DirectConnection, qReturnArg(frame));
        if (frame.isNull())
            frame = window()->grabWindow();
        if (frame.isNull())
            return false;
        const qreal dpr = frame.devicePixelRatio() > 0 ? frame.devicePixelRatio() : 1.0;
        const QRectF scene = mapRectToScene(boundingRect());
        const QRect px(qRound(scene.x() * dpr), qRound(scene.y() * dpr), qRound(scene.width() * dpr),
                       qRound(scene.height() * dpr));
        QImage image = frame.copy(px);
        image.setDevicePixelRatio(dpr);
        QMetaObject::invokeMethod(this, [done = std::move(done), image] { done(image); }, Qt::QueuedConnection);
        return true;
    }
    connect(result.data(), &QQuickItemGrabResult::ready, this, [result, done = std::move(done)] { done(result->image()); });
    return true;
}

bool Plot::saveImage(const QUrl &file)
{
    const QString path = localPath(file);
    if (path.isEmpty())
        return false;
    return grabImage([this, path](const QImage &image) {
        const bool ok = !image.isNull() && image.save(path);
        emit imageSaved(path, ok);
    });
}

bool Plot::copyImage()
{
    return grabImage([this](const QImage &image) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        const bool ok = !image.isNull() && clipboard;
        if (ok)
            clipboard->setImage(image);
        emit imageCopied(ok);
    });
}

QString Plot::toCsv(bool visibleRangeOnly) const
{
    struct Column {
        PlotTrace *trace;
        TraceSnapshot data;
        size_t first, last;
    };
    std::vector<Column> columns;
    const double xMin = d->xAxis ? d->xAxis->min() : -qInf();
    const double xMax = d->xAxis ? d->xAxis->max() : qInf();
    for (PlotTrace *t : d->elementsOf<PlotTrace>()) {
        if (!t->isVisible() || !t->buffer())
            continue;
        TraceSnapshot s = t->buffer()->snapshot();
        size_t first = 0, last = s->size();
        if (visibleRangeOnly && s->sorted) {
            const double a = t->fromDisplayX(xMin), b = t->fromDisplayX(xMax);
            first = s->lowerBound(std::min(a, b));
            last = s->upperBound(std::max(a, b));
        }
        columns.push_back({t, std::move(s), first, last});
    }
    QString out;
    QTextStream ts(&out);
    ts.setRealNumberPrecision(10);
    const QString xName = d->xAxis && !d->xAxis->title().isEmpty() ? d->xAxis->title() : QStringLiteral("x");
    const QString xUnit = d->xAxis ? d->xAxis->unit() : QString();
    const QString yUnit = d->yAxis ? d->yAxis->unit() : QString();
    auto header = [](const QString &name, const QString &unit) {
        QString h = name;
        if (!unit.isEmpty())
            h += QStringLiteral(" (") + unit + u')';
        return u'"' + h.replace(u'"', QStringLiteral("\"\"")) + u'"';
    };
    // Shared x column when all traces sample the same x grid.
    bool shared = !columns.empty();
    for (const Column &c : columns) {
        const Column &f = columns.front();
        if (!c.data->uniform || c.data->x0 != f.data->x0 || c.data->dx != f.data->dx || c.first != f.first
            || c.last != f.last || c.trace->xScale() != f.trace->xScale() || c.trace->xOffset() != f.trace->xOffset())
            shared = false;
    }
    QStringList head;
    if (shared)
        head << header(xName, xUnit);
    for (const Column &c : columns) {
        const QString name = c.trace->name().isEmpty() ? QStringLiteral("trace") : c.trace->name();
        const QString unit = c.trace->unit().isEmpty() ? yUnit : c.trace->unit();
        if (!shared)
            head << header(name + QStringLiteral(" ") + xName, xUnit);
        head << header(name, unit);
    }
    ts << head.join(u',') << '\n';
    size_t rows = 0;
    for (const Column &c : columns)
        rows = std::max(rows, c.last - c.first);
    for (size_t r = 0; r < rows; ++r) {
        QStringList cells;
        if (shared) {
            const Column &f = columns.front();
            cells << QString::number(f.trace->toDisplayX(f.data->xAt(f.first + r)), 'g', 12);
        }
        for (const Column &c : columns) {
            const size_t i = c.first + r;
            const bool has = i < c.last;
            if (!shared)
                cells << (has ? QString::number(c.data->xAt(i), 'g', 12) : QString());
            cells << (has ? QString::number(c.data->yAt(i), 'g', 10) : QString());
        }
        ts << cells.join(u',') << '\n';
    }
    ts.flush();
    return out;
}

bool Plot::saveCsv(const QUrl &file, bool visibleRangeOnly) const
{
    const QString path = localPath(file);
    if (path.isEmpty())
        return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(toCsv(visibleRangeOnly).toUtf8());
    return f.commit();
}

}  // namespace mybench::plot
