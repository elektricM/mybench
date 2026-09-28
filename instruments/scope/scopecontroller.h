// ScopeController: the Oscilloscope (docs/instruments/scope.md; ELVISmx behaviour in
// docs/instruments/algorithms/scope.md).
//
// Two channels (AI 0 / AI 1 or AudioInput Left / Right), ELVISmx timebase and record policy,
// software edge trigger with the Timeout LED, RMS / Freq / Vp-p (+ extra) measurements, cursors,
// ELVISmx autoscale, and the MYBENCH extensions: math trace (CH0 ± CH1, ×, −, FFT), XY mode, roll
// mode, persistence, reference traces, maximum-rate sampling, trigger hysteresis and auto
// trigger. The acquisition runs in the worker thread (scopecore.h: Acquirer, Roller); traces
// reach MyBench.Plot through TraceBuffers.
#pragma once

#include "scopecore.h"
#include "scopeobjects.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>

#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

namespace mybench::instruments {

class ScopeController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings groups (persisted through saveExtraSettings) --------------------------------
    Q_PROPERTY(mybench::instruments::ScopeChannel* channel0 READ channel0 CONSTANT)
    Q_PROPERTY(mybench::instruments::ScopeChannel* channel1 READ channel1 CONSTANT)
    Q_PROPERTY(mybench::instruments::ScopeMath* math READ math CONSTANT)
    Q_PROPERTY(mybench::instruments::ScopeTraceStyle* refAStyle READ refAStyle CONSTANT)   // Graph Properties > Plots: REF A / REF B
    Q_PROPERTY(mybench::instruments::ScopeTraceStyle* refBStyle READ refBStyle CONSTANT)
    Q_PROPERTY(mybench::instruments::ScopeCursor* cursor1 READ cursor1 CONSTANT)
    Q_PROPERTY(mybench::instruments::ScopeCursor* cursor2 READ cursor2 CONSTANT)

    // --- Settings (persisted) -----------------------------------------------------------------
    // Timebase
    Q_PROPERTY(double timePerDiv READ timePerDiv WRITE setTimePerDiv NOTIFY timePerDivChanged)
    Q_PROPERTY(SamplingMode samplingMode READ samplingMode WRITE setSamplingMode NOTIFY samplingModeChanged)
    Q_PROPERTY(bool rollMode READ rollMode WRITE setRollMode NOTIFY rollModeChanged)
    // Trigger
    Q_PROPERTY(TriggerType triggerType READ triggerType WRITE setTriggerType NOTIFY triggerTypeChanged)
    Q_PROPERTY(TriggerSource triggerSource READ triggerSource WRITE setTriggerSource NOTIFY triggerSourceChanged)
    Q_PROPERTY(Slope triggerSlope READ triggerSlope WRITE setTriggerSlope NOTIFY triggerSlopeChanged)
    Q_PROPERTY(double triggerLevel READ triggerLevel WRITE setTriggerLevel NOTIFY triggerLevelChanged)
    Q_PROPERTY(double horizontalPosition READ horizontalPosition WRITE setHorizontalPosition NOTIFY horizontalPositionChanged)
    Q_PROPERTY(double triggerHysteresis READ triggerHysteresis WRITE setTriggerHysteresis NOTIFY triggerHysteresisChanged)
    Q_PROPERTY(NoTriggerMode noTriggerMode READ noTriggerMode WRITE setNoTriggerMode NOTIFY noTriggerModeChanged)
    // Measurements / autoscale options (MYBENCH, Advanced)
    Q_PROPERTY(FrequencyMethod frequencyMethod READ frequencyMethod WRITE setFrequencyMethod NOTIFY frequencyMethodChanged)
    Q_PROPERTY(bool autoscaleSetsLevel READ autoscaleSetsLevel WRITE setAutoscaleSetsLevel NOTIFY autoscaleSetsLevelChanged)
    // Cursors
    Q_PROPERTY(bool cursorsOn READ cursorsOn WRITE setCursorsOn NOTIFY cursorsOnChanged)
    Q_PROPERTY(bool showDeltaTime READ showDeltaTime WRITE setShowDeltaTime NOTIFY showDeltaTimeChanged)
    // Display
    Q_PROPERTY(DisplayMode displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(int persistence READ persistence WRITE setPersistence NOTIFY persistenceChanged)   // ms, 0 = off
    Q_PROPERTY(Background background READ background WRITE setBackground NOTIFY backgroundChanged)

    // --- Value lists and derived values -------------------------------------------------------
    Q_PROPERTY(QList<double> timeDivValues READ timeDivValues CONSTANT)   // ascending
    Q_PROPERTY(QStringList timeDivLabels READ timeDivLabels CONSTANT)     // ascending
    Q_PROPERTY(int timeDivIndex READ timeDivIndex WRITE setTimeDivIndex NOTIFY timePerDivChanged STORED false)
    Q_PROPERTY(QString timeDivLabel READ timeDivLabel NOTIFY timePerDivChanged)
    Q_PROPERTY(double screenTime READ screenTime NOTIFY timePerDivChanged)          // 10 × Time/Div
    Q_PROPERTY(double plannedRate READ plannedRate NOTIFY planChanged)              // S/s for these settings
    Q_PROPERTY(int plannedRecordLength READ plannedRecordLength NOTIFY planChanged) // N
    Q_PROPERTY(bool rollAvailable READ rollAvailable NOTIFY planChanged)
    Q_PROPERTY(bool rolling READ rolling NOTIFY planChanged)                        // roll mode in effect
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY planChanged)
    // Every channel measurement: [{ flag, label, name, extension }] (ScopeChannel::Measurement).
    Q_PROPERTY(QVariantList measurementCatalog READ measurementCatalog CONSTANT)

