// BodeController: the Bode Analyzer (docs/instruments/bode.md, ELVISmx behaviour matched).
//
// Run = one sweep: a sine on AO 0 steps logarithmically from Start to Stop; AI 0 measures the
// stimulus, AI 1 the circuit's response (BodeSweeper, worker thread). Each measured point keeps
// its complex response, so polarity, gain mapping (dB / ratio) and phase unwrapping are display
// transforms applied at any time. Enhancements: reference curves (overlay/compare), -3 dB,
// unity-gain and gain/phase-margin markers, averages per point, re-measure at the cursor, a fast
// sweep mode without the ELVISmx range pre-measurement, and a linear frequency axis option.
#pragma once

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>

#include <QElapsedTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace mybench::instruments {

namespace bode {
struct MeasuredPoint;
}

class BodeController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings (persisted; declaration order = restore order) -------------------------------
    // Measurement Settings
    Q_PROPERTY(double startFrequency READ startFrequency WRITE setStartFrequency NOTIFY startFrequencyChanged)
    Q_PROPERTY(double stopFrequency READ stopFrequency WRITE setStopFrequency NOTIFY stopFrequencyChanged)
    Q_PROPERTY(int stepsPerDecade READ stepsPerDecade WRITE setStepsPerDecade NOTIFY stepsPerDecadeChanged)
    Q_PROPERTY(double peakAmplitude READ peakAmplitude WRITE setPeakAmplitude NOTIFY peakAmplitudeChanged)
    Q_PROPERTY(Polarity polarity READ polarity WRITE setPolarity NOTIFY polarityChanged)
    // Graph Settings
    Q_PROPERTY(Mapping mapping READ mapping WRITE setMapping NOTIFY mappingChanged)
    Q_PROPERTY(bool autoscale READ autoscale WRITE setAutoscale NOTIFY autoscaleChanged)
    // Cursor Settings
    Q_PROPERTY(bool cursorsOn READ cursorsOn WRITE setCursorsOn NOTIFY cursorsOnChanged)
    // MYBENCH extensions
    Q_PROPERTY(SweepMode sweepMode READ sweepMode WRITE setSweepMode NOTIFY sweepModeChanged)
    Q_PROPERTY(int averagesPerPoint READ averagesPerPoint WRITE setAveragesPerPoint NOTIFY averagesPerPointChanged)
    Q_PROPERTY(bool logFrequencyAxis READ logFrequencyAxis WRITE setLogFrequencyAxis NOTIFY logFrequencyAxisChanged)
    // Autoscale off: the plots jump to these fixed ranges (ELVISmx): gain -30...+5, phase -360...+360 in 90 deg steps.
    Q_PROPERTY(double fixedGainMin READ fixedGainMin CONSTANT)
    Q_PROPERTY(double fixedGainMax READ fixedGainMax CONSTANT)
    Q_PROPERTY(double fixedPhaseMin READ fixedPhaseMin CONSTANT)
    Q_PROPERTY(double fixedPhaseMax READ fixedPhaseMax CONSTANT)
    Q_PROPERTY(double fixedPhaseStep READ fixedPhaseStep CONSTANT)
    Q_PROPERTY(bool elvisColors READ elvisColors WRITE setElvisColors NOTIFY elvisColorsChanged)
    Q_PROPERTY(bool unwrapPhase READ unwrapPhase WRITE setUnwrapPhase NOTIFY unwrapPhaseChanged)
    Q_PROPERTY(bool showCutoffMarkers READ showCutoffMarkers WRITE setShowCutoffMarkers NOTIFY markerOptionsChanged)
    Q_PROPERTY(bool showUnityGainMarker READ showUnityGainMarker WRITE setShowUnityGainMarker NOTIFY markerOptionsChanged)
    Q_PROPERTY(bool showMarginMarkers READ showMarginMarkers WRITE setShowMarginMarkers NOTIFY markerOptionsChanged)

    // --- View state (not persisted) --------------------------------------------------------------
    Q_PROPERTY(int cursorIndex READ cursorIndex WRITE setCursorIndex NOTIFY cursorChanged STORED false)
    // Second cursor (C2): with C1 it measures a difference — Δf, Δgain, Δphase and the slope.
    Q_PROPERTY(int cursor2Index READ cursor2Index WRITE setCursor2Index NOTIFY cursorChanged STORED false)

    // --- Plan of the current settings ------------------------------------------------------------
    Q_PROPERTY(int plannedPoints READ plannedPoints NOTIFY planChanged)
    Q_PROPERTY(double plannedSeconds READ plannedSeconds NOTIFY planChanged)
    Q_PROPERTY(bool settingsValid READ settingsValid NOTIFY planChanged)

    // --- Results ---------------------------------------------------------------------------------
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointsChanged)
    Q_PROPERTY(int totalPoints READ totalPoints NOTIFY pointsChanged)
    Q_PROPERTY(double progress READ progress NOTIFY pointsChanged)
    Q_PROPERTY(double remainingSeconds READ remainingSeconds NOTIFY pointsChanged)
    Q_PROPERTY(bool remeasuring READ remeasuring NOTIFY pointsChanged)
    Q_PROPERTY(int clippedCount READ clippedCount NOTIFY pointsChanged)
    Q_PROPERTY(double dataMinFrequency READ dataMinFrequency NOTIFY pointsChanged)
    Q_PROPERTY(double dataMaxFrequency READ dataMaxFrequency NOTIFY pointsChanged)
    // Readout row: the point under the cursor (Cursors On) or the most recent point.
    Q_PROPERTY(bool readoutValid READ readoutValid NOTIFY readoutChanged)
    Q_PROPERTY(double readoutFrequency READ readoutFrequency NOTIFY readoutChanged)
    Q_PROPERTY(double readoutGain READ readoutGain NOTIFY readoutChanged)
    Q_PROPERTY(double readoutGainDb READ readoutGainDb NOTIFY readoutChanged)
    Q_PROPERTY(double readoutPhase READ readoutPhase NOTIFY readoutChanged)
    Q_PROPERTY(bool readoutClipped READ readoutClipped NOTIFY readoutChanged)
    Q_PROPERTY(double cursorFrequency READ cursorFrequency NOTIFY cursorChanged)
    // Both cursors' values (NaN when the cursor is not on a point) and C2 − C1.
    Q_PROPERTY(double cursorGainDb READ cursorGainDb NOTIFY readoutChanged)
    Q_PROPERTY(double cursorPhase READ cursorPhase NOTIFY readoutChanged)
    Q_PROPERTY(double cursor2Frequency READ cursor2Frequency NOTIFY cursorChanged)
    Q_PROPERTY(double cursor2GainDb READ cursor2GainDb NOTIFY readoutChanged)
    Q_PROPERTY(double cursor2Phase READ cursor2Phase NOTIFY readoutChanged)
    Q_PROPERTY(double cursorSlope READ cursorSlope NOTIFY readoutChanged)   // dB / decade from C1 to C2
    // Markers (NaN when absent). cutoffFrequencies: the -3 dB crossings.
    Q_PROPERTY(double maxGainDb READ maxGainDb NOTIFY markersChanged)
    Q_PROPERTY(QList<double> cutoffFrequencies READ cutoffFrequencies NOTIFY markersChanged)
    Q_PROPERTY(double unityGainFrequency READ unityGainFrequency NOTIFY markersChanged)
    Q_PROPERTY(double phaseMargin READ phaseMargin NOTIFY markersChanged)
    Q_PROPERTY(double phaseCrossoverFrequency READ phaseCrossoverFrequency NOTIFY markersChanged)
    Q_PROPERTY(double gainMargin READ gainMargin NOTIFY markersChanged)

    // Traces (x = frequency in Hz): gain in the Mapping's unit, phase in degrees, clipped points.
    Q_PROPERTY(mybench::plot::TraceBuffer* gain READ gain CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* phase READ phase CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* clippedGain READ clippedGain CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* clippedPhase READ clippedPhase CONSTANT)

    // References: [{ slot, name, colorIndex, visible, points, polarity }] (slots 0..7).
    Q_PROPERTY(QVariantList references READ references NOTIFY referencesChanged)
    Q_PROPERTY(int maxReferences READ maxReferences CONSTANT)

