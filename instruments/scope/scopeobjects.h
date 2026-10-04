// Settings groups of the Oscilloscope, exposed to QML as sub-objects of ScopeController:
//   ScopeChannel     the Channel 0 / Channel 1 inspector sections + the channel's measurement results
//   ScopeMath        the math trace (MYBENCH extension)
//   ScopeCursor      C1 / C2 (linked trace, position, cursor style)
// Trace and cursor looks (ELVISmx Graph Properties > Plots / Cursors) are shared
// mybench::plot::TraceStyle objects, edited in the shared TraceStyleDialog.
// Every writable, notifying property is a setting: the controller persists these objects with
// saveExtraSettings()/restoreExtraSettings() and watches their notify signals.
#pragma once

#include "scopecore.h"

#include <mybench/plot/tracestyle.h>

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace mybench::instruments {

class ScopeChannel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ScopeController")
    Q_PROPERTY(int index READ index CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)                      // "CH 0"
    // Settings (use ScopeController.setChannelSource/setChannelEnabled from the UI: they check
    // that AI and audio sources are not mixed).
    Q_PROPERTY(Source source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(double voltsPerDiv READ voltsPerDiv WRITE setVoltsPerDiv NOTIFY voltsPerDivChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(InputRange inputRange READ inputRange WRITE setInputRange NOTIFY inputRangeChanged)
    Q_PROPERTY(int measurementMask READ measurementMask WRITE setMeasurementMask NOTIFY measurementMaskChanged)
    Q_PROPERTY(mybench::plot::TraceStyle* style READ style CONSTANT)
    // Derived
    Q_PROPERTY(bool audio READ audio NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QString coupling READ coupling NOTIFY sourceChanged)   // "DC" (AI) / "AC" (audio)
    Q_PROPERTY(QList<double> scaleValues READ scaleValues NOTIFY sourceChanged)   // ascending
    Q_PROPERTY(QStringList scaleLabels READ scaleLabels NOTIFY sourceChanged)      // ascending
    Q_PROPERTY(int scaleIndex READ scaleIndex WRITE setScaleIndex NOTIFY voltsPerDivChanged STORED false)
    Q_PROPERTY(QString scaleLabel READ scaleLabel NOTIFY voltsPerDivChanged)
    Q_PROPERTY(double effectiveRange READ effectiveRange NOTIFY rangeChanged)   // ±V
    // Results of the displayed record
    Q_PROPERTY(bool hasResults READ hasResults NOTIFY resultsChanged)
    Q_PROPERTY(double rms READ rms NOTIFY resultsChanged)
    Q_PROPERTY(double frequency READ frequency NOTIFY resultsChanged)
    Q_PROPERTY(double peakToPeak READ peakToPeak NOTIFY resultsChanged)
    // [{ key, label, text }] of the selected measurements, in display order.
    Q_PROPERTY(QVariantList readouts READ readouts NOTIFY readoutsChanged)

public:
    // Same order as scope::Source / scope::InputRange.
    enum Source { AI0, AI1, AudioLeft, AudioRight };
    Q_ENUM(Source)
    enum InputRange { Auto, Range10V, Range2V };
    Q_ENUM(InputRange)
    // Measurement flags (the ELVISmx three + MYBENCH extras), chosen in the measurement strip.
    enum Measurement {
        Rms = 1 << 0, Frequency = 1 << 1, PeakToPeak = 1 << 2,
        Mean = 1 << 3, Minimum = 1 << 4, Maximum = 1 << 5, Amplitude = 1 << 6,
        Period = 1 << 7, CounterFrequency = 1 << 8, DutyCycle = 1 << 9,
        RiseTime = 1 << 10, FallTime = 1 << 11, PositiveWidth = 1 << 12, NegativeWidth = 1 << 13,
        AcRms = 1 << 14, CrestFactor = 1 << 15, Overshoot = 1 << 16, Preshoot = 1 << 17,
        SlewRate = 1 << 18, Phase = 1 << 19, Peak = 1 << 20,
    };
    Q_ENUM(Measurement)
    static constexpr int kElvisMeasurements = Rms | Frequency | PeakToPeak;
    static constexpr int kMeasurementCount = 21;
    static constexpr int kAllMeasurements = (1 << kMeasurementCount) - 1;

    ScopeChannel(int index, Source source, bool enabled, QObject* parent = nullptr);

    int index() const { return m_index; }
    QString name() const { return QStringLiteral("CH %1").arg(m_index); }
    Source source() const { return m_source; }
    void setSource(Source source);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    double voltsPerDiv() const { return m_voltsPerDiv; }
    void setVoltsPerDiv(double voltsPerDiv);
    double position() const { return m_position; }
    void setPosition(double divisions);
    InputRange inputRange() const { return m_inputRange; }
    void setInputRange(InputRange range);
    int measurementMask() const { return m_measurementMask; }
    void setMeasurementMask(int mask);
    plot::TraceStyle* style() { return &m_style; }
    const plot::TraceStyle* style() const { return &m_style; }

    scope::Source coreSource() const { return scope::Source(int(m_source)); }
    scope::InputRange coreRange() const { return scope::InputRange(int(m_inputRange)); }
    bool audio() const { return scope::isAudio(coreSource()); }
    QString sourceName() const { return scope::sourceName(coreSource()); }
    QString coupling() const { return audio() ? QStringLiteral("AC") : QStringLiteral("DC"); }
    QList<double> scaleValues() const;
    QStringList scaleLabels() const;
    int scaleIndex() const;
    void setScaleIndex(int index);
    QString scaleLabel() const;
    double effectiveRange() const { return scope::inputRangeFor(coreSource(), m_voltsPerDiv, coreRange()); }

    bool hasResults() const { return m_hasResults; }
    double rms() const { return m_results.rms; }
    // Freq readout: the ELVISmx FFT tone, or the counter-style edge frequency (MYBENCH option).
    double frequency() const { return m_frequencyByEdges ? m_results.counterFrequency : m_results.frequency; }
    void setFrequencyByEdges(bool on);
    double peakToPeak() const { return m_results.peakToPeak; }
    const scope::Measurements& results() const { return m_results; }
    void setResults(const scope::Measurements& results);   // UI thread
    void clearResults();
    QVariantList readouts() const;

    Q_INVOKABLE bool shows(int measurement) const { return (m_measurementMask & measurement) != 0; }
    Q_INVOKABLE void setShown(int measurement, bool on);
    // Every measurement: [{ flag, label, name, extension }], in display order.
    Q_INVOKABLE static QVariantList measurementCatalog();
    // Numeric value of one measurement of the last record (NaN without one); duty cycle in %.
    Q_INVOKABLE double measurement(int flag) const;
    static QString measurementLabel(Measurement m);   // short: "RMS", "Freq", "Vp-p"
    static QString measurementName(Measurement m);    // long: "RMS Voltage"
    static double measurementValue(const scope::Measurements& r, Measurement m);
    static QString measurementUnit(Measurement m);

    scope::ChannelSettings settings() const { return {coreSource(), m_enabled, m_voltsPerDiv, coreRange()}; }
    QJsonObject save() const;
    void restore(const QJsonObject& json);

signals:
    void sourceChanged();
    void enabledChanged();
    void voltsPerDivChanged();
    void positionChanged();
    void inputRangeChanged();
    void measurementMaskChanged();
    void rangeChanged();
    void resultsChanged();
    void readoutsChanged();

private:
    int m_index;
    Source m_source;
    bool m_enabled;
    double m_voltsPerDiv = 1.0;
    double m_position = 0.0;
    InputRange m_inputRange = Auto;
    int m_measurementMask = kElvisMeasurements;
    plot::TraceStyle m_style;
    bool m_hasResults = false;
    bool m_frequencyByEdges = false;
    scope::Measurements m_results;
};

class ScopeMath : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ScopeController")
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(Operation operation READ operation WRITE setOperation NOTIFY operationChanged)
    Q_PROPERTY(double voltsPerDiv READ voltsPerDiv WRITE setVoltsPerDiv NOTIFY voltsPerDivChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(mybench::plot::TraceStyle* style READ style CONSTANT)
    Q_PROPERTY(bool fft READ fft NOTIFY operationChanged)
    Q_PROPERTY(QString functionName READ functionName NOTIFY operationChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY operationChanged)          // "V", "V²", "dBV"
    Q_PROPERTY(QList<double> scaleValues READ scaleValues CONSTANT)     // ascending
    Q_PROPERTY(QStringList scaleLabels READ scaleLabels NOTIFY operationChanged)
    Q_PROPERTY(int scaleIndex READ scaleIndex WRITE setScaleIndex NOTIFY voltsPerDivChanged STORED false)
    Q_PROPERTY(double rms READ rms NOTIFY resultsChanged)
    Q_PROPERTY(double peakToPeak READ peakToPeak NOTIFY resultsChanged)
    Q_PROPERTY(double peakFrequency READ peakFrequency NOTIFY resultsChanged)   // FFT: strongest bin
    Q_PROPERTY(double peakLevel READ peakLevel NOTIFY resultsChanged)           // FFT: dBV

public:
    // Same order as scope::MathFunction. FftA / FftB only exist so that sessions of older versions
    // (math = FFT of CH n) can be migrated to the FFT section (ScopeController::fftSource):
    // setOperation() refuses them.
    enum Operation { Sum, Difference, Product, InvertA, InvertB, FftA, FftB };
    Q_ENUM(Operation)

    explicit ScopeMath(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    Operation operation() const { return m_operation; }
    void setOperation(Operation operation);
    double voltsPerDiv() const { return m_voltsPerDiv; }
    void setVoltsPerDiv(double v);
    double position() const { return m_position; }
    void setPosition(double divisions);
    plot::TraceStyle* style() { return &m_style; }
    bool fft() const { return scope::isFft(coreFunction()); }
    QString functionName() const { return scope::mathName(coreFunction()); }
    QString unit() const;
    QList<double> scaleValues() const;
    QStringList scaleLabels() const;
    int scaleIndex() const;
    void setScaleIndex(int index);
    double rms() const { return m_rms; }
    double peakToPeak() const { return m_peakToPeak; }
    double peakFrequency() const { return m_peakFrequency; }
    double peakLevel() const { return m_peakLevel; }
    void setResults(double rms, double vpp, double peakFrequency, double peakLevel);

    scope::MathFunction coreFunction() const { return scope::MathFunction(int(m_operation)); }
    QJsonObject save() const;
    void restore(const QJsonObject& json);

signals:
    void enabledChanged();
    void operationChanged();
    void voltsPerDivChanged();
    void positionChanged();
    void resultsChanged();

private:
    bool m_enabled = false;
    Operation m_operation = Sum;
    double m_voltsPerDiv = 1.0;
    double m_position = 0.0;
    plot::TraceStyle m_style;
    double m_rms = scope::kNaN;
    double m_peakToPeak = scope::kNaN;
    double m_peakFrequency = scope::kNaN;
    double m_peakLevel = scope::kNaN;
};

class ScopeCursor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ScopeController")
    Q_PROPERTY(QString label READ label CONSTANT)                  // "C1"
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY channelChanged)   // 0, 1 (2 = math)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)   // s
    Q_PROPERTY(CursorStyle cursorStyle READ cursorStyle WRITE setCursorStyle NOTIFY cursorStyleChanged)
    // The cursor's C1/C2 row in the measurement strip ("C1 Volts" of ELVISmx).
    Q_PROPERTY(bool showValue READ showValue WRITE setShowValue NOTIFY showValueChanged)
    // Color, line style and width, marker at the cursor's point on the linked trace (pointStyle;
    // NoPoints = none). Interpolation and fill do not apply to a cursor.
    Q_PROPERTY(mybench::plot::TraceStyle* style READ style CONSTANT)

public:
    // NoCursor hides the cursor graphics (the cursor readouts stay); SmallCrosshair = vertical line
    // with a cross marker at the cursor's point.
    enum CursorStyle { VerticalLine, FullCrosshair, SmallCrosshair, NoCursor };
    Q_ENUM(CursorStyle)

    ScopeCursor(const QString& label, int channel, QObject* parent = nullptr);

    QString label() const { return m_label; }
    int channel() const { return m_channel; }
    void setChannel(int channel);
    double position() const { return m_position; }
    void setPosition(double seconds);
    CursorStyle cursorStyle() const { return m_cursorStyle; }
    void setCursorStyle(CursorStyle style);
    bool showValue() const { return m_showValue; }
    void setShowValue(bool on);
    plot::TraceStyle* style() { return &m_style; }
    const plot::TraceStyle* style() const { return &m_style; }
    // Look and cursor style back to the defaults (dashed 1 px line, theme color, no marker).
    Q_INVOKABLE void resetStyle();

    QJsonObject save() const;
    void restore(const QJsonObject& json);

signals:
    void channelChanged();
    void positionChanged();
    void cursorStyleChanged();
    void showValueChanged();

private:
    QString m_label;
    int m_channel;
    int m_defaultChannel;
    double m_position = scope::kNaN;
    CursorStyle m_cursorStyle = VerticalLine;
    bool m_showValue = true;
    plot::TraceStyle m_style;
};

// The style JSON of TraceStyle::toJson(); also reads the format of older versions (ScopeTraceStyle,
// enum key names such as "Dash", "NoPoints").
QVariantMap traceStyleJson(const QJsonObject& json);
// A TraceStyle with these values as its defaults (reset() returns to them).
void initTraceStyle(plot::TraceStyle& style, plot::TraceStyle::LineStyle lineStyle, double width);

// JSON of every stored, writable, notifying property of `object` (enums by key name).
QJsonObject saveProperties(const QObject* object);
void restoreProperties(QObject* object, const QJsonObject& json);

}  // namespace mybench::instruments
