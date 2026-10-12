// DsaController: the Spectrum Analyzer (docs/instruments/dsa.md; ELVISmx "Dynamic Signal
// Analyzer") with a third-octave analysis mode (the ELVISmx
// featured application "Octave Analyzer", docs/instruments/dsa.md).
//
// FFT mode: one continuous AI task at fs = coerceAiRate(2 × span); contiguous records of
// N = 2 × lines samples, read in chunks of at most 100 ms; windowed spectrum, averaging, max/min
// hold (worker); measurements on the averaged spectrum. The worker publishes the averaged powers
// (V²) at most ~30 times per second; the display conversion (units, RMS/Peak, PSD, scale,
// markers, cursors) happens here on the UI thread, so display settings apply instantly, also to a
// stopped result.
// Octave mode: 50 kS/s continuous, reads of 1000 samples through the third-octave filter bank
// (octaveanalysis.h); band levels in dBV with Z/A/B/C weighting.
//
// Live changes: source, range, span, lines and the analysis mode restart the acquisition; the
// window and the averaging settings restart the averaging (Mailbox); display settings only
// reformat the last result.
#pragma once

#include "dsaanalysis.h"
#include "dsaformat.h"
#include "octaveanalysis.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>
#include <mybench/plot/waterfall.h>

#include <QDateTime>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <optional>

namespace mybench::instruments {

class DsaController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings (persisted; restored in this order) -------------------------------------------
    // Analysis mode (MYBENCH): FFT spectrum (ELVISmx DSA) or third-octave bands (Octave Analyzer).
    Q_PROPERTY(AnalysisMode analysisMode READ analysisMode WRITE setAnalysisMode NOTIFY analysisModeChanged)
    // Input Settings
    Q_PROPERTY(int sourceChannel READ sourceChannel WRITE setSourceChannel NOTIFY sourceChannelChanged)
    Q_PROPERTY(int voltageRangeIndex READ voltageRangeIndex WRITE setVoltageRangeIndex NOTIFY voltageRangeIndexChanged)
    // FFT Settings
    Q_PROPERTY(double frequencySpan READ frequencySpan WRITE setFrequencySpan NOTIFY frequencySpanChanged)
    Q_PROPERTY(int resolutionIndex READ resolutionIndex WRITE setResolutionIndex NOTIFY resolutionIndexChanged)
    Q_PROPERTY(int windowIndex READ windowIndex WRITE setWindowIndex NOTIFY windowIndexChanged)
    // Averaging
    Q_PROPERTY(AveragingMode averagingMode READ averagingMode WRITE setAveragingMode NOTIFY averagingModeChanged)
    Q_PROPERTY(Weighting weighting READ weighting WRITE setWeighting NOTIFY weightingChanged)
    Q_PROPERTY(int averages READ averages WRITE setAverages NOTIFY averagesChanged)
    // Frequency Display (+ the Display Mode selector under the spectrum)
    Q_PROPERTY(DisplayUnits units READ units WRITE setUnits NOTIFY unitsChanged)
    Q_PROPERTY(MagnitudeMode magnitudeMode READ magnitudeMode WRITE setMagnitudeMode NOTIFY magnitudeModeChanged)
    Q_PROPERTY(SpectrumDisplay spectrumDisplay READ spectrumDisplay WRITE setSpectrumDisplay NOTIFY spectrumDisplayChanged)
    // Scale Settings
    Q_PROPERTY(ScaleMode scaleMode READ scaleMode WRITE setScaleMode NOTIFY scaleModeChanged)
    Q_PROPERTY(double scaleMaximum READ scaleMaximum WRITE setScaleMaximum NOTIFY scaleMaximumChanged)
    Q_PROPERTY(double scaleMinimum READ scaleMinimum WRITE setScaleMinimum NOTIFY scaleMinimumChanged)
    // Cursor Settings
    Q_PROPERTY(bool cursorsOn READ cursorsOn WRITE setCursorsOn NOTIFY cursorsOnChanged)
    Q_PROPERTY(CursorPlot cursor1Plot READ cursor1Plot WRITE setCursor1Plot NOTIFY cursor1PlotChanged)
    Q_PROPERTY(CursorPlot cursor2Plot READ cursor2Plot WRITE setCursor2Plot NOTIFY cursor2PlotChanged)
    Q_PROPERTY(int cursorSelect READ cursorSelect WRITE setCursorSelect NOTIFY cursorSelectChanged)
    // View (MYBENCH enhancements)
    Q_PROPERTY(bool harmonicMarkers READ harmonicMarkers WRITE setHarmonicMarkers NOTIFY harmonicMarkersChanged)
    Q_PROPERTY(bool analysisTables READ analysisTables WRITE setAnalysisTables NOTIFY analysisTablesChanged)
    Q_PROPERTY(bool waterfallVisible READ waterfallVisible WRITE setWaterfallVisible NOTIFY waterfallVisibleChanged)
    Q_PROPERTY(int waterfallHistory READ waterfallHistory WRITE setWaterfallHistory NOTIFY waterfallHistoryChanged)
    Q_PROPERTY(bool maxHold READ maxHold WRITE setMaxHold NOTIFY maxHoldChanged)
    Q_PROPERTY(bool minHold READ minHold WRITE setMinHold NOTIFY minHoldChanged)
    Q_PROPERTY(bool logFrequency READ logFrequency WRITE setLogFrequency NOTIFY logFrequencyChanged)
    // 1/3 Octave settings
    Q_PROPERTY(OctaveAveraging octaveAveraging READ octaveAveraging WRITE setOctaveAveraging NOTIFY octaveAveragingChanged)
    Q_PROPERTY(double octaveTimeConstant READ octaveTimeConstant WRITE setOctaveTimeConstant NOTIFY octaveTimeConstantChanged)
    Q_PROPERTY(int octaveLowBand READ octaveLowBand WRITE setOctaveLowBand NOTIFY octaveLowBandChanged)
    Q_PROPERTY(int octaveHighBand READ octaveHighBand WRITE setOctaveHighBand NOTIFY octaveHighBandChanged)
    Q_PROPERTY(OctaveWeighting octaveWeighting READ octaveWeighting WRITE setOctaveWeighting NOTIFY octaveWeightingChanged)