    // --- Results ------------------------------------------------------------------------------
    Q_PROPERTY(bool waitingForTrigger READ waitingForTrigger NOTIFY waitingForTriggerChanged)
    Q_PROPERTY(bool hasRecord READ hasRecord NOTIFY recordChanged)
    Q_PROPERTY(double recordRate READ recordRate NOTIFY recordChanged)             // rate of the displayed record
    Q_PROPERTY(int recordLength READ recordLength NOTIFY recordChanged)
    Q_PROPERTY(double triggerTime READ triggerTime NOTIFY recordChanged)           // s, NaN: untriggered
    Q_PROPERTY(bool autoscaling READ autoscaling NOTIFY autoscalingChanged)
    Q_PROPERTY(double fftBinWidth READ fftBinWidth NOTIFY recordChanged)           // Hz
    Q_PROPERTY(double fftMaxFrequency READ fftMaxFrequency NOTIFY recordChanged)   // Hz (Nyquist)

    // --- Traces (x in s, y in V; XY: x = CH 0 V, y = CH 1 V; FFT: x in Hz, y in dBV) ----------
    Q_PROPERTY(mybench::plot::TraceBuffer* ch0 READ ch0 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* ch1 READ ch1 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* mathTrace READ mathTrace CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* fftTrace READ fftTrace CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* xyTrace READ xyTrace CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* refA READ refA CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* refB READ refB CONSTANT)

    // --- Reference traces (view state, not persisted) ------------------------------------------
    Q_PROPERTY(bool refAAvailable READ refAAvailable NOTIFY referencesChanged)
    Q_PROPERTY(bool refBAvailable READ refBAvailable NOTIFY referencesChanged)
    Q_PROPERTY(bool refAVisible READ refAVisible WRITE setRefAVisible NOTIFY referencesChanged STORED false)
    Q_PROPERTY(bool refBVisible READ refBVisible WRITE setRefBVisible NOTIFY referencesChanged STORED false)
    Q_PROPERTY(QString refALabel READ refALabel NOTIFY referencesChanged)
    Q_PROPERTY(QString refBLabel READ refBLabel NOTIFY referencesChanged)
    Q_PROPERTY(double refAScale READ refAScale NOTIFY referencesChanged)       // V/div when saved
    Q_PROPERTY(double refBScale READ refBScale NOTIFY referencesChanged)
    Q_PROPERTY(double refAPosition READ refAPosition NOTIFY referencesChanged) // div when saved
    Q_PROPERTY(double refBPosition READ refBPosition NOTIFY referencesChanged)

public:
    enum TriggerType { Immediate, Edge };
    Q_ENUM(TriggerType)
    enum TriggerSource { Chan0Source, Chan1Source };
    Q_ENUM(TriggerSource)
    enum Slope { Rising, Falling };
    Q_ENUM(Slope)
    enum SamplingMode { Standard, HighResolution };
    Q_ENUM(SamplingMode)
    // What an Edge acquisition without crossing shows: ELVISmx clears the graph.
    enum NoTriggerMode { ClearGraph, KeepLastTrace, ShowUntriggered };
    Q_ENUM(NoTriggerMode)
    enum FrequencyMethod { ToneFrequency, EdgeFrequency };
    Q_ENUM(FrequencyMethod)
    enum DisplayMode { YT, XY };
    Q_ENUM(DisplayMode)
    enum Background { ThemeBackground, Black, White };
    Q_ENUM(Background)

