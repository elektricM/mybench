// Plot: GPU scene-graph 2D plot (QQuickItem + QSGGeometryNodes, RHI: Metal/Vulkan/D3D/OpenGL).
//
//     import MyBench.Plot
//     Plot {
//         xAxis { min: 0; max: 10e-3; unit: "s"; title: "Time" }
//         yAxis { min: -4; max: 4; divisions: 8; labelsVisible: false }
//         graticule: Plot.Scope
//         PlotTrace { buffer: scope.ch0; color: "#3ddc84"; yScale: 1 / scope.voltsPerDiv0 }
//         PlotCursor { position: 2e-3; label: "C1" }
//     }
//
// Axes: xAxis/yAxis are PlotAxis objects (grouped properties, or assign a shared PlotAxis).
// Elements (PlotTrace, PlotLogicTrace, PlotWaterfall, PlotCursor, PlotLine, PlotMarker,
// PlotRegion) declared inside the Plot attach themselves; elsewhere set `plot: thePlot`.
//
// Interaction (when `interactive`, per axis `PlotAxis.interactive`):
//   wheel: zoom x at the pointer · Shift+wheel: zoom y · Ctrl/⌘+wheel: zoom x and y ·
//   Alt+wheel or horizontal trackpad scroll: pan x · trackpad pinch: zoom x (Shift: y) ·
//   drag: pan (dragMode Pan) or rubber-band zoom (dragMode Zoom, Shift+drag, right drag) ·
//   double-click: reset to the home ranges · drag cursors / lines / handles / regions ·
//   click a cursor or line, then arrow keys nudge it 1 px (Shift: 10 px) · +/−: zoom x ·
//   Home or 0: reset · Esc: deselect.
#pragma once

#include "mybench/plot/plotaxis.h"
#include "mybench/plot/plotelements.h"

#include <QColor>
#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

namespace mybench::plot {

struct PlotPrivate;

class Plot : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::PlotAxis *xAxis READ xAxis WRITE setXAxis NOTIFY xAxisChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotAxis *yAxis READ yAxis WRITE setYAxis NOTIFY yAxisChanged FINAL)

    Q_PROPERTY(ColorScheme colorScheme READ colorScheme WRITE setColorScheme NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor RESET resetBackgroundColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor plotAreaColor READ plotAreaColor WRITE setPlotAreaColor RESET resetPlotAreaColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor RESET resetGridColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor minorGridColor READ minorGridColor WRITE setMinorGridColor RESET resetMinorGridColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor frameColor READ frameColor WRITE setFrameColor RESET resetFrameColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor RESET resetTextColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor titleColor READ titleColor WRITE setTitleColor RESET resetTitleColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor RESET resetAccentColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(Graticule graticule READ graticule WRITE setGraticule NOTIFY styleChanged FINAL)
    Q_PROPERTY(mybench::plot::PlotElement::LineStyle gridStyle READ gridStyle WRITE setGridStyle NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool frameVisible READ frameVisible WRITE setFrameVisible NOTIFY styleChanged FINAL)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY styleChanged FINAL)
    Q_PROPERTY(QString placeholderText READ placeholderText WRITE setPlaceholderText NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool xLabelsVisible READ xLabelsVisible WRITE setXLabelsVisible NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool yLabelsVisible READ yLabelsVisible WRITE setYLabelsVisible NOTIFY styleChanged FINAL)

    Q_PROPERTY(qreal leftMargin READ leftMargin WRITE setLeftMargin NOTIFY marginsChanged FINAL)
    Q_PROPERTY(qreal rightMargin READ rightMargin WRITE setRightMargin NOTIFY marginsChanged FINAL)
    Q_PROPERTY(qreal topMargin READ topMargin WRITE setTopMargin NOTIFY marginsChanged FINAL)
    Q_PROPERTY(qreal bottomMargin READ bottomMargin WRITE setBottomMargin NOTIFY marginsChanged FINAL)
    Q_PROPERTY(QRectF plotArea READ plotArea NOTIFY plotAreaChanged FINAL)
    Q_PROPERTY(qreal implicitLeftMargin READ implicitLeftMargin NOTIFY plotAreaChanged FINAL)

    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive NOTIFY interactionChanged FINAL)
    Q_PROPERTY(DragMode dragMode READ dragMode WRITE setDragMode NOTIFY interactionChanged FINAL)
    Q_PROPERTY(bool hoverReadout READ hoverReadout WRITE setHoverReadout NOTIFY interactionChanged FINAL)
    Q_PROPERTY(bool legendVisible READ legendVisible WRITE setLegendVisible NOTIFY styleChanged FINAL)
    Q_PROPERTY(Corner legendPosition READ legendPosition WRITE setLegendPosition NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool containsMouse READ containsMouse NOTIFY hoverChanged FINAL)
    Q_PROPERTY(QPointF hoverValue READ hoverValue NOTIFY hoverChanged FINAL)