    // --- View state (not persisted) --------------------------------------------------------------
    // Cursor positions in their plot's x unit (Hz or s), snapped to lines / samples.
    Q_PROPERTY(double cursor1Position READ cursor1Position WRITE setCursor1Position NOTIFY cursorsChanged STORED false)
    Q_PROPERTY(double cursor2Position READ cursor2Position WRITE setCursor2Position NOTIFY cursorsChanged STORED false)

    // --- Value lists -----------------------------------------------------------------------------
    Q_PROPERTY(QStringList sourceLabels READ sourceLabels CONSTANT)
    Q_PROPERTY(QStringList rangeLabels READ rangeLabels CONSTANT)
    Q_PROPERTY(QStringList resolutionLabels READ resolutionLabels CONSTANT)
    Q_PROPERTY(QStringList windowLabels READ windowLabels CONSTANT)
    Q_PROPERTY(QStringList octaveBandLabels READ octaveBandLabels CONSTANT)

    // --- Derived settings --------------------------------------------------------------------------
    Q_PROPERTY(bool octave READ isOctave NOTIFY analysisModeChanged)
    Q_PROPERTY(bool audioSource READ audioSource NOTIFY sourceChannelChanged)
    Q_PROPERTY(int effectiveRangeIndex READ effectiveRangeIndex NOTIFY acquisitionSettingsChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChannelChanged)
    Q_PROPERTY(double maximumSpan READ maximumSpan NOTIFY acquisitionSettingsChanged)
    Q_PROPERTY(int lines READ lines NOTIFY resolutionIndexChanged)
    Q_PROPERTY(double expectedSampleRate READ expectedSampleRate NOTIFY acquisitionSettingsChanged)
    Q_PROPERTY(double lineSpacing READ lineSpacing NOTIFY acquisitionSettingsChanged)   // df, Hz
    Q_PROPERTY(bool weightingEnabled READ weightingEnabled NOTIFY averagingModeChanged)
    Q_PROPERTY(bool averagesEnabled READ averagesEnabled NOTIFY averagingModeChanged)
    Q_PROPERTY(bool restartEnabled READ restartEnabled NOTIFY restartEnabledChanged)
    Q_PROPERTY(QString magnitudeUnit READ magnitudeUnit NOTIFY displayChanged)   // spectrum y unit
    Q_PROPERTY(QString levelUnit READ levelUnit NOTIFY displayChanged)           // markers, tables
    Q_PROPERTY(QString powerUnit READ powerUnit NOTIFY displayChanged)           // fundamental power
    Q_PROPERTY(bool logarithmicUnits READ logarithmicUnits NOTIFY displayChanged)
    Q_PROPERTY(double spectrumMinimum READ spectrumMinimum NOTIFY scaleChanged)   // y range shown
    Q_PROPERTY(double spectrumMaximum READ spectrumMaximum NOTIFY scaleChanged)
    Q_PROPERTY(double frequencyMinimum READ frequencyMinimum NOTIFY frequencyRangeChanged)   // x range
    Q_PROPERTY(double frequencyMaximum READ frequencyMaximum NOTIFY frequencyRangeChanged)
    Q_PROPERTY(double recordDuration READ recordDuration NOTIFY frequencyRangeChanged)       // time plot, s
    Q_PROPERTY(QString octaveWeightingSuffix READ octaveWeightingSuffix NOTIFY octaveWeightingChanged)