    ScopeController();
    ~ScopeController() override;

    ScopeChannel* channel0() { return &m_channel0; }
    ScopeChannel* channel1() { return &m_channel1; }
    ScopeChannel* channel(int index) { return index == 0 ? &m_channel0 : &m_channel1; }
    const ScopeChannel* channel(int index) const { return index == 0 ? &m_channel0 : &m_channel1; }
    ScopeMath* math() { return &m_math; }
    ScopeTraceStyle* refAStyle() { return &m_refAStyle; }
    ScopeTraceStyle* refBStyle() { return &m_refBStyle; }
    ScopeCursor* cursor1() { return &m_cursor1; }
    ScopeCursor* cursor2() { return &m_cursor2; }

    double timePerDiv() const { return m_timePerDiv; }
    void setTimePerDiv(double seconds);
    SamplingMode samplingMode() const { return m_samplingMode; }
    void setSamplingMode(SamplingMode mode);
    bool rollMode() const { return m_rollMode; }
    void setRollMode(bool on);
    TriggerType triggerType() const { return m_triggerType; }
    void setTriggerType(TriggerType type);
    TriggerSource triggerSource() const { return m_triggerSource; }
    void setTriggerSource(TriggerSource source);
    Slope triggerSlope() const { return m_triggerSlope; }
    void setTriggerSlope(Slope slope);
    double triggerLevel() const { return m_triggerLevel; }
    void setTriggerLevel(double volts);
    double horizontalPosition() const { return m_horizontalPosition; }
    void setHorizontalPosition(double percent);
    double triggerHysteresis() const { return m_triggerHysteresis; }
    void setTriggerHysteresis(double divisions);
    NoTriggerMode noTriggerMode() const { return m_noTriggerMode; }
    void setNoTriggerMode(NoTriggerMode mode);
    FrequencyMethod frequencyMethod() const { return m_frequencyMethod; }
    void setFrequencyMethod(FrequencyMethod method);
    bool autoscaleSetsLevel() const { return m_autoscaleSetsLevel; }
    void setAutoscaleSetsLevel(bool on);
    bool cursorsOn() const { return m_cursorsOn; }
    void setCursorsOn(bool on);
    bool showDeltaTime() const { return m_showDeltaTime; }
    void setShowDeltaTime(bool on);
    DisplayMode displayMode() const { return m_displayMode; }
    void setDisplayMode(DisplayMode mode);
    int persistence() const { return m_persistence; }
    void setPersistence(int ms);
    Background background() const { return m_background; }
    void setBackground(Background background);

    QList<double> timeDivValues() const;
    QStringList timeDivLabels() const;
    int timeDivIndex() const;
    void setTimeDivIndex(int index);
    QString timeDivLabel() const;
    double screenTime() const { return 10.0 * m_timePerDiv; }
    double plannedRate() const;
    int plannedRecordLength() const;
    bool rollAvailable() const;
    bool rolling() const { return m_rollMode && rollAvailable(); }
    QString validationMessage() const { return scope::validate(snapshot()); }
    QVariantList measurementCatalog() const { return ScopeChannel::measurementCatalog(); }