public:
    enum ColorScheme { Dark, Light };
    Q_ENUM(ColorScheme)
    // Grid: lines at the axis ticks (major + minor). Scope: fixed divisions (xAxis/yAxis
    // `divisions`, default 10 × 8), dotted division lines, center axes with minor tick marks.
    enum Graticule { Grid, Scope, NoGraticule };
    Q_ENUM(Graticule)
    enum DragMode { Pan, Zoom, NoDrag };
    Q_ENUM(DragMode)
    enum Corner { TopLeft, TopRight, BottomLeft, BottomRight };
    Q_ENUM(Corner)

    explicit Plot(QQuickItem *parent = nullptr);
    ~Plot() override;

    PlotAxis *xAxis() const;
    void setXAxis(PlotAxis *axis);
    PlotAxis *yAxis() const;
    void setYAxis(PlotAxis *axis);

    ColorScheme colorScheme() const;
    void setColorScheme(ColorScheme scheme);
#define MYBENCH_PLOT_COLOR(name, Name)                                                             \
    QColor name() const;                                                                           \
    void set##Name(const QColor &c);                                                               \
    void reset##Name();
    MYBENCH_PLOT_COLOR(backgroundColor, BackgroundColor)
    MYBENCH_PLOT_COLOR(plotAreaColor, PlotAreaColor)
    MYBENCH_PLOT_COLOR(gridColor, GridColor)
    MYBENCH_PLOT_COLOR(minorGridColor, MinorGridColor)
    MYBENCH_PLOT_COLOR(frameColor, FrameColor)
    MYBENCH_PLOT_COLOR(textColor, TextColor)
    MYBENCH_PLOT_COLOR(titleColor, TitleColor)
    MYBENCH_PLOT_COLOR(accentColor, AccentColor)