public:
    enum Polarity { Normal, Inverted };
    Q_ENUM(Polarity)
    enum Mapping { Linear, Logarithmic };   // gain as a ratio or in dB (ELVISmx Mapping)
    Q_ENUM(Mapping)
    enum SweepMode { Standard, Fast };       // Standard = ELVISmx (range pre-measurement per point)
    Q_ENUM(SweepMode)

    static constexpr int kMaxReferences = 8;

    BodeController();
    ~BodeController() override;

    double startFrequency() const { return m_start; }
    void setStartFrequency(double hertz);
    double stopFrequency() const { return m_stop; }
    void setStopFrequency(double hertz);
    int stepsPerDecade() const { return m_steps; }
    void setStepsPerDecade(int steps);
    double peakAmplitude() const { return m_amplitude; }
    void setPeakAmplitude(double volts);
    Polarity polarity() const { return m_polarity; }
    void setPolarity(Polarity polarity);
    Mapping mapping() const { return m_mapping; }
    void setMapping(Mapping mapping);
    bool autoscale() const { return m_autoscale; }
    void setAutoscale(bool on);
    bool cursorsOn() const { return m_cursorsOn; }
    void setCursorsOn(bool on);
    SweepMode sweepMode() const { return m_sweepMode; }
    void setSweepMode(SweepMode mode);
    int averagesPerPoint() const { return m_averages; }
    void setAveragesPerPoint(int averages);
    bool logFrequencyAxis() const { return m_logFrequencyAxis; }
    void setLogFrequencyAxis(bool on);
    bool unwrapPhase() const { return m_unwrapPhase; }
    static constexpr double fixedGainMin() { return -30; }
    static constexpr double fixedGainMax() { return 5; }
    static constexpr double fixedPhaseMin() { return -360; }
    static constexpr double fixedPhaseMax() { return 360; }
    static constexpr double fixedPhaseStep() { return 90; }
    bool elvisColors() const { return m_elvisColors; }
    void setElvisColors(bool on);
    void setUnwrapPhase(bool on);
    bool showCutoffMarkers() const { return m_showCutoff; }
    void setShowCutoffMarkers(bool on);
    bool showUnityGainMarker() const { return m_showUnityGain; }
    void setShowUnityGainMarker(bool on);
    bool showMarginMarkers() const { return m_showMargins; }
    void setShowMarginMarkers(bool on);

    int cursorIndex() const { return m_cursorIndex; }
    void setCursorIndex(int index);
    int cursor2Index() const { return m_cursor2Index; }
    void setCursor2Index(int index);

    int plannedPoints() const { return m_plannedPoints; }
    double plannedSeconds() const { return m_plannedSeconds; }
    bool settingsValid() const { return m_start <= m_stop; }

    int pointCount() const { return int(m_points.size()); }
    int totalPoints() const { return m_total; }
    double progress() const;
    double remainingSeconds() const { return m_remainingSeconds; }
    bool remeasuring() const { return m_remeasuring; }
    int clippedCount() const;
    double dataMinFrequency() const;
    double dataMaxFrequency() const;

    bool readoutValid() const { return readoutPoint() >= 0; }
    double readoutFrequency() const;
    double readoutGain() const;
    double readoutGainDb() const;
    double readoutPhase() const;
    bool readoutClipped() const;
    double cursorFrequency() const;
    double cursorGainDb() const { return pointGainDb(m_cursorIndex); }
    double cursorPhase() const { return pointPhase(m_cursorIndex); }
    double cursor2Frequency() const;
    double cursor2GainDb() const { return pointGainDb(m_cursor2Index); }
    double cursor2Phase() const { return pointPhase(m_cursor2Index); }
    double cursorSlope() const;

    double maxGainDb() const { return m_maxGainDb; }
    QList<double> cutoffFrequencies() const { return m_cutoff; }
    double unityGainFrequency() const { return m_unityGain; }
    double phaseMargin() const { return m_phaseMargin; }
    double phaseCrossoverFrequency() const { return m_phaseCrossover; }
    double gainMargin() const { return m_gainMargin; }

    mybench::plot::TraceBuffer* gain() { return &m_gainTrace; }
    mybench::plot::TraceBuffer* phase() { return &m_phaseTrace; }
    mybench::plot::TraceBuffer* clippedGain() { return &m_clippedGainTrace; }
    mybench::plot::TraceBuffer* clippedPhase() { return &m_clippedPhaseTrace; }

    QVariantList references() const;
    int maxReferences() const { return kMaxReferences; }

    // Cursor (Position Left / Right; dragging snaps to the nearest measured point).
    Q_INVOKABLE void cursorLeft();
    Q_INVOKABLE void cursorRight();
    Q_INVOKABLE void setCursorFrequency(double hertz);
    Q_INVOKABLE void setCursor2Frequency(double hertz);
    // Measures the cursor's point again (idle, cursor on a point) and replaces it.
    Q_INVOKABLE void remeasureCursorPoint();
    // Clears the measured points (references stay).
    Q_INVOKABLE void clearSweep();

    // References (overlay / compare).
    Q_INVOKABLE mybench::plot::TraceBuffer* referenceGain(int slot);
    Q_INVOKABLE mybench::plot::TraceBuffer* referencePhase(int slot);
    Q_INVOKABLE bool keepAsReference(const QString& name = {});
    Q_INVOKABLE void renameReference(int slot, const QString& name);
    Q_INVOKABLE void setReferenceVisible(int slot, bool visible);
    Q_INVOKABLE void removeReference(int slot);
    Q_INVOKABLE void clearReferences();
    Q_INVOKABLE bool exportReference(int slot, const QUrl& file);

