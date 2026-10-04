// Plot rendering: layout and geometry (updatePolish, GUI thread) and the scene graph
// (updatePaintNode, render thread with the GUI thread blocked).
#include "plot_p.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGOpacityNode>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QSGTextureMaterial>
#include <QSGTransformNode>

#include <rhi/qrhi.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace mybench::plot {

namespace {

quint64 hashCombine(quint64 seed, quint64 v)
{
    return seed ^ (v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

quint64 hashDouble(quint64 seed, double v)
{
    quint64 bits;
    std::memcpy(&bits, &v, sizeof bits);
    return hashCombine(seed, bits);
}

quint64 hashMap(quint64 seed, const ScaleMap &m)
{
    seed = hashDouble(seed, m.min);
    seed = hashDouble(seed, m.max);
    seed = hashDouble(seed, m.pixelStart);
    seed = hashDouble(seed, m.pixelLength);
    seed = hashCombine(seed, quint64(m.type) * 2 + (m.inverted ? 1 : 0));
    return seed;
}

QColor withAlpha(QColor c, double alpha)
{
    c.setAlphaF(float(std::clamp(alpha, 0.0, 1.0) * c.alphaF()));
    return c;
}

PixelPoint gapPoint()
{
    return {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
}

// Dark text on light chips, white text on dark ones.
QColor contrastText(const QColor &background)
{
    const double l = 0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
    return l > 0.55 ? QColor(0x0b, 0x0d, 0x10) : QColor(0xff, 0xff, 0xff);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------------

std::shared_ptr<QTextLayout> PlotPrivate::layout(const QString &text, const QFont &font, int fontId)
{
    const QString key = QString::number(fontId) + QChar(0x1f) + text;
    auto it = layoutCacheNext.constFind(key);
    if (it != layoutCacheNext.constEnd())
        return *it;
    std::shared_ptr<QTextLayout> l;
    auto old = layoutCache.constFind(key);
    if (old != layoutCache.constEnd()) {
        l = *old;
    } else {
        l = std::make_shared<QTextLayout>(text, font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        l->setTextOption(option);
        l->setCacheEnabled(true);
        l->beginLayout();
        QTextLine line = l->createLine();
        line.setLineWidth(100000);
        line.setPosition(QPointF(0, 0));
        l->endLayout();
    }
    layoutCacheNext.insert(key, l);
    return l;
}

QSizeF PlotPrivate::textSize(const QString &text, const QFont &font) const
{
    const QFontMetricsF fm(font);
    return QSizeF(fm.horizontalAdvance(text), fm.height());
}

QSizeF PlotPrivate::addText(TextLayer layer, const QString &text, QPointF pos, const QColor &color, int fontId,
                            qreal rotation)
{
    if (text.isEmpty())
        return {};
    const QFont f = fontId == 1 ? chipFont() : fontId == 2 ? titleFont() : labelFont();
    std::shared_ptr<QTextLayout> l = layout(text, f, fontId);
    const QTextLine line = l->lineAt(0);
    const QSizeF size(line.naturalTextWidth(), line.height());
    // Snap to device pixels for crisp glyphs.
    pos = QPointF(std::round(pos.x() * dpr) / dpr, std::round(pos.y() * dpr) / dpr);
    texts[layer].push_back({l, pos, color, rotation});
    return size;
}

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------

namespace {

TickSet ticksFor(const PlotAxis *axis, double length, Plot::Graticule graticule, bool isX)
{
    if (!axis)
        return {};
    if (graticule == Plot::Scope && !axis->isLog()) {
        const int div = axis->divisions() > 0 ? axis->divisions() : (isX ? 10 : 8);
        return divisionTicks(axis->min(), axis->max(), div, axis->minorTicks() < 0 ? 5 : axis->minorTicks());
    }
    return axis->ticks(length);
}

}  // namespace

void PlotPrivate::computeLayout()
{
    const QRectF bounds = q->boundingRect();
    dpr = q->window() ? q->window()->effectiveDevicePixelRatio() : 1.0;
    if (!(dpr > 0))
        dpr = 1.0;
    aa = float(1.0 / dpr);

    const QFontMetricsF fm(labelFont());
    const qreal lineH = std::ceil(fm.height());
    PlotAxis *xa = xAxis, *ya = yAxis;
    const bool xLabels = showXLabels();
    const bool yLabels = showYLabels();
    const QString xTitle = xa && this->xLabels ? xa->decoratedTitle() : QString();
    const QString yTitle = ya && this->yLabels ? ya->decoratedTitle() : QString();

    bool leftHandle = false, rightHandle = false, topHandle = false, bottomHandle = false;
    for (PlotLine *l : elementsOf<PlotLine>()) {
        if (!l->isVisible() || l->handle() == PlotLine::NoHandle)
            continue;
        const bool horizontal = l->orientation() == PlotElement::Horizontal;
        const bool start = l->handle() == PlotLine::Start;
        (horizontal ? (start ? leftHandle : rightHandle) : (start ? topHandle : bottomHandle)) = true;
    }
    qreal laneLabelW = 0;
    for (PlotLogicTrace *t : elementsOf<PlotLogicTrace>()) {
        if (!t->isVisible() || !t->labelsVisible())
            continue;
        for (int r = 0; r < t->rowCount(); ++r)
            laneLabelW = std::max(laneLabelW, fm.horizontalAdvance(t->nameForRow(r)));
    }
    const qreal handleW = 20, handleH = 20;

    qreal top = margins[2];
    if (top < 0) {
        top = topHandle ? handleH + 2 : 10;
        if (yLabels)
            top = std::max(top, std::ceil(lineH / 2) + 3);
    }
    qreal bottom = margins[3];
    if (bottom < 0) {
        bottom = 8 + (xLabels ? lineH + 4 : 0) + (xTitle.isEmpty() ? 0 : lineH + 4) + (bottomHandle ? handleH : 0);
        if (!xLabels && xTitle.isEmpty() && !bottomHandle)
            bottom = 10;
    }
    const qreal plotH = std::max<qreal>(1, bounds.height() - top - bottom);
    yTicks = ticksFor(ya, plotH, graticule, false);

    qreal maxLabelW = laneLabelW;
    if (yLabels) {
        for (double v : yTicks.major)
            maxLabelW = std::max(maxLabelW, fm.horizontalAdvance(ya->tickLabel(v, yTicks)));
    }
    qreal left = 8;
    if (yLabels || laneLabelW > 0)
        left += std::ceil(maxLabelW) + 8;
    if (!yTitle.isEmpty())
        left += lineH + 6;
    if (leftHandle)
        left += handleW;
    if (!yLabels && laneLabelW <= 0 && yTitle.isEmpty() && !leftHandle)
        left = 10;
    implicitLeft = left;
    if (margins[0] >= 0)
        left = margins[0];

    qreal right = margins[1];
    if (right < 0)
        right = rightHandle ? handleW + 8 : 14;
    qreal plotW = std::max<qreal>(1, bounds.width() - left - right);
    xTicks = ticksFor(xa, plotW, graticule, true);
    if (margins[1] < 0 && xLabels && !xTicks.major.empty()) {
        // Room for half of the last label when its tick sits at the right edge.
        const double last = xTicks.major.back();
        const double px = xa->map(left, plotW).toPixel(last);
        const qreal half = fm.horizontalAdvance(xa->tickLabel(last, xTicks)) / 2;
        const qreal overflow = px + half + 4 - (bounds.width() - 2);
        if (overflow > 0 && px >= left + plotW - 1) {
            right += overflow;
            plotW = std::max<qreal>(1, bounds.width() - left - right);
            xTicks = ticksFor(xa, plotW, graticule, true);
        }
    }

    const QRectF newArea(left, top, plotW, plotH);
    const bool areaChanged = newArea != area;
    area = newArea;
    xMap = xa ? xa->map(area.left(), area.width()) : ScaleMap();
    yMap = ya ? ya->map(area.bottom(), -area.height()) : ScaleMap();
    if (areaChanged)
        QMetaObject::invokeMethod(q, &Plot::plotAreaChanged, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------------------------
// Background, grid, frame, labels
// ---------------------------------------------------------------------------------------------

void PlotPrivate::buildStatic()
{
    for (LineGeometry *g : {&gridMinor, &gridMajor, &gridAxis, &frameLines}) {
        g->clear();
        g->aa = aa;
    }
    texts[LabelLayer].clear();
    if (!mapsValid())
        return;

    const QFontMetricsF fm(labelFont());
    const qreal lineH = std::ceil(fm.height());
    const double r = dpr;
    auto snap = [r](double v, double width) {
        const double off = (int(std::lround(width * r)) % 2) ? 0.5 : 0.0;
        return (std::round(v * r - off) + off) / r;
    };
    const QRectF a = area;
    const quint32 major = packColor(gridColor());
    const quint32 minor = packColor(minorGridColor());
    const float hw = 0.5f;
    const double top = a.top(), bottom = a.bottom(), leftX = a.left(), rightX = a.right();
    auto inX = [&](double px) { return px >= leftX - 0.5 && px <= rightX + 0.5; };
    auto inY = [&](double py) { return py >= top - 0.5 && py <= bottom + 0.5; };
    auto nearEdge = [](double v, double edge) { return std::abs(v - edge) < 0.75; };

    if (graticule == Plot::Grid) {
        dashPattern(int(gridStyle), 1.0f, &majorDashOn, &majorDashOff);
        minorDashOn = minorDashOff = 0;
        if (xAxis->gridVisible()) {
            if (xAxis->minorGridVisible()) {
                for (double v : xTicks.minor) {
                    const double px = snap(xMap.toPixel(v), 1.0);
                    if (inX(px) && !nearEdge(px, leftX) && !nearEdge(px, rightX))
                        gridMinor.segment(float(px), float(top), float(px), float(bottom), hw, minor);
                }
            }
            for (double v : xTicks.major) {
                const double px = snap(xMap.toPixel(v), 1.0);
                if (inX(px) && !(frameVisible && (nearEdge(px, leftX) || nearEdge(px, rightX))))
                    gridMajor.segment(float(px), float(top), float(px), float(bottom), hw, major);
            }
        }
        if (yAxis->gridVisible()) {
            if (yAxis->minorGridVisible()) {
                for (double v : yTicks.minor) {
                    const double py = snap(yMap.toPixel(v), 1.0);
                    if (inY(py) && !nearEdge(py, top) && !nearEdge(py, bottom))
                        gridMinor.segment(float(leftX), float(py), float(rightX), float(py), hw, minor);
                }
            }
            for (double v : yTicks.major) {
                const double py = snap(yMap.toPixel(v), 1.0);
                if (inY(py) && !(frameVisible && (nearEdge(py, top) || nearEdge(py, bottom))))
                    gridMajor.segment(float(leftX), float(py), float(rightX), float(py), hw, major);
            }
        }
    } else if (graticule == Plot::Scope) {
        // Dotted division lines, solid center axes with minor tick marks.
        majorDashOn = 1.0f;
        majorDashOff = 3.0f;
        const quint32 axisColor = packColor(withAlpha(gridColor(), 2.2));
        const double cx = snap(a.center().x(), 1.0), cy = snap(a.center().y(), 1.0);
        for (double v : xTicks.major) {
            const double px = snap(xMap.toPixel(v), 1.0);
            if (inX(px) && !nearEdge(px, leftX) && !nearEdge(px, rightX) && !nearEdge(px, cx))
                gridMajor.segment(float(px), float(top), float(px), float(bottom), hw, packColor(withAlpha(gridColor(), 1.6)));
        }
        for (double v : yTicks.major) {
            const double py = snap(yMap.toPixel(v), 1.0);
            if (inY(py) && !nearEdge(py, top) && !nearEdge(py, bottom) && !nearEdge(py, cy))
                gridMajor.segment(float(leftX), float(py), float(rightX), float(py), hw, packColor(withAlpha(gridColor(), 1.6)));
        }
        gridAxis.segment(float(cx), float(top), float(cx), float(bottom), hw, axisColor);
        gridAxis.segment(float(leftX), float(cy), float(rightX), float(cy), hw, axisColor);
        const float tick = 3.0f;
        for (double v : xTicks.minor) {
            const double px = snap(xMap.toPixel(v), 1.0);
            if (!inX(px))
                continue;
            gridAxis.segment(float(px), float(cy - tick), float(px), float(cy + tick), hw, axisColor);
            gridAxis.segment(float(px), float(top), float(px), float(top + tick), hw, axisColor);
            gridAxis.segment(float(px), float(bottom - tick), float(px), float(bottom), hw, axisColor);
        }
        for (double v : yTicks.minor) {
            const double py = snap(yMap.toPixel(v), 1.0);
            if (!inY(py))
                continue;
            gridAxis.segment(float(cx - tick), float(py), float(cx + tick), float(py), hw, axisColor);
            gridAxis.segment(float(leftX), float(py), float(leftX + tick), float(py), hw, axisColor);
            gridAxis.segment(float(rightX - tick), float(py), float(rightX), float(py), hw, axisColor);
        }
    }

    if (frameVisible) {
        const quint32 fc = packColor(frameColor());
        const double l = snap(leftX, 1.0), rr = snap(rightX, 1.0), t = snap(top, 1.0), b = snap(bottom, 1.0);
        frameLines.segment(float(l), float(t), float(rr), float(t), hw, fc);
        frameLines.segment(float(rr), float(t), float(rr), float(b), hw, fc);
        frameLines.segment(float(rr), float(b), float(l), float(b), hw, fc);
        frameLines.segment(float(l), float(b), float(l), float(t), hw, fc);
    }

    // Tick labels.
    const QColor tc = textColor();
    const QRectF bounds = q->boundingRect();
    bool leftHandle = false, bottomHandle = false;
    for (PlotLine *l : elementsOf<PlotLine>()) {
        if (!l->isVisible())
            continue;
        if (l->orientation() == PlotElement::Horizontal && l->handle() == PlotLine::Start)
            leftHandle = true;
        if (l->orientation() == PlotElement::Vertical && l->handle() == PlotLine::End)
            bottomHandle = true;
    }
    if (showXLabels()) {
        qreal lastRight = -1e9;
        const qreal y = a.bottom() + 4 + (bottomHandle ? 20 : 0);
        for (double v : xTicks.major) {
            const double px = xMap.toPixel(v);
            if (!inX(px))
                continue;
            const QString s = xAxis->tickLabel(v, xTicks);
            const qreal w = fm.horizontalAdvance(s);
            const qreal x = std::clamp(px - w / 2, 1.0, bounds.width() - w - 1);
            if (x < lastRight + 8)
                continue;
            addText(LabelLayer, s, QPointF(x, y), tc);
            lastRight = x + w;
        }
    }
    if (showYLabels()) {
        qreal lastTop = 1e9;
        const qreal rightEdge = a.left() - 7 - (leftHandle ? 20 : 0);
        for (auto it = yTicks.major.begin(); it != yTicks.major.end(); ++it) {
            const double py = yMap.toPixel(*it);
            if (!inY(py))
                continue;
            const QString s = yAxis->tickLabel(*it, yTicks);
            const qreal w = fm.horizontalAdvance(s);
            const qreal y = std::clamp(py - lineH / 2, 0.0, bounds.height() - lineH);
            if (std::abs(y - lastTop) < lineH + 1 && lastTop < 1e8)
                continue;
            addText(LabelLayer, s, QPointF(rightEdge - w, y), tc);
            lastTop = y;
        }
    }
    const QColor titleC = titleColor();
    const QString xTitle = xLabels ? xAxis->decoratedTitle() : QString();
    if (!xTitle.isEmpty()) {
        const QSizeF s = textSize(xTitle, titleFont());
        addText(LabelLayer, xTitle, QPointF(a.center().x() - s.width() / 2, bounds.height() - s.height() - 4), titleC, 2);
    }
    const QString yTitle = yLabels ? yAxis->decoratedTitle() : QString();
    if (!yTitle.isEmpty()) {
        const QSizeF s = textSize(yTitle, titleFont());
        addText(LabelLayer, yTitle, QPointF(4, a.center().y() + s.width() / 2), titleC, 2, -90);
    }
    // Logic lane names.
    for (PlotLogicTrace *t : elementsOf<PlotLogicTrace>()) {
        if (!t->isVisible() || !t->labelsVisible())
            continue;
        for (int row = 0; row < t->rowCount(); ++row) {
            const double y0 = yMap.toPixel(t->firstRow() + row), y1 = yMap.toPixel(t->firstRow() + row + 1);
            const double mid = 0.5 * (y0 + y1);
            if (!inY(mid))
                continue;
            const QString name = t->nameForRow(row);
            const qreal w = fm.horizontalAdvance(name);
            addText(LabelLayer, name, QPointF(a.left() - 8 - w, mid - lineH / 2), t->colorForRow(row));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Traces
// ---------------------------------------------------------------------------------------------

void PlotPrivate::buildTraces()
{
    const std::vector<PlotTrace *> list = elementsOf<PlotTrace>();
    bool same = list.size() == traces.size();
    for (size_t i = 0; same && i < list.size(); ++i)
        same = traces[i]->trace == list[i];
    if (!same) {
        std::vector<std::unique_ptr<TraceGeom>> next;
        for (PlotTrace *t : list) {
            auto it = std::find_if(traces.begin(), traces.end(), [t](const auto &g) { return g && g->trace == t; });
            if (it != traces.end()) {
                next.push_back(std::move(*it));
            } else {
                auto g = std::make_unique<TraceGeom>();
                g->trace = t;
                next.push_back(std::move(g));
            }
        }
        traces = std::move(next);
        traceListChanged = true;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ghostsActive = false;
    const QColor areaC = areaColor();
    for (auto &gp : traces) {
        TraceGeom &g = *gp;
        PlotTrace *t = g.trace;
        g.line.aa = g.dots.aa = aa;
        if (!t || !t->isVisible() || !t->buffer() || !mapsValid()) {
            if (!g.line.empty() || !g.fill.empty() || !g.dots.empty()) {
                g.line.clear();
                g.fill.clear();
                g.dots.clear();
                g.dirty = true;
            }
            g.key = 0;
            g.points.clear();
            if (!g.ghosts.empty()) {
                g.ghosts.clear();
                g.dirty = true;
            }
            continue;
        }
        const TraceSnapshot s = t->buffer()->snapshot();
        const PlotTrace::Appearance ap = t->appearance();
        const QColor color = traceColor(t);
        const QColor fillC = ap.fillColor.isValid() ? ap.fillColor : color;
        quint64 key = hashCombine(0, s->version);
        key = hashCombine(key, quint64(quintptr(t->buffer())));
        key = hashMap(key, xMap);
        key = hashMap(key, yMap);
        key = hashDouble(key, ap.width);
        key = hashDouble(key, t->xScale());
        key = hashDouble(key, t->xOffset());
        key = hashDouble(key, t->yScale());
        key = hashDouble(key, t->yOffset());
        key = hashDouble(key, ap.fillBaseline);
        key = hashDouble(key, ap.fillOpacity);
        key = hashDouble(key, ap.pointSize);
        key = hashDouble(key, dpr);
        key = hashCombine(key, quint64(color.rgba()) | (quint64(areaC.rgba()) << 32));
        key = hashCombine(key, quint64(fillC.rgba()));
        key = hashCombine(key, quint64(ap.style) | (quint64(ap.interpolation) << 4) | (quint64(ap.fill) << 8)
                                   | (quint64(ap.pointsVisible) << 9) | (quint64(ap.lineVisible) << 10)
                                   | (quint64(ap.pointsOnly) << 11) | (quint64(ap.fillGradient) << 12)
                                   | (quint64(ap.pointShape) << 16));
        const int persistence = t->persistence();
        // Persistence: keep the previous record's geometry as a fading ghost.
        if (persistence > 0 && s->version != g.dataVersion && !g.line.empty()) {
            if (g.ghosts.empty() || now - g.ghosts.back().time >= 12) {
                g.ghosts.push_back({std::make_shared<const LineGeometry>(g.line), now, ++ghostCounter});
                while (g.ghosts.size() > 48)
                    g.ghosts.pop_front();
                g.dirty = true;
            }
        }
        if (persistence <= 0 && !g.ghosts.empty()) {
            g.ghosts.clear();
            g.dirty = true;
        }
        while (!g.ghosts.empty() && now - g.ghosts.front().time > persistence) {
            g.ghosts.pop_front();
            g.dirty = true;
        }
        if (!g.ghosts.empty())
            ghostsActive = true;
        g.dataVersion = s->version;
        if (key == g.key)
            continue;
        g.key = key;
        g.dirty = true;

        TraceTransform tf{t->xScale(), t->xOffset(), t->yScale(), t->yOffset()};
        static constexpr StepMode stepModes[] = {StepMode::None, StepMode::Hold, StepMode::Vertical,
                                                 StepMode::CenterHorizontal, StepMode::CenterVertical};
        g.info = buildPolyline(*s, xMap, yMap, tf, stepModes[int(ap.interpolation)], 1.0, g.points);

        // Points are drawn while samples are sparse (>= 3 px apart); a "points only" trace with a
        // dense record falls back to a dotted line so it never disappears.
        const bool sparse = !g.info.decimated && double(g.points.size()) <= area.width() / 3.0;
        const bool dottedFallback = ap.pointsOnly && !sparse;
        const quint32 packed = packColor(color);
        g.line.clear();
        if (ap.lineVisible || dottedFallback)
            g.line.polyline(g.points.data(), g.points.size(), float(ap.width * 0.5), packed);
        g.dash = dashPattern(dottedFallback ? int(PlotElement::Dot) : int(ap.style), float(ap.width));

        g.fill.clear();
        if (ap.fill && g.points.size() > 1) {
            double base = area.bottom();
            if (std::isinf(ap.fillBaseline)) {
                const double edge = yMap.toPixel(ap.fillBaseline > 0 ? yMap.max : yMap.min);
                if (std::isfinite(edge))
                    base = std::clamp(edge, area.top(), area.bottom());
            } else if (!std::isnan(ap.fillBaseline)) {
                const double b = yMap.toPixel(ap.fillBaseline);
                if (std::isfinite(b))
                    base = std::clamp(b, area.top() - 1, area.bottom() + 1);
            }
            const QColor topC = withAlpha(fillC, ap.fillOpacity);
            const QColor bottomC = withAlpha(fillC, ap.fillOpacity * (ap.fillGradient ? 0.12 : 1.0));
            g.fill.vertices.reserve(g.points.size() * 4);
            for (size_t i = 1; i < g.points.size(); ++i) {
                const PixelPoint &p0 = g.points[i - 1], &p1 = g.points[i];
                if (p0.isGap() || p1.isGap() || p1.x == p0.x)
                    continue;
                g.fill.quad(QPointF(p0.x, p0.y), QPointF(p1.x, p1.y), QPointF(p1.x, base), QPointF(p0.x, base),
                            topC, bottomC);
            }
            // Decimated columns are vertical pairs: fill each column down from its max.
            if (g.info.decimated) {
                for (size_t i = 1; i < g.points.size(); ++i) {
                    const PixelPoint &p0 = g.points[i - 1], &p1 = g.points[i];
                    if (p0.isGap() || p1.isGap() || p1.x != p0.x)
                        continue;
                    const float yTop = std::min(p0.y, p1.y);
                    g.fill.quad(QPointF(p0.x - 0.5, yTop), QPointF(p0.x + 0.5, yTop), QPointF(p0.x + 0.5, base),
                                QPointF(p0.x - 0.5, base), topC, bottomC);
                }
            }
        }

        g.dots.clear();
        if (ap.pointsVisible && sparse) {
            const double r = ap.pointSize * 0.5;
            const quint32 dotC = packColor(fillC);
            const float stroke = float(std::clamp(ap.width * 0.6, 1.0, 2.0));
            // Staircases add corner vertices: mark the samples only.
            std::vector<PixelPoint> samplePoints;
            if (ap.interpolation != PlotTrace::Linear)
                buildPolyline(*s, xMap, yMap, tf, StepMode::None, 1.0, samplePoints);
            const std::vector<PixelPoint> &markAt = ap.interpolation != PlotTrace::Linear ? samplePoints : g.points;
            // Hollow shapes: transparent fill with a border; Plus/X: thin arms; Cross: bold arms.
            for (const PixelPoint &p : markAt) {
                if (p.isGap() || p.x < area.left() - r || p.x > area.right() + r)
                    continue;
                const QRectF box(p.x - r, p.y - r, 2 * r, 2 * r);
                switch (ap.pointShape) {
                case PlotTrace::FilledCircle: g.dots.shape(box, ShapeKind::Box, float(r), dotC); break;
                case PlotTrace::Circle: g.dots.shape(box, ShapeKind::Box, float(r), 0, stroke, dotC); break;
                case PlotTrace::Square: g.dots.shape(box.adjusted(0.5, 0.5, -0.5, -0.5), ShapeKind::Box, 0.0f, 0, stroke, dotC); break;
                case PlotTrace::FilledSquare: g.dots.shape(box.adjusted(0.5, 0.5, -0.5, -0.5), ShapeKind::Box, 0.0f, dotC); break;
                case PlotTrace::Diamond: g.dots.shape(box.adjusted(-1, -1, 1, 1), ShapeKind::Diamond, 0.0f, 0, stroke, dotC); break;
                case PlotTrace::FilledDiamond: g.dots.shape(box.adjusted(-1, -1, 1, 1), ShapeKind::Diamond, 0.0f, dotC); break;
                case PlotTrace::Cross: g.dots.shape(box.adjusted(-0.5, -0.5, 0.5, 0.5), ShapeKind::Plus, float(r * 0.34 + 0.25), dotC); break;
                case PlotTrace::Plus: g.dots.shape(box.adjusted(-0.5, -0.5, 0.5, 0.5), ShapeKind::Plus, stroke * 0.5f, dotC); break;
                case PlotTrace::X: g.dots.shape(box.adjusted(-0.5, -0.5, 0.5, 0.5), ShapeKind::X, stroke * 0.5f, dotC); break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Logic lanes
// ---------------------------------------------------------------------------------------------

void PlotPrivate::buildLogic()
{
    logicLines.clear();
    logicLines.aa = aa;
    logicFill.clear();
    if (!mapsValid())
        return;
    for (PlotLogicTrace *t : elementsOf<PlotLogicTrace>()) {
        if (!t->isVisible() || !t->buffer())
            continue;
        const LogicSnapshot ls = t->buffer()->snapshot();
        if (ls->empty())
            continue;
        const double vx0 = std::max(std::min(xMap.min, xMap.max), ls->startX);
        const double vx1 = std::min(std::max(xMap.min, xMap.max), ls->endX);
        if (!(vx1 > vx0))
            continue;
        const float hw = float(t->width() * 0.5);
        auto X = [&](double x) { return float(xMap.toPixel(x)); };
        const double columns = std::abs(X(vx1) - X(vx0));
        std::vector<PixelPoint> path;
        for (int row = 0; row < t->rowCount(); ++row) {
            const int lane = t->laneForRow(row);
            if (lane < 0 || lane >= ls->laneCount)
                continue;
            const double ya = yMap.toPixel(t->firstRow() + row), yb = yMap.toPixel(t->firstRow() + row + 1);
            const double topY = std::min(ya, yb), botY = std::max(ya, yb);
            const double mid = 0.5 * (topY + botY), half = 0.5 * (botY - topY) * t->laneHeight();
            const float hiY = float(mid - half), loY = float(mid + half);
            const QColor color = t->colorForRow(row);
            const quint32 packed = packColor(color);
            const QColor fillC = withAlpha(color, t->fillOpacity());
            const QColor busyC = withAlpha(color, 0.45);
            auto levelY = [&](bool high) { return high ? hiY : loY; };
            auto highFill = [&](float x0, float x1) {
                if (x1 > x0)
                    logicFill.rect(QRectF(QPointF(x0, hiY), QPointF(x1, loY)), fillC);
            };

            const size_t i0 = ls->lowerBound(lane, vx0);
            const size_t i1 = ls->upperBound(lane, vx1);
            bool level = ls->levelBefore(lane, i0);
            path.clear();
            float startX = X(vx0);
            path.push_back({startX, levelY(level)});
            if (double(i1 - i0) <= 2.0 * columns) {
                // Exact edges.
                for (size_t k = i0; k < i1; ++k) {
                    const float px = X(ls->edgeAt(lane, k));
                    path.push_back({px, levelY(level)});
                    if (level)
                        highFill(startX, px);
                    level = !level;
                    path.push_back({px, levelY(level)});
                    startX = px;
                }
            } else {
                // Dense: per pixel column, one edge is drawn exactly, more become a busy block.
                const double pxStart = X(vx0), pxEnd = X(vx1);
                size_t j = i0;
                float blockStart = -1, blockEnd = -1;
                auto flushBlock = [&] {
                    if (blockStart < 0)
                        return;
                    logicFill.rect(QRectF(QPointF(blockStart, hiY), QPointF(blockEnd, loY)), busyC);
                    logicLines.segment(blockStart, hiY, blockEnd, hiY, hw, packed);
                    logicLines.segment(blockStart, loY, blockEnd, loY, hw, packed);
                    blockStart = blockEnd = -1;
                };
                for (double c = std::floor(pxStart); c < pxEnd; c += 1.0) {
                    const double cx0 = std::max(c, pxStart), cx1 = std::min(c + 1.0, pxEnd);
                    const double x1 = std::min(vx1, xMap.fromPixel(cx1));
                    size_t e = j;
                    {
                        size_t lo = j, hi = i1;
                        while (lo < hi) {
                            const size_t m = lo + (hi - lo) / 2;
                            if (ls->edgeAt(lane, m) < x1)
                                lo = m + 1;
                            else
                                hi = m;
                        }
                        e = lo;
                    }
                    const size_t k = e - j;
                    if (k == 1) {
                        flushBlock();
                        const float px = X(ls->edgeAt(lane, j));
                        path.push_back({px, levelY(level)});
                        if (level)
                            highFill(startX, px);
                        level = !level;
                        path.push_back({px, levelY(level)});
                        startX = px;
                    } else if (k >= 2) {
                        path.push_back({float(cx0), levelY(level)});
                        if (level)
                            highFill(startX, float(cx0));
                        path.push_back(gapPoint());
                        if (blockStart < 0)
                            blockStart = float(cx0);
                        blockEnd = float(cx1);
                        level = ls->levelBefore(lane, e);
                        startX = float(cx1);
                        path.push_back({startX, levelY(level)});
                    }
                    j = e;
                }
                flushBlock();
            }
            const float endX = X(vx1);
            path.push_back({endX, levelY(level)});
            if (level)
                highFill(startX, endX);
            logicLines.polyline(path.data(), path.size(), hw, packed);
        }
    }
}


// ---------------------------------------------------------------------------------------------
// Overlays: regions, reference lines + handles, cursors + readouts, markers, rubber band
// ---------------------------------------------------------------------------------------------

void PlotPrivate::buildOverlays()
{
    regions.clear();
    for (LineGeometry *g : {&overlaySolid, &overlayDash, &overlayDot, &rubberLine}) {
        g->clear();
        g->aa = aa;
    }
    overlayShapes.clear();
    overlayShapes.aa = aa;
    chips.clear();
    chips.aa = aa;
    rubberFill.clear();
    texts[ChipLayer].clear();
    hits.clear();
    if (!mapsValid())
        return;

    const QRectF a = area;
    const double ratio = dpr;
    // Pixel-align thin straight lines so they stay crisp (odd device widths on pixel centers).
    auto snap = [ratio](double v, double width) {
        const double off = (int(std::lround(width * ratio)) % 2) ? 0.5 : 0.0;
        return (std::round(v * ratio - off) + off) / ratio;
    };
    const QFont cf = chipFont();
    const QFontMetricsF cfm(cf);
    const qreal chipH = std::ceil(cfm.height()) + 4;
    const QColor areaC = areaColor();
    const QColor titleC = titleColor();
    auto lineFor = [&](PlotElement::LineStyle style) -> LineGeometry & {
        return style == PlotElement::Dot ? overlayDot : (style == PlotElement::Solid ? overlaySolid : overlayDash);
    };
    // Chip: rounded rectangle with centered text; `rect` is the outer box.
    auto chip = [&](ShapeGeometry &shapes, TextLayer layer, const QRectF &rect, const QString &text,
                    const QColor &fill, const QColor &textC, float border = 0, const QColor &borderC = QColor()) {
        shapes.shape(rect, ShapeKind::Box, 4.0f, packColor(fill), border, packColor(borderC));
        const qreal w = cfm.horizontalAdvance(text);
        addText(layer, text, QPointF(rect.center().x() - w / 2, rect.center().y() - cfm.height() / 2), textC, 1);
    };
    auto chipWidth = [&](const QString &text) { return std::ceil(cfm.horizontalAdvance(text)) + 12; };

    // Regions.
    for (PlotRegion *r : elementsOf<PlotRegion>()) {
        if (!r->isVisible())
            continue;
        const bool vertical = r->orientation() == PlotElement::Vertical;
        const ScaleMap &m = vertical ? xMap : yMap;
        const double p0 = snap(m.toPixel(r->from()), 1.0), p1 = snap(m.toPixel(r->to()), 1.0);
        if (!std::isfinite(p0) || !std::isfinite(p1))
            continue;
        QRectF rect = vertical ? QRectF(QPointF(std::min(p0, p1), a.top()), QPointF(std::max(p0, p1), a.bottom()))
                               : QRectF(QPointF(a.left(), std::min(p0, p1)), QPointF(a.right(), std::max(p0, p1)));
        const QRectF visible = rect.intersected(a);
        if (!visible.isEmpty())
            regions.rect(visible, r->color());
        const QColor edge = withAlpha(QColor(r->color().red(), r->color().green(), r->color().blue()), 0.75);
        if (r->edgesVisible()) {
            const quint32 ec = packColor(edge);
            if (vertical) {
                overlaySolid.segment(float(rect.left()), float(a.top()), float(rect.left()), float(a.bottom()), 0.5f, ec);
                overlaySolid.segment(float(rect.right()), float(a.top()), float(rect.right()), float(a.bottom()), 0.5f, ec);
            } else {
                overlaySolid.segment(float(a.left()), float(rect.top()), float(a.right()), float(rect.top()), 0.5f, ec);
                overlaySolid.segment(float(a.left()), float(rect.bottom()), float(a.right()), float(rect.bottom()), 0.5f, ec);
            }
        }
        if (!r->label().isEmpty() && !visible.isEmpty())
            addText(ChipLayer, r->label(), QPointF(visible.left() + 5, visible.top() + 4), withAlpha(titleC, 0.85));
        if (r->draggable()) {
            hits.push_back({HitArea::RegionBody, visible, r});
            if (r->resizable()) {
                const qreal g = 4;
                if (vertical) {
                    hits.push_back({HitArea::RegionFrom, QRectF(rect.left() - g, a.top(), 2 * g, a.height()), r});
                    hits.push_back({HitArea::RegionTo, QRectF(rect.right() - g, a.top(), 2 * g, a.height()), r});
                } else {
                    hits.push_back({HitArea::RegionTo, QRectF(a.left(), rect.top() - g, a.width(), 2 * g), r});
                    hits.push_back({HitArea::RegionFrom, QRectF(a.left(), rect.bottom() - g, a.width(), 2 * g), r});
                }
            }
        }
    }

    // Markers.
    for (PlotMarker *mk : elementsOf<PlotMarker>()) {
        if (!mk->isVisible())
            continue;
        const double px = xMap.toPixel(mk->x()), py = yMap.toPixel(mk->displayY());
        if (!std::isfinite(px) || !std::isfinite(py) || !a.adjusted(-1, -1, 1, 1).contains(QPointF(px, py)))
            continue;
        QColor c = mk->color();
        if (!c.isValid())
            c = mk->trace() ? traceColor(mk->trace()) : titleC;
        if (mk->guides()) {
            const quint32 gc = packColor(withAlpha(c, 0.55));
            overlayDash.segment(float(px), float(py), float(px), float(a.bottom()), 0.5f, gc);
            overlayDash.segment(float(a.left()), float(py), float(px), float(py), 0.5f, gc);
        }
        const double s = mk->size();
        QRectF glyph(px - s / 2, py - s / 2, s, s);
        const quint32 fill = packColor(c), ring = packColor(areaC);
        switch (mk->shape()) {
        case PlotMarker::Circle:
            overlayShapes.shape(glyph, ShapeKind::Box, float(s / 2), fill, 1.5f, ring);
            break;
        case PlotMarker::Ring:
            overlayShapes.shape(glyph, ShapeKind::Box, float(s / 2), packColor(withAlpha(areaC, 0.6)), 1.75f, fill);
            break;
        case PlotMarker::Square:
            overlayShapes.shape(glyph, ShapeKind::Box, 1.5f, fill, 1.25f, ring);
            break;
        case PlotMarker::Diamond:
            overlayShapes.shape(glyph.adjusted(-1, -1, 1, 1), ShapeKind::Diamond, 0, fill);
            break;
        case PlotMarker::TriangleDown:
            glyph = QRectF(px - s / 2, py - s * 1.15 - 2, s, s * 0.9);
            overlayShapes.shape(glyph, ShapeKind::TriangleDown, 0, fill);
            break;
        case PlotMarker::TriangleUp:
            glyph = QRectF(px - s / 2, py + 2 + s * 0.25, s, s * 0.9);
            overlayShapes.shape(glyph, ShapeKind::TriangleUp, 0, fill);
            break;
        }
        if (!mk->label().isEmpty()) {
            const qreal w = chipWidth(mk->label()) - 2, h = chipH - 2;
            QRectF rect;
            switch (mk->labelPosition()) {
            case PlotMarker::Above: rect = QRectF(px - w / 2, glyph.top() - h - 3, w, h); break;
            case PlotMarker::Below: rect = QRectF(px - w / 2, glyph.bottom() + 3, w, h); break;
            case PlotMarker::Left: rect = QRectF(glyph.left() - w - 4, py - h / 2, w, h); break;
            case PlotMarker::Right: rect = QRectF(glyph.right() + 4, py - h / 2, w, h); break;
            }
            rect.moveLeft(std::clamp(rect.left(), a.left() + 2, a.right() - w - 2));
            rect.moveTop(std::clamp(rect.top(), a.top() + 2, a.bottom() - h - 2));
            chip(chips, ChipLayer, rect, mk->label(), withAlpha(areaC, 0.88), titleC, 1.0f, withAlpha(c, 0.7));
        }
    }

    // Reference lines with edge handles.
    for (PlotLine *l : elementsOf<PlotLine>()) {
        if (!l->isVisible())
            continue;
        const bool horizontal = l->orientation() == PlotElement::Horizontal;
        const double v = l->displayValue();
        double p = horizontal ? yMap.toPixel(v) : xMap.toPixel(v);
        if (!std::isfinite(p))
            continue;
        p = snap(p, l->width());
        const QColor c = l->color();
        const quint32 packed = packColor(c);
        const bool inside = horizontal ? (p >= a.top() - 0.5 && p <= a.bottom() + 0.5) : (p >= a.left() - 0.5 && p <= a.right() + 0.5);
        const bool showLine = l->lineMode() == PlotLine::Always || (l->lineMode() == PlotLine::WhileDragging && l->dragging());
        const float hw = float(l->width() * (l->dragging() ? 0.75 : 0.5));
        if (showLine && inside) {
            LineGeometry &g = lineFor(l->style());
            if (horizontal)
                g.segment(float(a.left()), float(p), float(a.right()), float(p), hw, packed);
            else
                g.segment(float(p), float(a.top()), float(p), float(a.bottom()), hw, packed);
            if (l->draggable()) {
                hits.push_back({HitArea::Line, horizontal ? QRectF(a.left(), p - 4, a.width(), 8) : QRectF(p - 4, a.top(), 8, a.height()), l});
            }
        }
        if (l->handle() == PlotLine::NoHandle)
            continue;
        const QString label = l->label();
        const qreal textW = label.isEmpty() ? 0 : cfm.horizontalAdvance(label);
        const bool start = l->handle() == PlotLine::Start;
        QRectF body, tip;
        ShapeKind tipKind;
        if (horizontal) {
            const double y = std::clamp(p, a.top(), a.bottom());
            const qreal bw = std::max<qreal>(14, std::ceil(textW) + 8), bh = 16;
            if (start) {
                body = QRectF(a.left() - 6 - bw, y - bh / 2, bw, bh);
                tip = QRectF(a.left() - 6.5, y - 5.5, 6.5, 11);
                tipKind = ShapeKind::TriangleRight;
            } else {
                body = QRectF(a.right() + 6, y - bh / 2, bw, bh);
                tip = QRectF(a.right(), y - 5.5, 6.5, 11);
                tipKind = ShapeKind::TriangleLeft;
            }
            if (p < a.top() - 0.5 || p > a.bottom() + 0.5) {  // off-screen: arrow shows where it is
                tip = QRectF(body.center().x() - 5, p < a.top() ? body.top() - 6 : body.bottom(), 10, 6);
                tipKind = p < a.top() ? ShapeKind::TriangleUp : ShapeKind::TriangleDown;
            }
        } else {
            const double x = std::clamp(p, a.left(), a.right());
            const qreal bw = std::max<qreal>(16, std::ceil(textW) + 8), bh = 14;
            if (start) {
                body = QRectF(x - bw / 2, a.top() - 6 - bh, bw, bh);
                tip = QRectF(x - 5.5, a.top() - 6.5, 11, 6.5);
                tipKind = ShapeKind::TriangleDown;
            } else {
                body = QRectF(x - bw / 2, a.bottom() + 6, bw, bh);
                tip = QRectF(x - 5.5, a.bottom(), 11, 6.5);
                tipKind = ShapeKind::TriangleUp;
            }
            if (p < a.left() - 0.5 || p > a.right() + 0.5) {
                tip = QRectF(p < a.left() ? body.left() - 6 : body.right(), body.center().y() - 5, 6, 10);
                tipKind = p < a.left() ? ShapeKind::TriangleLeft : ShapeKind::TriangleRight;
            }
        }
        const QColor fillC = l->dragging() ? c.lighter(115) : c;
        chips.shape(body, ShapeKind::Box, 3.0f, packColor(fillC));
        chips.shape(tip, tipKind, 0, packColor(fillC));
        if (!label.isEmpty())
            addText(ChipLayer, label, QPointF(body.center().x() - textW / 2, body.center().y() - cfm.height() / 2),
                    contrastText(c), 1);
        if (l->draggable())
            hits.push_back({HitArea::LineHandle, body.united(tip).adjusted(-3, -3, 3, 3), l});
    }

    // Cursors.
    std::vector<std::pair<double, PlotCursor *>> verticals;
    for (PlotCursor *c : elementsOf<PlotCursor>()) {
        if (!c->isVisible())
            continue;
        const bool vertical = c->orientation() == PlotElement::Vertical;
        double p = vertical ? xMap.toPixel(c->position()) : yMap.toPixel(c->position());
        if (std::isfinite(p))
            p = snap(p, c->selected() || c->dragging() ? 1.8 : 1.0);
        const bool inside = std::isfinite(p) && (vertical ? (p >= a.left() - 0.5 && p <= a.right() + 0.5)
                                                          : (p >= a.top() - 0.5 && p <= a.bottom() + 0.5));
        if (!inside)
            continue;
        const QColor col = c->color();
        const quint32 packed = packColor(col);
        const float hw = float(c->lineWidth()) * 0.5f + (c->selected() || c->dragging() ? 0.4f : 0.0f);
        LineGeometry &g = lineFor(c->style());
        if (vertical) {
            if (c->lineVisible())
                g.segment(float(p), float(a.top()), float(p), float(a.bottom()), hw, packed);
            verticals.push_back({p, c});
        } else if (c->lineVisible()) {
            g.segment(float(a.left()), float(p), float(a.right()), float(p), hw, packed);
        }
        if (c->draggable())
            hits.push_back({HitArea::CursorLine, vertical ? QRectF(p - 5, a.top(), 10, a.height()) : QRectF(a.left(), p - 5, a.width(), 10), c});

        // Position chip: top of a vertical cursor, right end of a horizontal one.
        PlotAxis *axis = vertical ? xAxis.data() : yAxis.data();
        QString text = c->label();
        if (c->readoutVisible()) {
            QString value;
            if (!vertical && c->trace()) {
                const QString unit = c->trace()->unit().isEmpty() ? yAxis->unit() : c->trace()->unit();
                value = formatEngineering(c->value(), unit, 4);
            } else {
                value = axis->formatValue(c->position(), 4);
            }
            text = text.isEmpty() ? value : text + QStringLiteral("  ") + value;
        }
        if (!text.isEmpty()) {
            const qreal w = chipWidth(text);
            QRectF rect = vertical ? QRectF(p - w / 2, a.top() + 4, w, chipH) : QRectF(a.right() - w - 4, p - chipH - 3, w, chipH);
            if (vertical)
                rect.moveLeft(std::clamp(rect.left(), a.left() + 2, a.right() - w - 2));
            else if (rect.top() < a.top() + 2)
                rect.moveTop(p + 3);
            chip(chips, ChipLayer, rect, text, c->dragging() ? col.lighter(112) : col, contrastText(col));
            if (c->draggable())
                hits.push_back({HitArea::CursorChip, rect.adjusted(-2, -2, 2, 2), c});
        }
        // Trace readout: dot at the intersection and the value next to it.
        if (vertical && c->trace() && c->readoutVisible() && c->trace()->isVisible()) {
            PlotTrace *t = c->trace();
            const double v = c->value();
            const double py = yMap.toPixel(t->toDisplayY(v));
            if (std::isfinite(py) && py >= a.top() - 1 && py <= a.bottom() + 1) {
                const QColor tc = traceColor(t);
                overlayShapes.shape(QRectF(p - 4, py - 4, 8, 8), ShapeKind::Box, 4.0f, packColor(tc), 1.5f, packColor(areaC));
                const QString unit = t->unit().isEmpty() ? yAxis->unit() : t->unit();
                const QString vt = formatEngineering(v, unit, 4);
                const qreal w = chipWidth(vt) - 2, h = chipH - 2;
                QRectF rect(p + 8, py - h / 2, w, h);
                if (rect.right() > a.right() - 2)
                    rect.moveRight(p - 8);
                rect.moveTop(std::clamp(rect.top(), a.top() + chipH + 8, a.bottom() - h - 2));
                chip(chips, ChipLayer, rect, vt, withAlpha(areaC, 0.9), titleC, 1.0f, withAlpha(tc, 0.8));
            }
        }
    }
    // Delta between the first two vertical cursors.
    if (verticals.size() >= 2 && !xAxis->isLog()) {
        auto [p0, c0] = verticals[0];
        auto [p1, c1] = verticals[1];
        if (p0 > p1) {
            std::swap(p0, p1);
            std::swap(c0, c1);
        }
        const double delta = std::abs(c1->position() - c0->position());
        QString text = QStringLiteral("Δ ") + xAxis->formatValue(delta, 4);
        if (xAxis->unit() == u"s" && delta > 0)
            text += QStringLiteral("   ") + formatEngineering(1.0 / delta, QStringLiteral("Hz"), 4);
        const qreal w = chipWidth(text);
        if (p1 - p0 > 12) {
            const qreal y = a.bottom() - chipH - 6;
            const quint32 lc = packColor(withAlpha(titleC, 0.5));
            overlayDot.segment(float(p0), float(y + chipH / 2), float(p1), float(y + chipH / 2), 0.5f, lc);
            QRectF rect(0.5 * (p0 + p1) - w / 2, y, w, chipH);
            rect.moveLeft(std::clamp(rect.left(), a.left() + 2, a.right() - w - 2));
            chip(chips, ChipLayer, rect, text, withAlpha(areaC, 0.92), titleC, 1.0f, withAlpha(titleC, 0.35));
        }
    }

    // Rubber band.
    if (!rubberRect.isEmpty()) {
        const QColor ac = accentColor();
        rubberFill.rect(rubberRect, withAlpha(ac, 0.14));
        const quint32 lc = packColor(withAlpha(ac, 0.85));
        const QRectF r = rubberRect;
        rubberLine.segment(float(r.left()), float(r.top()), float(r.right()), float(r.top()), 0.5f, lc);
        rubberLine.segment(float(r.right()), float(r.top()), float(r.right()), float(r.bottom()), 0.5f, lc);
        rubberLine.segment(float(r.right()), float(r.bottom()), float(r.left()), float(r.bottom()), 0.5f, lc);
        rubberLine.segment(float(r.left()), float(r.bottom()), float(r.left()), float(r.top()), 0.5f, lc);
    }
}

// ---------------------------------------------------------------------------------------------
// Popups: legend, hover readout, placeholder
// ---------------------------------------------------------------------------------------------

void PlotPrivate::buildPopups()
{
    popupShapes.clear();
    popupShapes.aa = aa;
    popupLines.clear();
    popupLines.aa = aa;
    texts[PopupLayer].clear();
    if (!mapsValid())
        return;
    const QRectF a = area;
    const QFontMetricsF fm(labelFont());
    const qreal lineH = std::ceil(fm.height());
    const QColor areaC = areaColor();
    const QColor titleC = titleColor();
    const QColor panel = withAlpha(panelColor(), 0.94);
    const QColor panelBorder = withAlpha(frameColor(), 1.4);

    const std::vector<PlotTrace *> all = elementsOf<PlotTrace>();
    bool anyData = false;
    for (PlotTrace *t : all)
        anyData = anyData || (t->isVisible() && t->buffer() && t->buffer()->count() > 0);
    for (PlotLogicTrace *t : elementsOf<PlotLogicTrace>())
        anyData = anyData || (t->buffer() && !t->buffer()->snapshot()->empty());
    for (PlotWaterfall *w : elementsOf<PlotWaterfall>())
        anyData = anyData || (w->buffer() && w->buffer()->rowCount() > 0);

    // Legend.
    if (legendVisible) {
        std::vector<PlotTrace *> entries;
        qreal nameW = 0;
        for (PlotTrace *t : all) {
            if (t->isVisible() && t->legendVisible() && !t->name().isEmpty()) {
                entries.push_back(t);
                nameW = std::max(nameW, fm.horizontalAdvance(t->name()));
            }
        }
        if (!entries.empty()) {
            const qreal rowH = lineH + 3, pad = 8, swatch = 16;
            const qreal w = pad + swatch + 7 + std::ceil(nameW) + pad;
            const qreal h = pad - 2 + rowH * qreal(entries.size()) + pad - 3;
            const bool right = legendPosition == Plot::TopRight || legendPosition == Plot::BottomRight;
            const bool bottom = legendPosition == Plot::BottomLeft || legendPosition == Plot::BottomRight;
            const QRectF box(right ? a.right() - w - 10 : a.left() + 10, bottom ? a.bottom() - h - 10 : a.top() + 10, w, h);
            popupShapes.shape(box, ShapeKind::Box, 6.0f, packColor(panel), 1.0f, packColor(panelBorder));
            qreal y = box.top() + pad - 2;
            for (PlotTrace *t : entries) {
                const QColor c = traceColor(t);
                const float cy = float(y + rowH / 2);
                popupLines.segment(float(box.left() + pad), cy, float(box.left() + pad + swatch), cy,
                                   float(std::min(t->width(), 3.0) * 0.5 + 0.25), packColor(c));
                addText(PopupLayer, t->name(), QPointF(box.left() + pad + swatch + 7, y + (rowH - lineH) / 2), titleC);
                y += rowH;
            }
        }
    }

    // Hover readout.
    if (hoverReadout && hovering && drag == Drag::None && a.contains(hoverPos) && anyData) {
        const double x = xMap.fromPixel(hoverPos.x());
        overlayDot.segment(float(hoverPos.x()), float(a.top()), float(hoverPos.x()), float(a.bottom()), 0.5f,
                           packColor(withAlpha(titleC, 0.45)));
        struct Row {
            QColor color;
            QString name, value;
        };
        std::vector<Row> rows;
        qreal nameW = 0, valueW = 0;
        for (PlotTrace *t : all) {
            if (!t->isVisible() || !t->buffer())
                continue;
            const TraceSnapshot s = t->buffer()->snapshot();
            if (s->empty() || !s->sorted)
                continue;
            const double dx = t->fromDisplayX(x);
            if (dx < s->xFirst() || dx > s->xLast())
                continue;
            const size_t i = s->nearest(dx);
            const double sx = t->toDisplayX(s->xAt(i)), sy = s->yAt(i);
            const double px = xMap.toPixel(sx), py = yMap.toPixel(t->toDisplayY(sy));
            const QColor c = traceColor(t);
            if (std::isfinite(py) && a.contains(QPointF(px, py)))
                overlayShapes.shape(QRectF(px - 3.5, py - 3.5, 7, 7), ShapeKind::Box, 3.5f, packColor(c), 1.5f, packColor(areaC));
            const QString unit = t->unit().isEmpty() ? yAxis->unit() : t->unit();
            Row r{c, t->name().isEmpty() ? QStringLiteral("trace") : t->name(), formatEngineering(sy, unit, 4)};
            nameW = std::max(nameW, fm.horizontalAdvance(r.name));
            valueW = std::max(valueW, fm.horizontalAdvance(r.value));
            rows.push_back(r);
        }
        if (!rows.empty()) {
            const QString header = xAxis->formatValue(x, 5);
            const qreal headerW = QFontMetricsF(chipFont()).horizontalAdvance(header);
            const qreal pad = 8, rowH = lineH + 2;
            const qreal w = std::max(headerW, 12 + nameW + 12 + valueW) + 2 * pad;
            const qreal h = pad + rowH + 2 + rowH * qreal(rows.size()) + pad - 2;
            QRectF box(hoverPos.x() + 14, hoverPos.y() + 14, w, h);
            if (box.right() > a.right() - 4)
                box.moveRight(hoverPos.x() - 14);
            if (box.bottom() > a.bottom() - 4)
                box.moveBottom(hoverPos.y() - 14);
            box.moveTop(std::max(box.top(), a.top() + 4));
            popupShapes.shape(box, ShapeKind::Box, 6.0f, packColor(panel), 1.0f, packColor(panelBorder));
            addText(PopupLayer, header, QPointF(box.left() + pad, box.top() + pad - 1), titleC, 1);
            qreal y = box.top() + pad + rowH + 2;
            for (const Row &r : rows) {
                popupShapes.shape(QRectF(box.left() + pad, y + rowH / 2 - 3.5, 7, 7), ShapeKind::Box, 3.5f, packColor(r.color));
                addText(PopupLayer, r.name, QPointF(box.left() + pad + 12, y), textColor());
                const qreal vw = fm.horizontalAdvance(r.value);
                addText(PopupLayer, r.value, QPointF(box.right() - pad - vw, y), titleC);
                y += rowH;
            }
        }
    }

    if (!anyData && !placeholder.isEmpty()) {
        const QSizeF s = textSize(placeholder, titleFont());
        addText(PopupLayer, placeholder, QPointF(a.center().x() - s.width() / 2, a.center().y() - s.height() / 2),
                withAlpha(textColor(), 0.9), 2);
    }
}

// ---------------------------------------------------------------------------------------------
// Polish
// ---------------------------------------------------------------------------------------------

void Plot::updatePolish()
{
    QElapsedTimer timer;
    timer.start();
    d->layoutCacheNext.clear();
    if (d->xAxis && d->yAxis) {
        d->applyAutoScale();
        d->computeLayout();
    }
    if (d->xAxis && d->yAxis) {
        d->buildStatic();
        d->buildTraces();
        d->buildLogic();
        d->buildOverlays();
        d->buildPopups();
    }
    for (int layer = 0; layer < TextLayerCount; ++layer) {
        quint64 h = 0x51ed;
        for (const TextItem &t : d->texts[layer]) {
            h = hashCombine(h, quint64(quintptr(t.layout.get())));
            h = hashDouble(h, t.position.x());
            h = hashDouble(h, t.position.y());
            h = hashCombine(h, t.color.rgba());
            h = hashDouble(h, t.rotation);
        }
        if (h != d->textHash[layer]) {
            d->textHash[layer] = h;
            ++d->textGeneration[layer];
        }
    }
    d->layoutCache.swap(d->layoutCacheNext);
    d->staticDirty = true;
    d->overlayDirty = true;
    d->stats.polishMs = double(timer.nsecsElapsed()) / 1e6;
    if (d->ghostsActive)
        QMetaObject::invokeMethod(this, &Plot::requestUpdate, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------------------------
// Scene graph
// ---------------------------------------------------------------------------------------------

namespace {

// Texture holding the waterfall ring (RGBA8, columns x capacity); uploads only changed rows.
class WaterfallTexture : public QSGTexture
{
public:
    ~WaterfallTexture() override = default;
    qint64 comparisonKey() const override { return qint64(quintptr(this)); }
    QRhiTexture *rhiTexture() const override { return m_texture.get(); }
    QSize textureSize() const override { return m_size; }
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }

    void resize(QSize size)
    {
        if (size == m_size)
            return;
        m_size = size;
        m_pixels.assign(size_t(std::max(0, size.width())) * size_t(std::max(0, size.height())), 0xff000000u);
        m_full = true;
        m_rows.clear();
    }
    quint32 *row(int slot) { return m_pixels.data() + size_t(slot) * size_t(m_size.width()); }
    void markRow(int slot) { m_rows.push_back(slot); }
    void markAll()
    {
        m_full = true;
        m_rows.clear();
    }
    bool pending() const { return m_full || !m_rows.empty(); }

    void commitTextureOperations(QRhi *rhi, QRhiResourceUpdateBatch *batch) override
    {
        if (m_size.isEmpty())
            return;
        if (!m_texture || m_texture->pixelSize() != m_size) {
            m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, m_size, 1));
            if (!m_texture->create()) {
                m_texture.reset();
                return;
            }
            m_full = true;
        }
        const int w = m_size.width();
        if (m_full) {
            QRhiTextureSubresourceUploadDescription desc(
                QByteArray(reinterpret_cast<const char *>(m_pixels.data()), qsizetype(m_pixels.size() * 4)));
            desc.setSourceSize(m_size);
            batch->uploadTexture(m_texture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, desc)));
        } else if (!m_rows.empty()) {
            QList<QRhiTextureUploadEntry> entries;
            for (int slot : m_rows) {
                QRhiTextureSubresourceUploadDescription desc(
                    QByteArray(reinterpret_cast<const char *>(row(slot)), qsizetype(w) * 4));
                desc.setSourceSize(QSize(w, 1));
                desc.setDestinationTopLeft(QPoint(0, slot));
                entries.append(QRhiTextureUploadEntry(0, 0, desc));
            }
            QRhiTextureUploadDescription description;
            description.setEntries(entries.cbegin(), entries.cend());
            batch->uploadTexture(m_texture.get(), description);
        }
        m_full = false;
        m_rows.clear();
    }

private:
    QSize m_size;
    std::vector<quint32> m_pixels;
    std::vector<int> m_rows;
    bool m_full = true;
    std::unique_ptr<QRhiTexture> m_texture;
};

struct WaterfallState {
    QPointer<PlotWaterfall> element;
    QSGGeometryNode *node = nullptr;
    std::unique_ptr<WaterfallTexture> texture;
    QSGTextureMaterial *material = nullptr;
    qint64 uploadedRows = -1;
    quint64 generation = ~0ull;
    quint64 styleKey = 0;
    int columns = 0;
    int capacity = 0;
};

struct TraceNodes {
    QSGNode *group = nullptr;
    QSGNode *ghostParent = nullptr;
    QSGGeometryNode *fill = nullptr;
    QSGGeometryNode *line = nullptr;
    QSGGeometryNode *dots = nullptr;
    std::vector<std::pair<quint64, QSGOpacityNode *>> ghosts;
};

class PlotRootNode : public QSGNode
{
public:
    ~PlotRootNode() override
    {
        // Materials reference the textures: drop the nodes first.
        for (auto &w : waterfalls) {
            if (w->node) {
                w->node->parent()->removeChildNode(w->node);
                delete w->node;
                w->node = nullptr;
            }
        }
    }
    QSGRectangleNode *background = nullptr;
    QSGRectangleNode *areaNode = nullptr;
    QSGClipNode *clip = nullptr;
    QSGNode *waterfallParent = nullptr;
    QSGGeometryNode *regions = nullptr, *gridMinor = nullptr, *gridMajor = nullptr, *gridAxis = nullptr;
    QSGNode *traceParent = nullptr;
    QSGGeometryNode *logicFill = nullptr, *logicLines = nullptr;
    QSGGeometryNode *overlaySolid = nullptr, *overlayDash = nullptr, *overlayDot = nullptr, *overlayShapes = nullptr;
    QSGGeometryNode *rubberFill = nullptr, *rubberLine = nullptr;
    QSGGeometryNode *frame = nullptr, *chips = nullptr;
    QSGNode *labelText = nullptr, *chipText = nullptr;
    QSGGeometryNode *popupShapes = nullptr, *popupLines = nullptr;
    QSGNode *popupText = nullptr;
    std::vector<TraceNodes> traceNodes;
    std::array<quint64, TextLayerCount> textGeneration{~0ull, ~0ull, ~0ull};
    std::vector<std::unique_ptr<WaterfallState>> waterfalls;
};

void setDash(QSGGeometryNode *node, const DashPattern &p)
{
    auto *m = static_cast<LineMaterial *>(node->material());
    if (m->dashOn == p.on && m->dashOff == p.off && m->dotLength == p.dotLength && m->dots == p.dots)
        return;
    m->dashOn = p.on;
    m->dashOff = p.off;
    m->dotLength = p.dotLength;
    m->dots = p.dots;
    node->markDirty(QSGNode::DirtyMaterial);
}

void setDash(QSGGeometryNode *node, float on, float off)
{
    setDash(node, DashPattern{on, off, 0.0f, 0.0f});
}

void rebuildText(QSGNode *parent, const std::vector<TextItem> &items, QQuickWindow *window)
{
    while (QSGNode *c = parent->firstChild()) {
        parent->removeChildNode(c);
        delete c;
    }
    if (!window)
        return;
    const QSGTextNode::RenderType renderType = window->textRenderType() == QQuickWindow::NativeTextRendering
                                                   ? QSGTextNode::NativeRendering
                                                   : QSGTextNode::QtRendering;
    std::vector<std::pair<QRgb, QSGTextNode *>> groups;
    for (const TextItem &item : items) {
        if (item.rotation != 0) {
            QSGTextNode *tn = window->createTextNode();
            tn->setRenderType(renderType);
            tn->setColor(item.color);
            QMatrix4x4 m;
            m.translate(float(item.position.x()), float(item.position.y()));
            m.rotate(float(item.rotation), 0, 0, 1);
            tn->setMatrix(m);
            tn->addTextLayout(QPointF(0, 0), item.layout.get());
            parent->appendChildNode(tn);
            continue;
        }
        QSGTextNode *tn = nullptr;
        for (auto &g : groups) {
            if (g.first == item.color.rgba()) {
                tn = g.second;
                break;
            }
        }
        if (!tn) {
            tn = window->createTextNode();
            tn->setRenderType(renderType);
            tn->setColor(item.color);
            parent->appendChildNode(tn);
            groups.push_back({item.color.rgba(), tn});
        }
        tn->addTextLayout(item.position, item.layout.get());
    }
}

}  // namespace

QSGNode *Plot::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QElapsedTimer timer;
    timer.start();
    auto *root = static_cast<PlotRootNode *>(oldNode);
    QQuickWindow *win = window();
    if (!root) {
        root = new PlotRootNode;
        root->background = win->createRectangleNode();
        root->areaNode = win->createRectangleNode();
        root->appendChildNode(root->background);
        root->appendChildNode(root->areaNode);
        root->clip = new QSGClipNode;
        root->clip->setIsRectangular(true);
        auto *clipGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        root->clip->setGeometry(clipGeometry);
        root->clip->setFlag(QSGNode::OwnsGeometry);
        root->appendChildNode(root->clip);
        root->waterfallParent = new QSGNode;
        root->clip->appendChildNode(root->waterfallParent);
        root->regions = createColorNode();
        root->gridMinor = createLineNode();
        root->gridMajor = createLineNode();
        root->gridAxis = createLineNode();
        root->traceParent = new QSGNode;
        root->logicFill = createColorNode();
        root->logicLines = createLineNode();
        root->overlaySolid = createLineNode();
        root->overlayDash = createLineNode();
        root->overlayDot = createLineNode();
        root->overlayShapes = createShapeNode();
        root->rubberFill = createColorNode();
        root->rubberLine = createLineNode();
        setDash(root->overlayDash, 5.0f, 4.0f);
        setDash(root->overlayDot, 1.5f, 3.0f);
        for (QSGNode *n : std::initializer_list<QSGNode *>{root->regions, root->gridMinor, root->gridMajor, root->gridAxis,
                                                         root->traceParent, root->logicFill, root->logicLines,
                                                         root->overlaySolid, root->overlayDash, root->overlayDot,
                                                         root->overlayShapes, root->rubberFill, root->rubberLine})
            root->clip->appendChildNode(n);
        root->frame = createLineNode();
        root->labelText = new QSGNode;
        root->chips = createShapeNode();
        root->chipText = new QSGNode;
        root->popupShapes = createShapeNode();
        root->popupLines = createLineNode();
        root->popupText = new QSGNode;
        for (QSGNode *n : std::initializer_list<QSGNode *>{root->frame, root->labelText, root->chips, root->chipText,
                                                         root->popupShapes, root->popupLines, root->popupText})
            root->appendChildNode(n);
    }

    const QRectF bounds = boundingRect();
    if (d->staticDirty) {
        root->background->setRect(bounds);
        root->background->setColor(d->backgroundColor());
        root->areaNode->setRect(d->area.isValid() ? d->area : bounds);
        root->areaNode->setColor(d->areaColor());
        const QRectF clipRect = d->area.isValid() ? d->area : bounds;
        root->clip->setClipRect(clipRect);
        QSGGeometry::updateRectGeometry(root->clip->geometry(), clipRect);
        root->clip->markDirty(QSGNode::DirtyGeometry);
        upload(root->gridMinor, d->gridMinor);
        upload(root->gridMajor, d->gridMajor);
        setDash(root->gridMajor, d->majorDashOn, d->majorDashOff);
        upload(root->gridAxis, d->gridAxis);
        upload(root->frame, d->frameLines);
        d->staticDirty = false;
    }

    // Traces.
    qsizetype vertices = 0;
    if (d->traceListChanged || root->traceNodes.size() != d->traces.size()) {
        for (TraceNodes &n : root->traceNodes) {
            root->traceParent->removeChildNode(n.group);
            delete n.group;
        }
        root->traceNodes.clear();
        for (size_t i = 0; i < d->traces.size(); ++i) {
            TraceNodes n;
            n.group = new QSGNode;
            n.ghostParent = new QSGNode;
            n.fill = createColorNode();
            n.line = createLineNode();
            n.dots = createShapeNode();
            n.group->appendChildNode(n.ghostParent);
            n.group->appendChildNode(n.fill);
            n.group->appendChildNode(n.line);
            n.group->appendChildNode(n.dots);
            root->traceParent->appendChildNode(n.group);
            root->traceNodes.push_back(n);
            d->traces[i]->dirty = true;
        }
        d->traceListChanged = false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (size_t i = 0; i < d->traces.size(); ++i) {
        TraceGeom &g = *d->traces[i];
        TraceNodes &n = root->traceNodes[i];
        vertices += qsizetype(g.line.vertices.size());
        if (g.dirty) {
            upload(n.line, g.line);
            setDash(n.line, g.dash);
            upload(n.fill, g.fill);
            upload(n.dots, g.dots);
        }
        // Persistence ghosts: nodes are created once per record; only their opacity changes.
        const int persistence = g.trace ? g.trace->persistence() : 0;
        for (auto it = n.ghosts.begin(); it != n.ghosts.end();) {
            const bool alive = std::any_of(g.ghosts.begin(), g.ghosts.end(), [&](const Ghost &gh) { return gh.id == it->first; });
            if (!alive) {
                n.ghostParent->removeChildNode(it->second);
                delete it->second;
                it = n.ghosts.erase(it);
            } else {
                ++it;
            }
        }
        for (const Ghost &gh : g.ghosts) {
            auto it = std::find_if(n.ghosts.begin(), n.ghosts.end(), [&](const auto &p) { return p.first == gh.id; });
            QSGOpacityNode *on;
            if (it == n.ghosts.end()) {
                on = new QSGOpacityNode;
                auto *material = new LineMaterial;
                material->additive = true;
                material->dashOn = g.dash.on;
                material->dashOff = g.dash.off;
                material->dotLength = g.dash.dotLength;
                material->dots = g.dash.dots;
                QSGGeometryNode *ln = createLineNode(material);
                upload(ln, *gh.geometry);
                on->appendChildNode(ln);
                n.ghostParent->appendChildNode(on);
                n.ghosts.push_back({gh.id, on});
            } else {
                on = it->second;
            }
            const double age = persistence > 0 ? double(now - gh.time) / persistence : 1.0;
            const double fade = std::clamp(1.0 - age, 0.0, 1.0);
            on->setOpacity(0.38 * fade * fade);
        }
        g.dirty = false;
    }

    if (d->overlayDirty) {
        upload(root->regions, d->regions);
        upload(root->logicFill, d->logicFill);
        upload(root->logicLines, d->logicLines);
        upload(root->overlaySolid, d->overlaySolid);
        upload(root->overlayDash, d->overlayDash);
        upload(root->overlayDot, d->overlayDot);
        upload(root->overlayShapes, d->overlayShapes);
        upload(root->rubberFill, d->rubberFill);
        upload(root->rubberLine, d->rubberLine);
        upload(root->chips, d->chips);
        upload(root->popupShapes, d->popupShapes);
        upload(root->popupLines, d->popupLines);
        d->overlayDirty = false;
    }

    QSGNode *textParents[TextLayerCount] = {root->labelText, root->chipText, root->popupText};
    for (int layer = 0; layer < TextLayerCount; ++layer) {
        if (root->textGeneration[layer] != d->textGeneration[layer]) {
            rebuildText(textParents[layer], d->texts[layer], win);
            root->textGeneration[layer] = d->textGeneration[layer];
        }
    }

    // Waterfalls.
    const std::vector<PlotWaterfall *> falls = d->elementsOf<PlotWaterfall>();
    for (auto it = root->waterfalls.begin(); it != root->waterfalls.end();) {
        WaterfallState &w = **it;
        const bool keep = w.element && std::find(falls.begin(), falls.end(), w.element.data()) != falls.end();
        if (!keep) {
            root->waterfallParent->removeChildNode(w.node);
            delete w.node;
            w.node = nullptr;
            it = root->waterfalls.erase(it);
        } else {
            ++it;
        }
    }
    for (PlotWaterfall *wf : falls) {
        auto it = std::find_if(root->waterfalls.begin(), root->waterfalls.end(),
                               [wf](const auto &s) { return s->element == wf; });
        WaterfallState *w;
        if (it == root->waterfalls.end()) {
            auto state = std::make_unique<WaterfallState>();
            state->element = wf;
            state->texture = std::make_unique<WaterfallTexture>();
            state->node = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0, 0, QSGGeometry::UnsignedIntType);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            state->node->setGeometry(geometry);
            state->node->setFlag(QSGNode::OwnsGeometry);
            state->material = new QSGTextureMaterial;
            state->material->setTexture(state->texture.get());
            state->node->setMaterial(state->material);
            state->node->setFlag(QSGNode::OwnsMaterial);
            root->waterfallParent->appendChildNode(state->node);
            w = state.get();
            root->waterfalls.push_back(std::move(state));
        } else {
            w = it->get();
        }
        WaterfallBuffer *buffer = wf->buffer();
        QSGGeometry *geometry = w->node->geometry();
        if (!wf->isVisible() || !buffer || !d->mapsValid()) {
            geometry->allocate(0, 0);
            w->node->markDirty(QSGNode::DirtyGeometry);
            continue;
        }
        const int columns = buffer->columns();
        const int capacity = buffer->capacity();
        const qint64 rows = buffer->rowCount();
        const quint64 generation = buffer->generation();
        quint64 styleKey = hashCombine(0, quint64(wf->colormap()));
        styleKey = hashDouble(styleKey, wf->minimum());
        styleKey = hashDouble(styleKey, wf->maximum());
        if (columns <= 0 || rows <= 0) {
            geometry->allocate(0, 0);
            w->node->markDirty(QSGNode::DirtyGeometry);
            w->uploadedRows = rows;
            w->generation = generation;
            continue;
        }
        const bool full = generation != w->generation || columns != w->columns || capacity != w->capacity
                          || styleKey != w->styleKey || rows - w->uploadedRows > capacity || w->uploadedRows < 0;
        w->texture->resize(QSize(columns, capacity));
        std::vector<float> values;
        int readColumns = 0;
        const qint64 from = full ? rows - capacity : w->uploadedRows;
        const qint64 first = buffer->readRows(from, rows - from, values, &readColumns);
        if (readColumns == columns) {
            const auto &lut = PlotWaterfall::lut(wf->colormap());
            const double lo = wf->minimum(), span = wf->maximum() - wf->minimum();
            const double scale = span != 0 ? 255.0 / span : 0.0;
            const qint64 count = qint64(values.size()) / std::max(1, columns);
            for (qint64 r = 0; r < count; ++r) {
                const int slot = int((first + r) % capacity);
                quint32 *dst = w->texture->row(slot);
                const float *src = values.data() + size_t(r) * size_t(columns);
                for (int c = 0; c < columns; ++c) {
                    const double t = (double(src[c]) - lo) * scale;
                    const int idx = std::isfinite(t) ? std::clamp(int(t), 0, 255) : 0;
                    dst[c] = lut[size_t(idx)];
                }
                if (!full)
                    w->texture->markRow(slot);
            }
            if (full)
                w->texture->markAll();
        }
        w->uploadedRows = rows;
        w->generation = generation;
        w->columns = columns;
        w->capacity = capacity;
        w->styleKey = styleKey;
        const QSGTexture::Filtering filter = wf->smooth() ? QSGTexture::Linear : QSGTexture::Nearest;
        w->material->setFiltering(filter);
        w->material->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        w->material->setVerticalWrapMode(QSGTexture::ClampToEdge);

        // Geometry: two strips (ring split), tessellated at bin edges on log x axes.
        const double x0 = buffer->x0(), dx = buffer->dx();
        const qint64 kept = std::min<qint64>(rows, capacity);
        const int head = int((rows - 1) % capacity);
        std::vector<double> edgePx;
        std::vector<float> edgeU;
        const bool logX = d->xMap.isLog();
        const int steps = logX ? columns : 1;
        for (int i = 0; i <= steps; ++i) {
            const double bin = logX ? double(i) : double(i) * columns;
            const double xv = x0 + (bin - 0.5) * dx;
            const double px = d->xMap.toPixel(xv);
            if (!std::isfinite(px))
                continue;
            edgePx.push_back(px);
            edgeU.push_back(float(bin / columns));
        }
        std::vector<QSGGeometry::TexturedPoint2D> verts;
        std::vector<quint32> idx;
        auto strip = [&](double ageTop, double ageBottom, float vTop, float vBottom) {
            const float yTop = float(d->yMap.toPixel(wf->yOrigin() - ageTop * wf->rowHeight()));
            const float yBottom = float(d->yMap.toPixel(wf->yOrigin() - ageBottom * wf->rowHeight()));
            const quint32 base = quint32(verts.size());
            for (size_t i = 0; i < edgePx.size(); ++i) {
                QSGGeometry::TexturedPoint2D top, bottom;
                top.set(float(edgePx[i]), yTop, edgeU[i], vTop);
                bottom.set(float(edgePx[i]), yBottom, edgeU[i], vBottom);
                verts.push_back(top);
                verts.push_back(bottom);
            }
            for (size_t i = 0; i + 1 < edgePx.size(); ++i) {
                const quint32 k = base + quint32(i * 2);
                idx.insert(idx.end(), {k, k + 1, k + 3, k, k + 3, k + 2});
            }
        };
        if (edgePx.size() >= 2) {
            const float cap = float(capacity);
            // Newest rows: slots head .. 0 (ages 0 .. head).
            const qint64 countA = std::min<qint64>(kept, head + 1);
            strip(0.0, double(countA), float(head + 1) / cap, float(head + 1 - countA) / cap);
            const qint64 countB = kept - countA;
            if (countB > 0)
                strip(double(countA), double(kept), 1.0f, float(capacity - countB) / cap);
        }
        if (geometry->vertexCount() != int(verts.size()) || geometry->indexCount() != int(idx.size()))
            geometry->allocate(int(verts.size()), int(idx.size()));
        if (!verts.empty())
            std::memcpy(geometry->vertexData(), verts.data(), verts.size() * sizeof(QSGGeometry::TexturedPoint2D));
        if (!idx.empty())
            std::memcpy(geometry->indexData(), idx.data(), idx.size() * sizeof(quint32));
        w->node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

    d->stats.vertices = vertices;
    d->stats.syncMs = double(timer.nsecsElapsed()) / 1e6;
    return root;
}

void Plot::releaseResources()
{
    // Nodes (and their textures) are released by the scene graph with the item's node tree.
}

}  // namespace mybench::plot