    // --- Results (FFT) -------------------------------------------------------------------------
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultsChanged)
    Q_PROPERTY(double fundamentalFrequency READ fundamentalFrequency NOTIFY resultsChanged)
    Q_PROPERTY(double fundamentalPower READ fundamentalPower NOTIFY resultsChanged)       // in powerUnit
    Q_PROPERTY(QString fundamentalPowerText READ fundamentalPowerText NOTIFY resultsChanged)
    Q_PROPERTY(QString fundamentalPowerValueText READ fundamentalPowerValueText NOTIFY resultsChanged)   // number only
    Q_PROPERTY(double thd READ thd NOTIFY resultsChanged)         // %
    Q_PROPERTY(double sinad READ sinad NOTIFY resultsChanged)     // dB
    Q_PROPERTY(double snr READ snr NOTIFY resultsChanged)         // dB, SINAD without the harmonics
    Q_PROPERTY(double vpk READ vpk NOTIFY resultsChanged)         // V
    Q_PROPERTY(double resultLineSpacing READ resultLineSpacing NOTIFY resultsChanged)   // df of the result
    Q_PROPERTY(double resultSampleRate READ resultSampleRate NOTIFY resultsChanged)
    Q_PROPERTY(int averagesDone READ averagesDone NOTIFY resultsChanged)
    Q_PROPERTY(int averagesTarget READ averagesTarget NOTIFY resultsChanged)
    Q_PROPERTY(QString averagingText READ averagingText NOTIFY resultsChanged)
    // [{ order, name, frequency, frequencyText, level, levelText, dbc, dbcText }], F first.
    Q_PROPERTY(QVariantList harmonics READ harmonics NOTIFY resultsChanged)
    // Strongest peaks: [{ frequency, frequencyText, level, levelText, dbc, dbcText }].
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY resultsChanged)

    // --- Results (1/3 octave) ---------------------------------------------------------------------
    Q_PROPERTY(bool hasOctaveResult READ hasOctaveResult NOTIFY octaveResultsChanged)
    Q_PROPERTY(double octaveOverall READ octaveOverall NOTIFY octaveResultsChanged)   // dBV (weighted)
    Q_PROPERTY(bool octaveSettled READ octaveSettled NOTIFY octaveResultsChanged)
    Q_PROPERTY(QString octaveLoudestBand READ octaveLoudestBand NOTIFY octaveResultsChanged)
    Q_PROPERTY(double octaveLoudestLevel READ octaveLoudestLevel NOTIFY octaveResultsChanged)
    Q_PROPERTY(double octaveMinimum READ octaveMinimum NOTIFY octaveResultsChanged)   // y range
    Q_PROPERTY(double octaveMaximum READ octaveMaximum NOTIFY octaveResultsChanged)
    Q_PROPERTY(double octaveFrequencyMinimum READ octaveFrequencyMinimum NOTIFY octaveRangeChanged)
    Q_PROPERTY(double octaveFrequencyMaximum READ octaveFrequencyMaximum NOTIFY octaveRangeChanged)
    Q_PROPERTY(double octaveSettlingTime READ octaveSettlingTime NOTIFY octaveRangeChanged)   // s
    // [{ label, centre, level, levelText }] of the selected bands.
    Q_PROPERTY(QVariantList octaveLevels READ octaveLevels NOTIFY octaveResultsChanged)

    // --- Cursor readouts -------------------------------------------------------------------------
    Q_PROPERTY(QString cursor1Text READ cursor1Text NOTIFY cursorsChanged)
    Q_PROPERTY(QString cursor2Text READ cursor2Text NOTIFY cursorsChanged)
    Q_PROPERTY(QString cursorDeltaText READ cursorDeltaText NOTIFY cursorsChanged)

    // --- Traces ----------------------------------------------------------------------------------
    Q_PROPERTY(mybench::plot::TraceBuffer* spectrum READ spectrum CONSTANT)       // x Hz, y magnitudeUnit
    Q_PROPERTY(mybench::plot::TraceBuffer* maxHoldTrace READ maxHoldTrace CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* minHoldTrace READ minHoldTrace CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* timeSignal READ timeSignal CONSTANT)   // x s, y V
    Q_PROPERTY(mybench::plot::TraceBuffer* octaveBars READ octaveBars CONSTANT)   // x Hz, y dBV
    Q_PROPERTY(mybench::plot::WaterfallBuffer* waterfall READ waterfall CONSTANT)

