// GenChannel: one output channel of the Waveform Generator (AO 0 / AO 1, or AudioOutput Left /
// Right): its mode (Off, Function, Arbitrary, DC), the settings of each mode (ELVISmx FGEN, ARB and
// DC Level), its arbitrary waveform, its generation state and its previews. Owned by
// GenController, which persists the settings (saveExtraSettings) and runs the engine.
#pragma once

#include "genengine.h"
#include "genwaveform.h"

#include <mybench/plot/tracebuffer.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

namespace mybench::instruments {

class GenChannel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by GenController")

    Q_PROPERTY(int index READ index CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)          // "AO 0", "AudioOutput Left"
    Q_PROPERTY(bool audio READ isAudio NOTIFY nameChanged)
    Q_PROPERTY(double outputRange READ outputRange NOTIFY nameChanged)   // V peak

    // --- Settings (persisted, in restore order) --------------------------------------------------
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    // Function (ELVISmx Function Generator)
    Q_PROPERTY(Shape shape READ shape WRITE setShape NOTIFY functionChanged)
    Q_PROPERTY(double frequency READ frequency WRITE setFrequency NOTIFY functionChanged)
    Q_PROPERTY(double amplitude READ amplitude WRITE setAmplitude NOTIFY functionChanged)
    Q_PROPERTY(double offset READ offset WRITE setOffset NOTIFY functionChanged)
    Q_PROPERTY(double dutyCycle READ dutyCycle WRITE setDutyCycle NOTIFY functionChanged)
    Q_PROPERTY(double phase READ phase WRITE setPhase NOTIFY functionChanged)
    Q_PROPERTY(double sweepStart READ sweepStart WRITE setSweepStart NOTIFY sweepChanged)
    Q_PROPERTY(double sweepStop READ sweepStop WRITE setSweepStop NOTIFY sweepChanged)
    Q_PROPERTY(double sweepStep READ sweepStep WRITE setSweepStep NOTIFY sweepChanged)
    Q_PROPERTY(double sweepInterval READ sweepInterval WRITE setSweepInterval NOTIFY sweepChanged)   // ms
    Q_PROPERTY(bool sweepBlockTiming READ sweepBlockTiming WRITE setSweepBlockTiming NOTIFY sweepChanged)
    // Arbitrary (ELVISmx Arbitrary Waveform Generator)
    Q_PROPERTY(double gain READ gain WRITE setGain NOTIFY arbitraryChanged)
    Q_PROPERTY(double updateRate READ updateRate WRITE setUpdateRate NOTIFY arbitraryChanged)
    Q_PROPERTY(GenerationMode generationMode READ generationMode WRITE setGenerationMode NOTIFY arbitraryChanged)
    Q_PROPERTY(Interpolation interpolation READ interpolation WRITE setInterpolation NOTIFY arbitraryChanged)
    // DC (ELVISmx DC Level)
    Q_PROPERTY(double dcLevel READ dcLevel WRITE setDcLevel NOTIFY dcChanged)

    // --- Arbitrary waveform (persisted by the controller) ----------------------------------------
    Q_PROPERTY(bool hasWaveform READ hasWaveform NOTIFY waveformChanged)
    Q_PROPERTY(QString waveformName READ waveformName NOTIFY waveformChanged)   // "< empty >"
    Q_PROPERTY(QString waveformPath READ waveformPath NOTIFY waveformChanged)
    Q_PROPERTY(QString waveformDetails READ waveformDetails NOTIFY waveformChanged)   // tooltip
    Q_PROPERTY(int waveformPoints READ waveformPoints NOTIFY waveformChanged)
    Q_PROPERTY(double waveformDuration READ waveformDuration NOTIFY previewChanged)   // at the Update Rate
    Q_PROPERTY(bool waveformIsDocument READ waveformIsDocument NOTIFY waveformChanged)
    Q_PROPERTY(int clippedSamples READ clippedSamples NOTIFY previewChanged)

