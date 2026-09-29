// Private implementation of Plot (shared by plot.cpp and plotrender.cpp).
#pragma once

#include "mybench/plot/plot.h"
#include "mybench/plot/waterfall.h"
#include "plotmaterials.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTextLayout>

#include <array>
#include <deque>
#include <memory>
#include <vector>

namespace mybench::plot {

struct TextItem {
    std::shared_ptr<QTextLayout> layout;
    QPointF position;    // top-left of the layout
    QColor color;
    qreal rotation = 0;  // degrees, around `position`
};

enum TextLayer { LabelLayer = 0, ChipLayer = 1, PopupLayer = 2, TextLayerCount = 3 };

struct Ghost {
    std::shared_ptr<const LineGeometry> geometry;
    qint64 time = 0;
    quint64 id = 0;
};

struct TraceGeom {
    QPointer<PlotTrace> trace;
    quint64 key = 0;           // inputs of the last build
    quint64 dataVersion = 0;
    const void *buffer = nullptr;
    std::vector<PixelPoint> points;
    PolylineInfo info;
    LineGeometry line;
    ColorGeometry fill;
    ShapeGeometry dots;
    DashPattern dash;
    bool dirty = true;         // geometry changed since the last upload
    std::deque<Ghost> ghosts;  // persistence
};

struct HitArea {
    enum Kind { CursorLine, CursorChip, Line, LineHandle, RegionBody, RegionFrom, RegionTo };
    Kind kind;
    QRectF rect;
    QPointer<PlotElement> element;
};

struct PlotPrivate {
    explicit PlotPrivate(Plot *plot);

    Plot *q;
    PlotAxis *ownX = nullptr;
    PlotAxis *ownY = nullptr;
    QPointer<PlotAxis> xAxis;
    QPointer<PlotAxis> yAxis;

    // ---- elements ----
    struct Entry {
        QPointer<PlotElement> element;
        quint64 order = 0;
    };
    std::vector<Entry> elements;
    quint64 attachCounter = 0;
    template <typename T>
    std::vector<T *> elementsOf() const;
    void sortElements();

    // ---- style ----
    Plot::ColorScheme scheme = Plot::Dark;
    std::optional<QColor> background, plotAreaColor, grid, minorGrid, frame, text, title, accent;
    Plot::Graticule graticule = Plot::Grid;
    PlotElement::LineStyle gridStyle = PlotElement::Solid;
    bool frameVisible = true;
    QFont font;
    bool fontSet = false;
    QString placeholder;
    bool xLabels = true;
    bool yLabels = true;
    bool showXLabels() const { return xLabels && xAxis && xAxis->labelsVisible(); }
    bool showYLabels() const { return yLabels && yAxis && yAxis->labelsVisible(); }
    qreal margins[4] = {-1, -1, -1, -1};  // left, right, top, bottom
    bool interactive = true;
    Plot::DragMode dragMode = Plot::Pan;
    bool hoverReadout = false;
    bool legendVisible = false;
    Plot::Corner legendPosition = Plot::TopRight;

    QColor color(const std::optional<QColor> &value, QRgb dark, QRgb light, int darkAlpha = 255,
                 int lightAlpha = 255) const;
    QColor backgroundColor() const;
    QColor areaColor() const;
    QColor gridColor() const;
    QColor minorGridColor() const;
    QColor frameColor() const;
    QColor textColor() const;
    QColor titleColor() const;
    QColor accentColor() const;
    QColor palette(int index) const;
    QColor traceColor(const PlotTrace *trace) const;

    // ---- layout (valid after polish) ----
    QRectF area;
    qreal implicitLeft = 0;
    ScaleMap xMap;
    ScaleMap yMap;
    TickSet xTicks;
    TickSet yTicks;
    qreal dpr = 1.0;
    float aa = 1.0f;

