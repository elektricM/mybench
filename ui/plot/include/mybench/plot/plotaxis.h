// PlotAxis: range, scale (linear/log10), ticks and labels of one plot axis.
//
// Each Plot has its own xAxis/yAxis (use grouped properties: `xAxis.min: 0`), or can share an
// axis object with other plots (`PlotAxis { id: freq }  Plot { xAxis: freq }`), e.g. Bode gain
// and phase plots panning/zooming together.
//
// Range: min/max are the visible range. Values assigned from QML/C++ (bindings to instrument
// settings) also become the "home" range; interactive zoom/pan changes only the visible range
// (zoomed = true) and reset() returns home.
#pragma once

#include "mybench/plot/scale.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace mybench::plot {

class PlotAxis : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double min READ min WRITE setMin NOTIFY rangeChanged FINAL)
    Q_PROPERTY(double max READ max WRITE setMax NOTIFY rangeChanged FINAL)
    Q_PROPERTY(Scale scale READ scale WRITE setScale NOTIFY scaleChanged FINAL)
    Q_PROPERTY(int divisions READ divisions WRITE setDivisions NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(int minorTicks READ minorTicks WRITE setMinorTicks NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(double tickSpacing READ tickSpacing WRITE setTickSpacing NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool unitInLabels READ unitInLabels WRITE setUnitInLabels NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool labelsVisible READ labelsVisible WRITE setLabelsVisible NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool minorGridVisible READ minorGridVisible WRITE setMinorGridVisible NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool inverted READ inverted WRITE setInverted NOTIFY appearanceChanged FINAL)
    // Log axes: Auto labels 1-2-5 of each decade when there is room; Decades labels only the
    // decades (10, 100, 1k...) with unlabeled minor lines at 2..9 (ELVISmx Bode).
    Q_PROPERTY(LogLabels logLabels READ logLabels WRITE setLogLabels NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(LabelFormat labelFormat READ labelFormat WRITE setLabelFormat NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(int decimals READ decimals WRITE setDecimals NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive NOTIFY interactiveChanged FINAL)
    Q_PROPERTY(double lowerLimit READ lowerLimit WRITE setLowerLimit NOTIFY limitsChanged FINAL)
    Q_PROPERTY(double upperLimit READ upperLimit WRITE setUpperLimit NOTIFY limitsChanged FINAL)
    Q_PROPERTY(double minimumSpan READ minimumSpan WRITE setMinimumSpan NOTIFY limitsChanged FINAL)
    // Largest visible span (linear: axis units; log: decades). NaN: unlimited.
    Q_PROPERTY(double maximumSpan READ maximumSpan WRITE setMaximumSpan NOTIFY limitsChanged FINAL)
    // Keep the view over the data: interactive pan/zoom stays within the extent of the traces
    // (all x, resp. all y) plus `limitMargin` x that extent on each side, so the data can never
    // scroll out of sight. An explicit lowerLimit/upperLimit wins for its side.
    Q_PROPERTY(bool limitToData READ limitToData WRITE setLimitToData NOTIFY limitsChanged FINAL)
    Q_PROPERTY(double limitMargin READ limitMargin WRITE setLimitMargin NOTIFY limitsChanged FINAL)
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale NOTIFY autoScaleChanged FINAL)
    Q_PROPERTY(double autoScaleMargin READ autoScaleMargin WRITE setAutoScaleMargin NOTIFY autoScaleChanged FINAL)
    Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY followChanged FINAL)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY rangeChanged FINAL)
    Q_PROPERTY(double span READ span NOTIFY rangeChanged FINAL)

public:
    enum Scale { Linear, Log };
    Q_ENUM(Scale)
    enum LabelFormat { Engineering, Fixed, Time };
    enum LogLabels { AutoLogLabels, Decades };
    Q_ENUM(LabelFormat)

    explicit PlotAxis(QObject *parent = nullptr);

    double min() const { return m_min; }
    void setMin(double v);
    double max() const { return m_max; }
    void setMax(double v);
    Scale scale() const { return m_scale; }
    void setScale(Scale s);
    // 0: automatic nice ticks; N > 0: N equal divisions (scope graticule, ticks at divisions).
    int divisions() const { return m_divisions; }
    void setDivisions(int d);
    // Minor intervals per major (or per division); -1: automatic.
    int minorTicks() const { return m_minorTicks; }
    void setMinorTicks(int n);
    // Target distance between major ticks in pixels (automatic ticks). Default 90.
    double tickSpacing() const { return m_tickSpacing; }
    void setTickSpacing(double px);
    QString unit() const { return m_unit; }
    void setUnit(const QString &u);
    QString title() const { return m_title; }
    void setTitle(const QString &t);
    bool unitInLabels() const { return m_unitInLabels; }
    void setUnitInLabels(bool on);
    bool labelsVisible() const { return m_labelsVisible; }
    void setLabelsVisible(bool on);
    bool gridVisible() const { return m_gridVisible; }
    void setGridVisible(bool on);
    bool minorGridVisible() const { return m_minorGridVisible; }
    void setMinorGridVisible(bool on);
    bool inverted() const { return m_inverted; }
    void setInverted(bool on);
    LogLabels logLabels() const { return m_logLabels; }
    void setLogLabels(LogLabels labels);
    LabelFormat labelFormat() const { return m_labelFormat; }
    void setLabelFormat(LabelFormat f);
    int decimals() const { return m_decimals; }
    void setDecimals(int d);
    // Zoom/pan by mouse, wheel, pinch and rubber band.
    bool interactive() const { return m_interactive; }
    void setInteractive(bool on);
    // Interactive zoom/pan stays inside [lowerLimit, upperLimit] (NaN: unlimited).
    double lowerLimit() const { return m_lowerLimit; }
    void setLowerLimit(double v);
    double upperLimit() const { return m_upperLimit; }
    void setUpperLimit(double v);
    // Smallest visible span (linear axes): applies to zoom and to autoscale()/autoScale, which
    // widen around the data centre (e.g. 1 mV for a DMM trend of a near-constant voltage).
    double minimumSpan() const { return m_minimumSpan; }
    void setMinimumSpan(double v);
    double maximumSpan() const { return m_maximumSpan; }
    void setMaximumSpan(double v);
    bool limitToData() const { return m_limitToData; }
    void setLimitToData(bool on);
    double limitMargin() const { return m_limitMargin; }
    void setLimitMargin(double f);
    // Data extent for limitToData (set by the Plot on every update).
    void setDataExtent(double lo, double hi);
    // Follow the data of the traces on this axis (range = data range + margin), every update.
    bool autoScale() const { return m_autoScale; }
    void setAutoScale(bool on);
    double autoScaleMargin() const { return m_autoScaleMargin; }
    void setAutoScaleMargin(double fraction);
    // Follow the latest data (x axis of recordings/trends): the visible span slides so the last
    // sample stays at the right edge. Zooming keeps following; panning by the user turns it off.
    bool follow() const { return m_follow; }
    void setFollow(bool on);
    bool zoomed() const { return m_zoomed; }
    double span() const { return m_max - m_min; }

    bool isLog() const { return m_scale == Log; }

    // Sets min and max (and the home range) at once.
    Q_INVOKABLE void setRange(double min, double max);
    // Visible range only (interaction); clamped to the limits; home kept.
    Q_INVOKABLE void setViewRange(double min, double max);
    // Back to the home range.
    Q_INVOKABLE void reset();
    // Zoom by `factor` (> 1 zooms in) keeping `anchor` (data value) at the same place.
    Q_INVOKABLE void zoom(double factor, double anchor);
    // Pan by a fraction of the visible span (0.1 = 10 % towards larger values).
    Q_INVOKABLE void pan(double fraction);
    // Readout text for a value on this axis: "1.235 ms", "−20.00 dB", "12:34:56".
    Q_INVOKABLE QString formatValue(double value, int digits = 4) const;
    // Normalized position (0..1, log aware, inversion applied) of a value, and back.
    Q_INVOKABLE double normalized(double value) const;
    Q_INVOKABLE double fromNormalized(double t) const;

    // Map for a pixel span: `start` is the pixel of the low end (left or bottom), `length` is
    // signed (negative for y axes growing upwards).
    ScaleMap map(double start, double length) const;
    // Ticks for an axis of `pixelLength` pixels.
    TickSet ticks(double pixelLength) const;
    QString tickLabel(double value, const TickSet &ticks) const;
    // Title with unit, e.g. "Frequency (Hz)" (unit omitted when shown in the labels).
    QString decoratedTitle() const;

signals:
    void rangeChanged();
    void scaleChanged();
    void appearanceChanged();
    void interactiveChanged();
    void limitsChanged();
    void autoScaleChanged();
    void followChanged();
    // Emitted when the user zoomed or panned (not for programmatic changes).
    void viewChangedByUser();

private:
    friend class Plot;
    friend struct PlotPrivate;
    void applyRange(double min, double max, bool home);
    void clampToLimits(double &min, double &max) const;

    double m_min = 0.0;
    double m_max = 1.0;
    double m_homeMin = 0.0;
    double m_homeMax = 1.0;
    Scale m_scale = Linear;
    int m_divisions = 0;
    int m_minorTicks = -1;
    double m_tickSpacing = 90.0;
    QString m_unit;
    QString m_title;
    bool m_unitInLabels = false;
    bool m_labelsVisible = true;
    bool m_gridVisible = true;
    bool m_minorGridVisible = true;
    bool m_inverted = false;
    LogLabels m_logLabels = AutoLogLabels;
    LabelFormat m_labelFormat = Engineering;
    int m_decimals = -1;
    bool m_interactive = true;
    double m_lowerLimit = qQNaN();
    double m_upperLimit = qQNaN();
    double m_minimumSpan = 0.0;
    double m_maximumSpan = qQNaN();
    bool m_limitToData = false;
    double m_limitMargin = 0.25;
    double m_dataLo = qQNaN();
    double m_dataHi = qQNaN();
    bool m_autoScale = false;
    double m_autoScaleMargin = 0.08;
    bool m_follow = false;
    bool m_zoomed = false;
};

}  // namespace mybench::plot