    // --- Generation state (not persisted) --------------------------------------------------------
    Q_PROPERTY(bool outputOn READ outputOn NOTIFY stateChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool generating READ generating NOTIFY stateChanged)   // Running or Sweeping
    Q_PROPERTY(bool sweeping READ sweeping NOTIFY stateChanged)
    Q_PROPERTY(QString readoutText READ readoutText NOTIFY readoutChanged)   // "OFF", "100.0000 Hz"...
    Q_PROPERTY(QString readoutCaption READ readoutCaption NOTIFY readoutChanged)
    Q_PROPERTY(double actualFrequency READ actualFrequency NOTIFY readoutChanged)
    Q_PROPERTY(double expectedFrequency READ expectedFrequency NOTIFY readoutChanged)   // idle: what Run would give
    Q_PROPERTY(double actualUpdateRate READ actualUpdateRate NOTIFY readoutChanged)
    Q_PROPERTY(int sweepStepIndex READ sweepStepIndex NOTIFY readoutChanged)   // 1-based
    Q_PROPERTY(int sweepStepCount READ sweepStepCount NOTIFY readoutChanged)
    Q_PROPERTY(double sweepProgress READ sweepProgress NOTIFY readoutChanged)
    Q_PROPERTY(double heldLevel READ heldLevel NOTIFY readoutChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)   // inline card error
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)   // "100 Hz sine · 1 Vpp"

    // --- Previews ----------------------------------------------------------------------------------
    Q_PROPERTY(mybench::plot::TraceBuffer* preview READ preview CONSTANT)     // time (s, V)
    Q_PROPERTY(mybench::plot::TraceBuffer* spectrum READ spectrum CONSTANT)   // frequency (Hz, dBV)
    Q_PROPERTY(mybench::plot::TraceBuffer* thumbnail READ thumbnail CONSTANT) // arbitrary waveform, sample index
    Q_PROPERTY(mybench::plot::TraceBuffer* history READ history CONSTANT)     // DC level vs time
    Q_PROPERTY(double previewSpan READ previewSpan NOTIFY previewChanged)     // s
    Q_PROPERTY(double previewMin READ previewMin NOTIFY previewChanged)       // V
    Q_PROPERTY(double previewMax READ previewMax NOTIFY previewChanged)
    Q_PROPERTY(bool previewStep READ previewStep NOTIFY previewChanged)       // draw as steps (coarse samples)
    Q_PROPERTY(double spectrumSpan READ spectrumSpan NOTIFY previewChanged)   // Hz
    Q_PROPERTY(double historySpan READ historySpan NOTIFY historyChanged)     // s shown by the DC chart

public:
    enum Mode { Off, Function, Arbitrary, Dc };
    Q_ENUM(Mode)
    enum Shape { Sine, Triangle, Square };
    Q_ENUM(Shape)
    enum GenerationMode { RunContinuously, RunOnce };
    Q_ENUM(GenerationMode)
    enum Interpolation { Hold, Linear };
    Q_ENUM(Interpolation)
    enum State { Inactive, Ready, Running, Sweeping, Done, Paused };
    Q_ENUM(State)

    // Where the arbitrary waveform comes from.
    struct Source {
        enum Kind { None, File, Document } kind = None;
        QString path;                            // File (and a saved Document)
        QString name;
        std::optional<gen::Document> document;   // Document (editor, .mbw, library)
        std::shared_ptr<const std::vector<double>> samples;
        double sampleRate = 0;                   // carried by the file / document (0 = none)
    };

    GenChannel(int index, QObject* parent = nullptr);

    int index() const { return m_index; }
    QString name() const;
    bool isAudio() const { return m_pair == gen::OutputPair::AudioOutputs; }
    double outputRange() const { return gen::outputRange(m_pair); }
    void setPair(gen::OutputPair pair);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    Shape shape() const { return m_function.shape == gen::Shape::Sine ? Sine : m_function.shape == gen::Shape::Triangle ? Triangle : Square; }
    void setShape(Shape shape);
    double frequency() const { return m_function.frequency; }
    void setFrequency(double hz);
    double amplitude() const { return m_function.amplitude; }
    void setAmplitude(double vpp);
    double offset() const { return m_function.offset; }
    void setOffset(double volts);
    double dutyCycle() const { return m_function.dutyCycle; }
    void setDutyCycle(double percent);
    double phase() const { return m_function.phase; }
    void setPhase(double degrees);
    double sweepStart() const { return m_sweep.start; }
    void setSweepStart(double hz);
    double sweepStop() const { return m_sweep.stop; }
    void setSweepStop(double hz);
    double sweepStep() const { return m_sweep.step; }
    void setSweepStep(double hz);
    double sweepInterval() const { return m_sweep.intervalMs; }
    void setSweepInterval(double ms);
    bool sweepBlockTiming() const { return m_sweep.blockTiming; }
    void setSweepBlockTiming(bool on);
    double gain() const { return m_gain; }
    void setGain(double gain);
    double updateRate() const { return m_updateRate; }
    void setUpdateRate(double rate);
    GenerationMode generationMode() const { return m_generationMode; }
    void setGenerationMode(GenerationMode mode);
    Interpolation interpolation() const { return m_interpolation; }
    void setInterpolation(Interpolation interpolation);
    double dcLevel() const { return m_dcLevel; }
    void setDcLevel(double volts);

    bool hasWaveform() const { return m_source.samples && !m_source.samples->empty(); }
    QString waveformName() const;
    QString waveformPath() const { return m_source.path; }
    QString waveformDetails() const;
    int waveformPoints() const { return m_source.samples ? int(m_source.samples->size()) : 0; }
    double waveformDuration() const;
    bool waveformIsDocument() const { return m_source.kind == Source::Document; }
    int clippedSamples() const { return m_clipped; }
    const Source& source() const { return m_source; }
    void setSource(Source source);   // new waveform (preview, points; not the Update Rate)
    void clearSource();

    bool outputOn() const { return m_outputOn; }
    State state() const;
    bool generating() const { return m_engineState == gen::ChannelState::Running || m_engineState == gen::ChannelState::Sweeping; }
    bool sweeping() const { return m_sweepRequested && m_outputOn; }
    QString readoutText() const;
    QString readoutCaption() const;
    double actualFrequency() const { return m_status.frequency; }
    double expectedFrequency() const { return m_expectedFrequency; }
    double actualUpdateRate() const { return m_status.updateRate; }
    int sweepStepIndex() const { return m_status.sweepStep; }
    int sweepStepCount() const { return m_status.sweepSteps; }
    double sweepProgress() const;
    double heldLevel() const { return m_status.level; }
    QString errorText() const { return m_errorText; }
    void setErrorText(const QString& text);
    QString summary() const;

    mybench::plot::TraceBuffer* preview() { return &m_preview; }
    mybench::plot::TraceBuffer* spectrum() { return &m_spectrum; }
    mybench::plot::TraceBuffer* thumbnail() { return &m_thumbnail; }
    mybench::plot::TraceBuffer* history() { return &m_history; }
    double previewSpan() const { return m_previewSpan; }
    double previewMin() const { return m_previewMin; }
    double previewMax() const { return m_previewMax; }
    bool previewStep() const { return m_previewStep; }
    double spectrumSpan() const { return m_spectrumSpan; }
    double historySpan() const { return 60.0; }

    // --- Used by the controller --------------------------------------------------------------------
    gen::ChannelSettings engineSettings() const;
    void setOutput(bool on, bool sweep, uint64_t startId);
    void setPaused(bool paused);
    bool paused() const { return m_paused; }
    uint64_t startId() const { return m_startId; }
    void applyStatus(const gen::ChannelStatus& status, gen::StrategyKind strategy);
    void setDone(bool done, double level);
    bool done() const { return m_done; }
    void setTaskRate(double rate);
    void setMaxRate(double rate);
    void setSpectrumVisible(bool visible);
    void appendHistory(double t);   // DC chart: current level at time t (s)
    void clearHistory();
    void updatePreview();
    // Persistent properties (settings) of this class.
    static QList<QMetaProperty> settingProperties();