    bool waitingForTrigger() const { return m_waitingForTrigger; }
    bool hasRecord() const { return m_record.enabled(0) || m_record.enabled(1); }
    double recordRate() const { return m_record.sampleRate; }
    int recordLength() const;
    double triggerTime() const;
    bool autoscaling() const { return m_autoscaling; }
    double fftBinWidth() const { return m_fftBinWidth; }
    double fftMaxFrequency() const { return m_record.sampleRate / 2.0; }

    plot::TraceBuffer* ch0() { return &m_ch0Trace; }
    plot::TraceBuffer* ch1() { return &m_ch1Trace; }
    plot::TraceBuffer* mathTrace() { return &m_mathTrace; }
    plot::TraceBuffer* fftTrace() { return &m_fftTrace; }
    plot::TraceBuffer* xyTrace() { return &m_xyTrace; }
    plot::TraceBuffer* refA() { return &m_ref[0].buffer; }
    plot::TraceBuffer* refB() { return &m_ref[1].buffer; }
    bool refAAvailable() const { return m_ref[0].available; }
    bool refBAvailable() const { return m_ref[1].available; }
    bool refAVisible() const { return m_ref[0].visible; }
    void setRefAVisible(bool on);
    bool refBVisible() const { return m_ref[1].visible; }
    void setRefBVisible(bool on);
    QString refALabel() const { return m_ref[0].label; }
    QString refBLabel() const { return m_ref[1].label; }
    double refAScale() const { return m_ref[0].scale; }
    double refBScale() const { return m_ref[1].scale; }
    double refAPosition() const { return m_ref[0].position; }
    double refBPosition() const { return m_ref[1].position; }

    // The settings the worker uses.
    scope::Snapshot snapshot() const;

    // --- Actions from the UI ------------------------------------------------------------------
    // Source with the ELVISmx rule: AI and audio inputs cannot be mixed, so choosing an audio
    // (AI) source switches the other channel to an audio (AI) source too (toast). Enabled:
    // disabling the last enabled channel while running is refused (returns false).
    Q_INVOKABLE bool setChannelSource(int channel, int source);
    Q_INVOKABLE bool setChannelEnabled(int channel, bool enabled);
    // Autoscale (works idle or running; idle: probe, then one acquisition).
    Q_INVOKABLE void autoscale();
    // ELVISmx measurement text ("707.11 mV", "?").
    Q_INVOKABLE QString formatMeasurement(double value, const QString& unit) const;
    // ELVISmx Sample Rate text ("50.00 kS/s").
    Q_INVOKABLE QString formatRate(double samplesPerSecond) const;
    // ELVISmx "Log" TXT layout (tab separated waveform export with absolute timestamps).
    Q_INVOKABLE bool exportElvisText(const QUrl& file);
    // Reference traces: slot 0 = REF A, 1 = REF B; source 0 = CH 0, 1 = CH 1, 2 = math.
    Q_INVOKABLE bool saveReference(int slot, int source);
    Q_INVOKABLE void clearReference(int slot);
    Q_INVOKABLE bool exportReference(int slot, const QUrl& file);
    Q_INVOKABLE bool importReference(int slot, const QUrl& file);
    // Cursor readout of a channel (0, 1, 2 = math) at time t (s): nearest sample, NaN if none.
    Q_INVOKABLE double valueAt(int channel, double seconds) const;
    // Places the cursors at 25 % / 75 % of the screen.
    Q_INVOKABLE void resetCursors();
    // Mouse wheel over the graph: `steps` entries along the Time/Div list (positive = larger
    // Time/Div), along a channel's Volts/Div list, and the enabled channel whose zero line is
    // nearest to `divisions` (vertical position on the graph, -5...5); -1 when none is enabled.
    Q_INVOKABLE void stepTimeDiv(int steps);
    Q_INVOKABLE void stepChannelScale(int channel, int steps);
    Q_INVOKABLE int channelNearestTo(double divisions) const;
    // Graph Properties "Reset to Default": every trace style (CH 0/1, math, references), both
    // cursors, the background and the displayed measurements.
    Q_INVOKABLE void resetGraphProperties();

