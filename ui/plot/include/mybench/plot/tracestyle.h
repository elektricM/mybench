// TraceStyle: the user-editable look of one trace (ELVISmx "Graph Properties > Plots" / plot legend
// menu equivalent, app.md 8.5): line color and width, line style, point style, interpolation and
// fill, with JSON persistence for the controllers' saved settings.
//
//     TraceStyle { id: ch0Style }                                   // declared values = defaults
//     PlotTrace { buffer: c.ch0; color: Theme.ch0; userStyle: ch0Style }
//     // persistence (controller extra settings): style.toJson() / style.fromJson(map)
//
// Attached to a PlotTrace (`PlotTrace.userStyle`), a style overrides the trace's own look:
// `lineColor`/`fillColor` unset (invalid) keep the trace's color, everything else applies as set.
// Values set in QML at creation (or with setAsDefault()) are the defaults that reset() restores
// and isDefault compares against.
#pragma once

#include <QColor>
#include <QObject>
#include <QQmlParserStatus>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace mybench::plot {

class TraceStyle : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    // Invalid (unset, `undefined` in QML) = the trace's own color.
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor RESET resetLineColor NOTIFY changed FINAL)
    // Point and fill color ("Point/Fill" in ELVISmx); invalid = the line color.
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor RESET resetFillColor NOTIFY changed FINAL)
    // px, 0 = no line (points only); clamped to [0, 10].
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY changed FINAL)
    Q_PROPERTY(LineStyle lineStyle READ lineStyle WRITE setLineStyle NOTIFY changed FINAL)
    Q_PROPERTY(PointStyle pointStyle READ pointStyle WRITE setPointStyle NOTIFY changed FINAL)
    Q_PROPERTY(Interpolation interpolation READ interpolation WRITE setInterpolation NOTIFY changed FINAL)
    Q_PROPERTY(FillTo fillTo READ fillTo WRITE setFillTo NOTIFY changed FINAL)
    Q_PROPERTY(bool isDefault READ isDefault NOTIFY changed FINAL)

public:
    // Same order as PlotElement::LineStyle.
    enum LineStyle { Solid, Dash, Dot, DashDot, DashDotDot };
    Q_ENUM(LineStyle)
    // Cross: bold "+" (✚); Plus: thin "+"; X: thin "×".
    enum PointStyle { NoPoints, Circle, FilledCircle, Square, FilledSquare, Diamond, FilledDiamond, Cross, Plus, X };
    Q_ENUM(PointStyle)
    // ELVISmx interpolation palette. Step: hold each value until the next sample (horizontal then
    // vertical); StepVertical: vertical then horizontal; CenterStep: the jump halfway between
    // samples; CenterStepVertical: horizontal at the mean of neighbours, vertical at the samples.
    enum Interpolation { PointsOnly, Linear, Step, StepVertical, CenterStep, CenterStepVertical };
    Q_ENUM(Interpolation)
    // Fill between the trace and zero, the bottom (-∞) or the top (+∞) of the plot, 50 % alpha.
    enum FillTo { NoFill, Zero, MinusInfinity, PlusInfinity };
    Q_ENUM(FillTo)

    explicit TraceStyle(QObject *parent = nullptr);

    QColor lineColor() const { return m_value.lineColor; }
    void setLineColor(const QColor &c);
    void resetLineColor() { setLineColor(QColor()); }
    QColor fillColor() const { return m_value.fillColor; }
    void setFillColor(const QColor &c);
    void resetFillColor() { setFillColor(QColor()); }
    double lineWidth() const { return m_value.lineWidth; }
    void setLineWidth(double w);
    LineStyle lineStyle() const { return m_value.lineStyle; }
    void setLineStyle(LineStyle s);
    PointStyle pointStyle() const { return m_value.pointStyle; }
    void setPointStyle(PointStyle s);
    Interpolation interpolation() const { return m_value.interpolation; }
    void setInterpolation(Interpolation i);
    FillTo fillTo() const { return m_value.fillTo; }
    void setFillTo(FillTo f);
    bool isDefault() const { return m_value == m_default; }

    // Back to the defaults.
    Q_INVOKABLE void reset();
    // The current values become the defaults (called automatically after QML creation).
    Q_INVOKABLE void setAsDefault();
    // Copies the values (not the defaults) of `other`.
    Q_INVOKABLE void copyFrom(mybench::plot::TraceStyle *other);
    // {lineColor: "#rrggbb" | "#aarrggbb" | "", fillColor, lineWidth, lineStyle: "solid" | "dash" |
    // "dot" | "dashDot" | "dashDotDot", pointStyle: "none" | "circle" | "filledCircle" | "square" |
    // "filledSquare" | "diamond" | "filledDiamond" | "cross" | "plus" | "x", interpolation:
    // "pointsOnly" | "linear" | "step" | "stepVertical" | "centerStep" | "centerStepVertical",
    // fillTo: "none" | "zero" | "minusInfinity" | "plusInfinity"}.
    Q_INVOKABLE QVariantMap toJson() const;
    // Missing keys and unknown values take the default, unknown keys are ignored, widths clamped.
    Q_INVOKABLE void fromJson(const QVariantMap &json);

    void classBegin() override {}
    void componentComplete() override { setAsDefault(); }

signals:
    void changed();

private:
    struct Values {
        QColor lineColor;
        QColor fillColor;
        double lineWidth = 1.5;
        LineStyle lineStyle = Solid;
        PointStyle pointStyle = NoPoints;
        Interpolation interpolation = Linear;
        FillTo fillTo = NoFill;
        bool operator==(const Values &) const = default;
    };
    void assign(const Values &v);
    Values m_value;
    Values m_default;
};

}  // namespace mybench::plot