signals:
    void nameChanged();
    void modeChanged();
    void functionChanged();
    void sweepChanged();
    void arbitraryChanged();
    void dcChanged();
    void waveformChanged();
    void stateChanged();
    void readoutChanged();
    void errorTextChanged();
    void summaryChanged();
    void previewChanged();
    void historyChanged();
    void settingChanged();          // any setting (persist + live update)
    void updateRateEdited(double rate);   // the user changed the Update Rate (linked rates)
    void modeAboutToChange();       // stop the channel before switching

private:
    void changed();
    void updateExpected();
    void updateSpectrum();

    int m_index;
    gen::OutputPair m_pair = gen::OutputPair::AnalogOutputs;
    Mode m_mode = Off;
    gen::FunctionSettings m_function;
    gen::SweepSettings m_sweep;
    double m_gain = 1.0;
    double m_updateRate = 1000.0;
    GenerationMode m_generationMode = RunContinuously;
    Interpolation m_interpolation = Hold;
    double m_dcLevel = 0.0;
    Source m_source;
    int m_clipped = 0;

    bool m_outputOn = false;
    bool m_sweepRequested = false;
    bool m_paused = false;
    bool m_done = false;
    uint64_t m_startId = 0;
    gen::ChannelState m_engineState = gen::ChannelState::Idle;
    gen::ChannelStatus m_status;
    gen::StrategyKind m_strategy = gen::StrategyKind::None;
    double m_taskRate = 0;
    double m_maxRate = 200000;
    double m_expectedFrequency = 100;
    QString m_errorText;

    mybench::plot::TraceBuffer m_preview;
    mybench::plot::TraceBuffer m_spectrum;
    mybench::plot::TraceBuffer m_thumbnail;
    mybench::plot::TraceBuffer m_history;
    double m_previewSpan = 0.02;
    double m_previewMin = -1;
    double m_previewMax = 1;
    bool m_previewStep = false;
    double m_spectrumSpan = 1000;
    bool m_spectrumVisible = false;
    double m_lastHistory = -1;
};

// "100.0000 Hz", "1.0000 kHz", "200.0000 mHz": the ELVISmx frequency display (4 decimals).
QString formatFrequencyDisplay(double hz);

}  // namespace mybench::instruments