    // Tests.
    const scope::Record& lastRecord() const { return m_record; }

signals:
    void timePerDivChanged();
    void samplingModeChanged();
    void rollModeChanged();
    void triggerTypeChanged();
    void triggerSourceChanged();
    void triggerSlopeChanged();
    void triggerLevelChanged();
    void horizontalPositionChanged();
    void triggerHysteresisChanged();
    void noTriggerModeChanged();
    void frequencyMethodChanged();
    void autoscaleSetsLevelChanged();
    void cursorsOnChanged();
    void showDeltaTimeChanged();
    void displayModeChanged();
    void persistenceChanged();
    void backgroundChanged();
    void planChanged();
    void waitingForTriggerChanged();
    void recordChanged();
    void autoscalingChanged();
    void referencesChanged();

protected:
    WorkerFunction createWorker() override;
    void onFinished() override;
    DataTable exportTable() const override;
    void saveExtraSettings(QJsonObject& settings) const override;
    void restoreExtraSettings(const QJsonObject& settings) override;

private:
    struct Reference {
        plot::TraceBuffer buffer;
        bool available = false;
        bool visible = true;
        QString label;
        double scale = 1.0;
        double position = 0.0;
        std::vector<double> data;
        double dt = 0;
    };
    struct Published;

    void watch(QObject* object);
    double maxRate() const;
    Q_SLOT void onSettingChanged();
    void applyRecord(const std::shared_ptr<const Published>& published);
    void applyAutoscale(const scope::AutoscaleResult& result);
    void setWaitingForTrigger(bool waiting);
    void setAutoscaling(bool on);
    void fixTriggerSource();
    void clampCursors();

    ScopeChannel m_channel0{0, ScopeChannel::AI0, true};
    ScopeChannel m_channel1{1, ScopeChannel::AI1, false};
    ScopeMath m_math;
    ScopeTraceStyle m_refAStyle;
    ScopeTraceStyle m_refBStyle;
    ScopeCursor m_cursor1{QStringLiteral("C1"), 0};
    ScopeCursor m_cursor2{QStringLiteral("C2"), 1};

    double m_timePerDiv = 5e-3;
    SamplingMode m_samplingMode = Standard;
    bool m_rollMode = false;
    TriggerType m_triggerType = Immediate;
    TriggerSource m_triggerSource = Chan0Source;
    Slope m_triggerSlope = Rising;
    double m_triggerLevel = 0.0;
    double m_horizontalPosition = 50.0;
    double m_triggerHysteresis = 0.0;
    NoTriggerMode m_noTriggerMode = ClearGraph;
    FrequencyMethod m_frequencyMethod = ToneFrequency;
    bool m_autoscaleSetsLevel = false;
    // Last AI / audio source chosen per channel (the other channel follows to the same kind).
    int m_lastAiSource[2] = {ScopeChannel::AI0, ScopeChannel::AI1};
    int m_lastAudioSource[2] = {-1, -1};
    bool m_cursorsOn = false;
    bool m_showDeltaTime = true;
    DisplayMode m_displayMode = YT;
    int m_persistence = 0;
    Background m_background = ThemeBackground;

    bool m_waitingForTrigger = false;
    bool m_autoscaling = false;
    bool m_autoscaleOnStart = false;
    double m_fftBinWidth = 0;
    scope::Record m_record;
    double m_recordX0 = 0;
    QDateTime m_recordTime;  // wall-clock time of the first sample of the displayed record   // x of sample 0 of the displayed record (roll mode: < 0 while filling)
    std::vector<double> m_mathRecord;
    Reference m_ref[2];

    std::shared_ptr<Mailbox<scope::Snapshot>> m_live = std::make_shared<Mailbox<scope::Snapshot>>();
    std::shared_ptr<std::atomic<bool>> m_autoscaleRequest = std::make_shared<std::atomic<bool>>(false);

    plot::TraceBuffer m_ch0Trace;
    plot::TraceBuffer m_ch1Trace;
    plot::TraceBuffer m_mathTrace;
    plot::TraceBuffer m_fftTrace;
    plot::TraceBuffer m_xyTrace;
};

void registerScope(InstrumentRegistry& registry);

}  // namespace mybench::instruments