public:
    enum AnalysisMode { FftAnalysis, OctaveAnalysis };
    Q_ENUM(AnalysisMode)
    enum AveragingMode { NoAveraging, VectorAveraging, RmsAveraging, PeakHoldAveraging };
    Q_ENUM(AveragingMode)
    enum Weighting { LinearWeighting, ExponentialWeighting };
    Q_ENUM(Weighting)
    enum DisplayUnits { LinearUnits, DbUnits, DbmUnits };
    Q_ENUM(DisplayUnits)
    enum MagnitudeMode { RmsMagnitude, PeakMagnitude };
    Q_ENUM(MagnitudeMode)
    enum SpectrumDisplay { PowerSpectrumDisplay, PowerSpectralDensityDisplay };
    Q_ENUM(SpectrumDisplay)
    enum ScaleMode { AutoScale, ManualScale };
    Q_ENUM(ScaleMode)
    enum CursorPlot { SpectrumPlot, TimePlot };
    Q_ENUM(CursorPlot)
    enum PeakAction { HighestPeak, NextPeak, PeakLeft, PeakRight };
    Q_ENUM(PeakAction)
    enum OctaveAveraging { SlowAveraging, FastAveraging, ImpulseAveraging, CustomAveraging };
    Q_ENUM(OctaveAveraging)
    enum OctaveWeighting { ZWeighting, AWeighting, BWeighting, CWeighting };
    Q_ENUM(OctaveWeighting)

    DsaController();
    ~DsaController() override;

    // Settings
    AnalysisMode analysisMode() const { return m_analysisMode; }
    void setAnalysisMode(AnalysisMode mode);
    int sourceChannel() const { return m_source; }
    void setSourceChannel(int channel);
    int voltageRangeIndex() const { return m_rangeIndex; }
    void setVoltageRangeIndex(int index);
    double frequencySpan() const { return m_span; }
    void setFrequencySpan(double hertz);
    int resolutionIndex() const { return m_resolutionIndex; }
    void setResolutionIndex(int index);
    int windowIndex() const { return m_windowIndex; }
    void setWindowIndex(int index);
    AveragingMode averagingMode() const { return m_averagingMode; }
    void setAveragingMode(AveragingMode mode);
    Weighting weighting() const { return m_weighting; }
    void setWeighting(Weighting weighting);
    int averages() const { return m_averages; }
    void setAverages(int count);
    DisplayUnits units() const { return m_units; }
    void setUnits(DisplayUnits units);
    MagnitudeMode magnitudeMode() const { return m_magnitudeMode; }
    void setMagnitudeMode(MagnitudeMode mode);
    SpectrumDisplay spectrumDisplay() const { return m_spectrumDisplay; }
    void setSpectrumDisplay(SpectrumDisplay display);
    ScaleMode scaleMode() const { return m_scaleMode; }
    void setScaleMode(ScaleMode mode);
    double scaleMaximum() const { return m_scaleMaximum; }
    void setScaleMaximum(double value);
    double scaleMinimum() const { return m_scaleMinimum; }
    void setScaleMinimum(double value);
    bool cursorsOn() const { return m_cursorsOn; }
    void setCursorsOn(bool on);
    CursorPlot cursor1Plot() const { return m_cursorPlot[0]; }
    void setCursor1Plot(CursorPlot plot) { setCursorPlot(0, plot); }
    CursorPlot cursor2Plot() const { return m_cursorPlot[1]; }
    void setCursor2Plot(CursorPlot plot) { setCursorPlot(1, plot); }
    int cursorSelect() const { return m_cursorSelect; }
    void setCursorSelect(int cursor);
    bool harmonicMarkers() const { return m_harmonicMarkers; }
    void setHarmonicMarkers(bool on);
    bool analysisTables() const { return m_analysisTables; }
    void setAnalysisTables(bool on);
    bool waterfallVisible() const { return m_waterfallVisible; }
    void setWaterfallVisible(bool on);
    int waterfallHistory() const { return m_waterfallHistory; }
    void setWaterfallHistory(int rows);
    bool maxHold() const { return m_maxHold; }
    void setMaxHold(bool on);
    bool minHold() const { return m_minHold; }
    void setMinHold(bool on);
    bool logFrequency() const { return m_logFrequency; }
    void setLogFrequency(bool on);
    OctaveAveraging octaveAveraging() const { return m_octaveAveraging; }
    void setOctaveAveraging(OctaveAveraging mode);
    double octaveTimeConstant() const { return m_octaveTimeConstant; }
    void setOctaveTimeConstant(double milliseconds);
    int octaveLowBand() const { return m_octaveLowBand; }
    void setOctaveLowBand(int band);
    int octaveHighBand() const { return m_octaveHighBand; }
    void setOctaveHighBand(int band);
    OctaveWeighting octaveWeighting() const { return m_octaveWeighting; }
    void setOctaveWeighting(OctaveWeighting weighting);

    double cursor1Position() const { return m_cursorPosition[0]; }
    void setCursor1Position(double x) { moveCursor(0, x); }
    double cursor2Position() const { return m_cursorPosition[1]; }
    void setCursor2Position(double x) { moveCursor(1, x); }

    // Lists
    QStringList sourceLabels() const;
    QStringList rangeLabels() const;
    QStringList resolutionLabels() const;
    QStringList windowLabels() const;
    QStringList octaveBandLabels() const;

    // Derived
    bool isOctave() const { return m_analysisMode == OctaveAnalysis; }
    bool audioSource() const;
    int effectiveRangeIndex() const { return audioSource() ? 1 : m_rangeIndex; }
    QString sourceName() const;
    double maximumSpan() const;
    int lines() const;
    double expectedSampleRate() const;
    double lineSpacing() const;
    bool weightingEnabled() const;
    bool averagesEnabled() const { return m_averagingMode != NoAveraging; }
    bool restartEnabled() const;
    QString magnitudeUnit() const;
    QString levelUnit() const;
    QString powerUnit() const;
    bool logarithmicUnits() const { return m_units != LinearUnits; }
    double spectrumMinimum() const { return m_scale.minimum; }
    double spectrumMaximum() const { return m_scale.maximum; }
    double frequencyMinimum() const;
    double frequencyMaximum() const;
    double recordDuration() const;
    QString octaveWeightingSuffix() const;

    // Results
    bool hasResult() const { return m_frame != nullptr; }
    double fundamentalFrequency() const;
    double fundamentalPower() const;
    QString fundamentalPowerText() const;
    QString fundamentalPowerValueText() const;
    double thd() const;
    double sinad() const;
    double snr() const;
    double vpk() const;
    double resultLineSpacing() const;
    double resultSampleRate() const;
    int averagesDone() const;
    int averagesTarget() const;
    QString averagingText() const;
    QVariantList harmonics() const { return m_harmonicList; }
    QVariantList peaks() const { return m_peakList; }

    bool hasOctaveResult() const { return m_octaveFrame != nullptr; }
    double octaveOverall() const { return m_octaveOverall; }
    bool octaveSettled() const { return m_octaveSettled; }
    QString octaveLoudestBand() const { return m_octaveLoudestBand; }
    double octaveLoudestLevel() const { return m_octaveLoudestLevel; }
    double octaveMinimum() const { return m_octaveScale.minimum; }
    double octaveMaximum() const { return m_octaveScale.maximum; }
    double octaveFrequencyMinimum() const;
    double octaveFrequencyMaximum() const;
    double octaveSettlingTime() const;
    QVariantList octaveLevels() const { return m_octaveLevelList; }

    QString cursor1Text() const { return m_cursorText[0]; }
    QString cursor2Text() const { return m_cursorText[1]; }
    QString cursorDeltaText() const { return m_cursorDeltaText; }

    mybench::plot::TraceBuffer* spectrum() { return &m_spectrumTrace; }
    mybench::plot::TraceBuffer* maxHoldTrace() { return &m_maxHoldTrace; }
    mybench::plot::TraceBuffer* minHoldTrace() { return &m_minHoldTrace; }
    mybench::plot::TraceBuffer* timeSignal() { return &m_timeTrace; }
    mybench::plot::TraceBuffer* octaveBars() { return &m_octaveTrace; }
    mybench::plot::WaterfallBuffer* waterfall() { return &m_waterfall; }

    // Actions
    Q_INVOKABLE void restartAveraging();          // the ELVISmx "Averaging: Restart" button
    Q_INVOKABLE void moveCursor(int cursor, double position);   // snaps to a line / sample
    Q_INVOKABLE void stepCursor(int direction);   // Position Left (-1) / Right (+1): one point
    Q_INVOKABLE void searchPeak(PeakAction action);   // for the selected cursor

    // The display conversion of the current settings (tests, export).
    mydaq::dsp::SpectrumFormat displayFormat() const;
    // The displayed spectrum values (magnitudeUnit), one per line; empty before the first result.
    const std::vector<double>& displayedSpectrum() const { return m_display; }