#undef MYBENCH_PLOT_COLOR
    Graticule graticule() const;
    void setGraticule(Graticule g);
    PlotElement::LineStyle gridStyle() const;
    void setGridStyle(PlotElement::LineStyle s);
    bool frameVisible() const;
    void setFrameVisible(bool on);
    QFont font() const;
    void setFont(const QFont &f);
    // Shown centered in the plot area while no trace has data ("Waiting for trigger…").
    QString placeholderText() const;
    void setPlaceholderText(const QString &t);
    // Per-plot override of the axes' labelsVisible (and titles), e.g. to hide the x labels of the
    // upper plot of a Bode pair sharing one PlotAxis.
    bool xLabelsVisible() const;
    void setXLabelsVisible(bool on);
    bool yLabelsVisible() const;
    void setYLabelsVisible(bool on);

    // Margins around the plot area in px; negative (default) = automatic from the labels.
    qreal leftMargin() const;
    void setLeftMargin(qreal m);
    qreal rightMargin() const;
    void setRightMargin(qreal m);
    qreal topMargin() const;
    void setTopMargin(qreal m);
    qreal bottomMargin() const;
    void setBottomMargin(qreal m);
    QRectF plotArea() const;
    // The automatic left margin (bind stacked plots' leftMargin to the max of these).
    qreal implicitLeftMargin() const;

    bool interactive() const;
    void setInteractive(bool on);
    DragMode dragMode() const;
    void setDragMode(DragMode m);
    // Crosshair + tooltip with every trace's value at the pointer.
    bool hoverReadout() const;
    void setHoverReadout(bool on);
    bool legendVisible() const;
    void setLegendVisible(bool on);
    Corner legendPosition() const;
    void setLegendPosition(Corner c);
    bool containsMouse() const;
    QPointF hoverValue() const;

    // ---- mapping (item coordinates <-> axis values; log aware) --------------------------------
    Q_INVOKABLE QPointF mapToValue(QPointF point) const;
    Q_INVOKABLE QPointF mapFromValue(QPointF value) const;
    Q_INVOKABLE double xToPixel(double x) const;
    Q_INVOKABLE double yToPixel(double y) const;
    Q_INVOKABLE double pixelToX(double px) const;
    Q_INVOKABLE double pixelToY(double py) const;

    // Engineering-notation readout text: format(0.00125, "s") == "1.250 ms" (U+2212 minus).
    Q_INVOKABLE QString format(double value, const QString &unit = QString(), int digits = 4) const;

    // ---- view ---------------------------------------------------------------------------------
    // Fit the visible range to the data of the visible traces (y over the visible x range).
    Q_INVOKABLE void autoscale();
    Q_INVOKABLE void autoscaleX();
    Q_INVOKABLE void autoscaleY();
    // Home ranges of both axes.
    Q_INVOKABLE void resetView();
    // Zoom around an item point (factor > 1 zooms in; 1 leaves that axis).
    Q_INVOKABLE void zoomAt(QPointF point, double factorX, double factorY);

    // ---- elements -----------------------------------------------------------------------------
    Q_INVOKABLE void addElement(mybench::plot::PlotElement *element);
    Q_INVOKABLE void removeElement(mybench::plot::PlotElement *element);
    QList<PlotElement *> elements() const;
    // Resolved color of a trace (its color, or the palette color for its index).
    Q_INVOKABLE QColor traceColor(mybench::plot::PlotTrace *trace) const;
    // Palette color i (CH0 green, CH1 cyan, amber, violet, ...), for the current color scheme.
    Q_INVOKABLE QColor paletteColor(int index) const;

    // ---- export -------------------------------------------------------------------------------
    // PNG of the whole item (asynchronous; imageSaved() reports the result).
    Q_INVOKABLE bool saveImage(const QUrl &file);
    // CSV of the visible traces (x range of the view, or everything).
    Q_INVOKABLE bool saveCsv(const QUrl &file, bool visibleRangeOnly = true) const;
    Q_INVOKABLE QString toCsv(bool visibleRangeOnly = true) const;

    // Statistics of the last rendered frame (for benchmarks): polish and sync times in ms,
    // number of line vertices uploaded.
    struct FrameStats {
        double polishMs = 0;
        double syncMs = 0;
        qsizetype vertices = 0;
    };
    FrameStats frameStats() const;

signals:
    void xAxisChanged();
    void yAxisChanged();
    void styleChanged();
    void marginsChanged();
    void plotAreaChanged();
    void interactionChanged();
    void hoverChanged();
    // A click (without drag) in the plot area, in axis values.
    void clicked(double x, double y);
    void doubleClicked(double x, double y);
    void viewReset();
    void imageSaved(const QString &path, bool ok);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void releaseResources() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    friend struct PlotPrivate;
    void requestUpdate();
    void connectAxis(PlotAxis *axis);
    std::unique_ptr<PlotPrivate> d;
};

}  // namespace mybench::plot