signals:
    void startFrequencyChanged();
    void stopFrequencyChanged();
    void stepsPerDecadeChanged();
    void peakAmplitudeChanged();
    void polarityChanged();
    void mappingChanged();
    void autoscaleChanged();
    void cursorsOnChanged();
    void sweepModeChanged();
    void averagesPerPointChanged();
    void logFrequencyAxisChanged();
    void unwrapPhaseChanged();
    void elvisColorsChanged();
    void markerOptionsChanged();
    void cursorChanged();
    void planChanged();
    void pointsChanged();
    void readoutChanged();
    void markersChanged();
    void referencesChanged();

protected:
    WorkerFunction createWorker() override;
    void onFinished() override;
    DataTable exportTable() const override;
    void saveExtraSettings(QJsonObject& settings) const override;
    void restoreExtraSettings(const QJsonObject& settings) override;

private:
    struct Point {
        double requested = 0;              // requested frequency of the sweep grid (Hz)
        double frequency = 0;              // generated frequency (Hz)
        std::complex<double> response;     // H, non-inverted
        bool clipped = false;
    };
    struct Reference {
        bool used = false;
        QString name;
        int colorIndex = 0;
        bool visible = true;
        Polarity polarity = Normal;
        std::vector<Point> points;
        // Children of the controller (C++ ownership also when QML reads them).
        mybench::plot::TraceBuffer* gain = nullptr;
        mybench::plot::TraceBuffer* phase = nullptr;
    };
    struct SweepKey {
        double start = 0, stop = 0, amplitude = 0;
        int steps = 0, averages = 0;
        SweepMode mode = Standard;
        bool operator==(const SweepKey&) const = default;
    };

    SweepKey sweepKey() const;
    void updatePlan();
    void applyPoint(int index, const bode::MeasuredPoint& measured, double requested);
    void rebuildDisplay();          // traces, markers, readout from m_points
    void rebuildReference(Reference& reference);
    void fillTraces(const std::vector<Point>& points, Polarity polarity, mybench::plot::TraceBuffer* gain,
                    mybench::plot::TraceBuffer* phase, mybench::plot::TraceBuffer* clippedGain,
                    mybench::plot::TraceBuffer* clippedPhase) const;
    int readoutPoint() const;
    int nearestPoint(double hertz) const;     // -1 without points
    double pointGainDb(int index) const;      // NaN outside the points
    double pointPhase(int index) const;       // unwrapped when Unwrap phase is on
    void setStatus(const QString& text) { setStatusText(text); }

    // Settings (member initializers = documented defaults).
    double m_start = 100;
    double m_stop = 10000;
    int m_steps = 5;
    double m_amplitude = 2.0;
    Polarity m_polarity = Normal;
    Mapping m_mapping = Logarithmic;
    bool m_autoscale = true;
    bool m_cursorsOn = false;
    SweepMode m_sweepMode = Standard;
    int m_averages = 1;
    bool m_logFrequencyAxis = true;
    bool m_unwrapPhase = false;
    bool m_elvisColors = false;
    bool m_showCutoff = true;
    bool m_showUnityGain = false;
    bool m_showMargins = false;

    int m_cursorIndex = -1;
    int m_cursor2Index = -1;
    int m_plannedPoints = 0;
    double m_plannedSeconds = 0;

    // Sweep state.
    std::vector<double> m_sweepFrequencies;   // requested grid of the current sweep
    SweepKey m_sweepKey;
    std::vector<Point> m_points;
    int m_total = 0;
    int m_remeasureIndex = -1;
    bool m_remeasuring = false;
    bool m_clipWarned = false;
    int m_firstJob = 0;
    int m_lastIndex = -1;                     // most recently measured point
    std::vector<double> m_displayPhase;       // phase as displayed (unwrapped when enabled)
    QElapsedTimer m_sweepClock;
    double m_remainingSeconds = 0;

    // Markers.
    double m_maxGainDb = 0;
    QList<double> m_cutoff;
    double m_unityGain = 0;
    double m_phaseMargin = 0;
    double m_phaseCrossover = 0;
    double m_gainMargin = 0;

    mybench::plot::TraceBuffer m_gainTrace;
    mybench::plot::TraceBuffer m_phaseTrace;
    mybench::plot::TraceBuffer m_clippedGainTrace;
    mybench::plot::TraceBuffer m_clippedPhaseTrace;
    std::array<Reference, kMaxReferences> m_references;
};

void registerBode(InstrumentRegistry& registry);

}  // namespace mybench::instruments