signals:
    void analysisModeChanged();
    void sourceChannelChanged();
    void voltageRangeIndexChanged();
    void frequencySpanChanged();
    void resolutionIndexChanged();
    void windowIndexChanged();
    void averagingModeChanged();
    void weightingChanged();
    void averagesChanged();
    void unitsChanged();
    void magnitudeModeChanged();
    void spectrumDisplayChanged();
    void scaleModeChanged();
    void scaleMaximumChanged();
    void scaleMinimumChanged();
    void cursorsOnChanged();
    void cursor1PlotChanged();
    void cursor2PlotChanged();
    void cursorSelectChanged();
    void harmonicMarkersChanged();
    void analysisTablesChanged();
    void waterfallVisibleChanged();
    void waterfallHistoryChanged();
    void maxHoldChanged();
    void minHoldChanged();
    void logFrequencyChanged();
    void octaveAveragingChanged();
    void octaveTimeConstantChanged();
    void octaveLowBandChanged();
    void octaveHighBandChanged();
    void octaveWeightingChanged();

    void acquisitionSettingsChanged();
    void restartEnabledChanged();
    void displayChanged();
    void scaleChanged();
    void frequencyRangeChanged();
    void resultsChanged();
    void octaveResultsChanged();
    void octaveRangeChanged();
    void cursorsChanged();