    // ---- geometry (built in updatePolish, uploaded in updatePaintNode) ----
    LineGeometry gridMinor, gridMajor, gridAxis, frameLines;
    float majorDashOn = 0, majorDashOff = 0, minorDashOn = 0, minorDashOff = 0;
    ColorGeometry regions;
    std::vector<std::unique_ptr<TraceGeom>> traces;
    bool traceListChanged = true;
    LineGeometry logicLines;
    ColorGeometry logicFill;
    LineGeometry overlaySolid, overlayDash, overlayDot;
    ShapeGeometry overlayShapes;
    ShapeGeometry chips;
    ShapeGeometry popupShapes;
    LineGeometry popupLines;
    ColorGeometry rubberFill;
    LineGeometry rubberLine;
    std::array<std::vector<TextItem>, TextLayerCount> texts;
    std::array<quint64, TextLayerCount> textHash{};
    std::array<quint64, TextLayerCount> textGeneration{};
    bool staticDirty = true;     // background/grid/frame geometry changed
    bool overlayDirty = true;
    quint64 ghostCounter = 0;
    bool ghostsActive = false;

    // Text layout cache (reused across frames while the text stays the same).
    QHash<QString, std::shared_ptr<QTextLayout>> layoutCache;
    QHash<QString, std::shared_ptr<QTextLayout>> layoutCacheNext;
    QFont labelFont() const;
    QFont chipFont() const;
    QFont titleFont() const;
    std::shared_ptr<QTextLayout> layout(const QString &text, const QFont &font, int fontId);
    QSizeF textSize(const QString &text, const QFont &font) const;
    // Adds text with its top-left at `pos`; returns its size.
    QSizeF addText(TextLayer layer, const QString &text, QPointF pos, const QColor &color, int fontId = 0,
                   qreal rotation = 0);

    // ---- interaction ----
    enum class Drag { None, Pan, Zoom, Cursor, Line, RegionBody, RegionFrom, RegionTo };
    Drag drag = Drag::None;
    QPointer<PlotElement> dragElement;
    QPointF pressPos;
    QPointF lastPos;
    bool moved = false;
    ScaleMap pressXMap, pressYMap;
    double grabOffset = 0.0;
    double grabFrom = 0.0, grabTo = 0.0;
    QPointF hoverPos;
    bool hovering = false;
    QPointer<PlotElement> selected;
    std::vector<HitArea> hits;
    QRectF rubberRect;
    Plot::ZoomAxes dragAxes = Plot::ZoomXY;   // axes of the zoom drag in progress (axis lock)
    Plot::ZoomAxes zoomAxes = Plot::ZoomXY;
    bool panPushed = false;  // the current pan drag has pushed its history entry

    // ---- zoom history / autoscale locks ----
    struct ViewState {
        double x0, x1, y0, y1;
        bool operator==(const ViewState &) const = default;
    };
    std::vector<ViewState> zoomHistory;
    QElapsedTimer coalesceTimer;  // wheel/pinch bursts make one history entry
    int historyGuard = 0;         // > 0: pushHistory() is a no-op (inside a wheel/pinch step)
    bool xLock = false;
    bool yLock = false;
    ViewState viewState() const;
    // Pushes the current ranges (before a user view change). `coalesce`: skip when the previous
    // coalesced push was less than 600 ms ago (wheel/pinch bursts).
    void pushHistory(bool coalesce = false);
    void unlock(bool x, bool y);
    void applyLocks();
    void fitX();
    void fitY();

    // ---- stats ----
    Plot::FrameStats stats;

    // ---- helpers ----
    bool mapsValid() const { return xMap.isValid() && yMap.isValid(); }
    void computeLayout();
    void buildStatic();
    void buildTraces();
    void buildLogic();
    void buildOverlays();
    void buildPopups();
    void applyAutoScale();
    const HitArea *hitTest(QPointF p) const;
    double snapCursor(PlotCursor *cursor, double position) const;
    bool dataXRange(double *lo, double *hi) const;
    bool dataYRange(double xLo, double xHi, double *lo, double *hi) const;
};

template <typename T>
std::vector<T *> PlotPrivate::elementsOf() const
{
    std::vector<T *> out;
    for (const Entry &e : elements) {
        if (auto *t = qobject_cast<T *>(e.element.data()))
            out.push_back(t);
    }
    return out;
}

}  // namespace mybench::plot