protected:
    WorkerFunction createWorker() override;
    DataTable exportTable() const override;
    void onStarted() override;
    void onFinished() override;

private:
    // Settings of a run that need a new task (restart()).
    struct Acquisition {
        mydaq::AiChannel channel = mydaq::AiChannel::AI0;
        double range = 10.0;
        double span = 40000;
        int lines = 400;
    };
    // Settings applied live by the worker (Mailbox); posting one also restarts the averaging.
    struct LiveSettings {
        dsa::AveragingSettings averaging;
        octave::TimeConstants timeConstants;
        int lowBand = 0;
    };
    // One published FFT result (worker → UI thread).
    struct SpectrumFrame {
        mydaq::dsp::PowerSpectrum averaged;   // V² per line
        std::vector<double> maxHold, minHold; // V² per line
        std::vector<double> time;             // record through the scaled window (V)
        double sampleRate = 0;
        double span = 0;
        int lines = 0;
        dsa::AveragingSettings averaging;
        dsa::Measurements measurements;
        std::vector<dsa::SpectralPeak> peaks;
        int count = 0;
        int target = 1;
        bool complete = false;
        mydaq::AiChannel channel = mydaq::AiChannel::AI0;
        double range = 10;
    };
    // One published octave result.
    struct OctaveFrame {
        std::vector<double> meanSquare;   // V² per band (all bands of the bank)
        std::vector<double> settling;     // s per band
        std::vector<double> time;         // latest block (V)
        double sampleRate = 0;
        double elapsed = 0;
        mydaq::AiChannel channel = mydaq::AiChannel::AI0;
        double range = 10;
    };

    Acquisition acquisition() const;
    LiveSettings liveSettings() const;
    void postLiveSettings();
    WorkerFunction createSpectrumWorker();
    WorkerFunction createOctaveWorker();
    void applySpectrumFrame(std::shared_ptr<const SpectrumFrame> frame);
    void applyOctaveFrame(std::shared_ptr<const OctaveFrame> frame);
    void updateSpectrumDisplay(bool newData);
    void updateOctaveDisplay();
    void updateScale();
    void updateCursors();
    void resetCursor(int cursor);
    void setCursorPlot(int cursor, CursorPlot plot);
    double cursorStep(int cursor) const;
    double cursorLimit(int cursor) const;
    void updateRestartEnabled();
    QString statusFor(const SpectrumFrame& frame) const;
    mydaq::dsp::AveragingMode dspAveraging() const;

    // Settings (member initializers = documented defaults, dsa.md §2 / algorithms §1).
    AnalysisMode m_analysisMode = FftAnalysis;
    int m_source = 0;                          // AI 0
    int m_rangeIndex = 0;                      // +/- 10V
    double m_span = 40000;                     // Hz
    int m_resolutionIndex = 1;                 // 400 lines
    int m_windowIndex = 0;                     // None
    AveragingMode m_averagingMode = RmsAveraging;
    Weighting m_weighting = ExponentialWeighting;
    int m_averages = 5;
    DisplayUnits m_units = DbUnits;
    MagnitudeMode m_magnitudeMode = RmsMagnitude;
    SpectrumDisplay m_spectrumDisplay = PowerSpectrumDisplay;
    ScaleMode m_scaleMode = AutoScale;
    double m_scaleMaximum = 10;
    double m_scaleMinimum = -120;
    bool m_cursorsOn = false;
    CursorPlot m_cursorPlot[2] = {SpectrumPlot, SpectrumPlot};
    int m_cursorSelect = 0;
    bool m_harmonicMarkers = true;
    bool m_analysisTables = false;
    bool m_waterfallVisible = false;
    int m_waterfallHistory = 200;
    bool m_maxHold = false;
    bool m_minHold = false;
    bool m_logFrequency = false;
    OctaveAveraging m_octaveAveraging = FastAveraging;
    double m_octaveTimeConstant = 125;         // ms (Custom)
    int m_octaveLowBand = 0;                    // 20 Hz (index into the 31 ISO bands)
    int m_octaveHighBand = 30;                 // 20 kHz
    OctaveWeighting m_octaveWeighting = ZWeighting;

    // View state and results (UI thread).
    double m_cursorPosition[2] = {0, 0};
    bool m_cursorPlaced[2] = {false, false};
    QString m_cursorText[2];
    QString m_cursorDeltaText;
    std::shared_ptr<const SpectrumFrame> m_frame;
    QDateTime m_frameStamp;                    // wall clock when the block arrived (log t0)
    std::vector<double> m_display;             // displayed spectrum (magnitudeUnit)
    dsa::ValueRange m_scale{-120, 10};
    std::optional<dsa::ValueRange> m_autoScale;
    QVariantList m_harmonicList;
    QVariantList m_peakList;
    int m_waterfallColumns = 0;
    double m_waterfallDf = 0;
    std::shared_ptr<const OctaveFrame> m_octaveFrame;
    QDateTime m_octaveStamp;
    double m_octaveOverall = 0;
    bool m_octaveSettled = false;
    QString m_octaveLoudestBand;
    double m_octaveLoudestLevel = 0;
    dsa::ValueRange m_octaveScale{-100, 0};
    std::optional<dsa::ValueRange> m_octaveAutoScale;
    QVariantList m_octaveLevelList;
    bool m_restartEnabled = false;

    // Shared with the worker.
    std::shared_ptr<Mailbox<LiveSettings>> m_live = std::make_shared<Mailbox<LiveSettings>>();
    std::shared_ptr<std::atomic<unsigned>> m_restartRequests = std::make_shared<std::atomic<unsigned>>(0);

    mybench::plot::TraceBuffer m_spectrumTrace;
    mybench::plot::TraceBuffer m_maxHoldTrace;
    mybench::plot::TraceBuffer m_minHoldTrace;
    mybench::plot::TraceBuffer m_timeTrace;
    mybench::plot::TraceBuffer m_octaveTrace;
    mybench::plot::WaterfallBuffer m_waterfall;
};

void registerDsa(InstrumentRegistry& registry);

}  // namespace mybench::instruments
