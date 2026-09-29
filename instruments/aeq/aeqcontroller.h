// AeqController: the Audio Equalizer (docs/instruments/aeq.md, help docs/help/aeq.md), MYBENCH's
// version of the ELVISmx "Audio Equalizer" featured application.
//
// A continuous AI task (AudioInput L/R, or AI 0/AI 1) and a streaming AO task (AudioOutput L/R,
// or AO 0/AO 1) run at the same rate; the worker reads short blocks, filters each channel
// through three 4th-order Bessel bands (Bass < 320 Hz, Midtone 320-2560 Hz, Treble 2560-20 840 Hz,
// ±12 dB each), scales by Volume and writes the block to the output, which was primed with
// `latency` seconds of silence. Gains, volume and bypass change live (ramped over a block).
// Displays: level meters, input/output spectrum in 1/24-octave bands (or waveforms) and the
// equalizer's frequency response.
#pragma once

#include "equalizer.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>
#include <mybench/worker.h>

#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace mybench::instruments {

class AeqController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings ---------------------------------------------------------------------------
    Q_PROPERTY(Source inputLeft READ inputLeft WRITE setInputLeft NOTIFY routingChanged)
    Q_PROPERTY(Source inputRight READ inputRight WRITE setInputRight NOTIFY routingChanged)
    Q_PROPERTY(Sink outputLeft READ outputLeft WRITE setOutputLeft NOTIFY routingChanged)
    Q_PROPERTY(Sink outputRight READ outputRight WRITE setOutputRight NOTIFY routingChanged)
    Q_PROPERTY(double bass READ bass WRITE setBass NOTIFY eqChanged)
    Q_PROPERTY(double midtone READ midtone WRITE setMidtone NOTIFY eqChanged)
    Q_PROPERTY(double treble READ treble WRITE setTreble NOTIFY eqChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY eqChanged)
    Q_PROPERTY(bool bypass READ bypass WRITE setBypass NOTIFY eqChanged)
    Q_PROPERTY(int rateIndex READ rateIndex WRITE setRateIndex NOTIFY streamChanged)
    Q_PROPERTY(int latencyIndex READ latencyIndex WRITE setLatencyIndex NOTIFY streamChanged)
    Q_PROPERTY(View view READ view WRITE setView NOTIFY displayChanged)
    Q_PROPERTY(int displayChannel READ displayChannel WRITE setDisplayChannel NOTIFY displayChanged)   // 0 = L, 1 = R
    Q_PROPERTY(int averagingIndex READ averagingIndex WRITE setAveragingIndex NOTIFY displayChanged)
    Q_PROPERTY(bool showInput READ showInput WRITE setShowInput NOTIFY displayChanged)
    // Time view span: 0 = 20 ms, 1 = 100 ms (ELVISmx, default); per-graph Auto-scale (default on).
    Q_PROPERTY(int timeSpanIndex READ timeSpanIndex WRITE setTimeSpanIndex NOTIFY displayChanged)
    Q_PROPERTY(bool autoScaleTime READ autoScaleTime WRITE setAutoScaleTime NOTIFY displayChanged)
    Q_PROPERTY(bool autoScaleSpectrum READ autoScaleSpectrum WRITE setAutoScaleSpectrum NOTIFY displayChanged)
    Q_PROPERTY(QStringList timeSpanNames READ timeSpanNames CONSTANT)
    Q_PROPERTY(double timeSpan READ timeSpan NOTIFY displayChanged)   // seconds

    // --- Derived ------------------------------------------------------------------------------
    Q_PROPERTY(QStringList sourceNames READ sourceNames CONSTANT)
    Q_PROPERTY(QStringList sinkNames READ sinkNames CONSTANT)
    Q_PROPERTY(QStringList rateNames READ rateNames CONSTANT)
    Q_PROPERTY(QStringList latencyNames READ latencyNames CONSTANT)
    Q_PROPERTY(QStringList averagingNames READ averagingNames CONSTANT)
    Q_PROPERTY(QString routingError READ routingError NOTIFY routingChanged)   // "" when valid
    Q_PROPERTY(bool feedbackInSimulator READ feedbackInSimulator NOTIFY routingChanged)
    Q_PROPERTY(double streamRate READ streamRate NOTIFY streamChanged)
    Q_PROPERTY(double latencySeconds READ latencySeconds NOTIFY streamChanged)
    Q_PROPERTY(double blockSeconds READ blockSeconds NOTIFY streamChanged)

    // --- Results -----------------------------------------------------------------------------
    // Meters in dBFS (full scale = ±2 V): [inL, inR, outL, outR] peak / RMS / held peak.
    Q_PROPERTY(QVariantList meters READ meters NOTIFY metersChanged)
    Q_PROPERTY(bool clipping READ clipping NOTIFY metersChanged)   // output clipped in the last second
    Q_PROPERTY(int dropouts READ dropouts NOTIFY metersChanged)    // stream restarts after under/overflows
    Q_PROPERTY(mybench::plot::TraceBuffer* spectrumIn READ spectrumIn CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* spectrumOut READ spectrumOut CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* timeIn READ timeIn CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* timeOut READ timeOut CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* response READ response CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* responseBass READ responseBass CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* responseMid READ responseMid CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* responseTreble READ responseTreble CONSTANT)

public:
    enum Source { SourceAI0, SourceAI1, SourceAudioInLeft, SourceAudioInRight, SourceNone };
    Q_ENUM(Source)
    enum Sink { SinkAO0, SinkAO1, SinkAudioOutLeft, SinkAudioOutRight, SinkNone };
    Q_ENUM(Sink)
    enum View { SpectrumView, TimeView };
    Q_ENUM(View)

    AeqController();
    ~AeqController() override;

    static constexpr double kFullScale = 2.0;   // ±2 V for audio and for AI/AO in this instrument

    Source inputLeft() const { return m_inputLeft; }
    void setInputLeft(Source source);
    Source inputRight() const { return m_inputRight; }
    void setInputRight(Source source);
    Sink outputLeft() const { return m_outputLeft; }
    void setOutputLeft(Sink sink);
    Sink outputRight() const { return m_outputRight; }
    void setOutputRight(Sink sink);
    double bass() const { return m_eq.gainsDb[aeq::Bass]; }
    void setBass(double db) { setGain(aeq::Bass, db); }
    double midtone() const { return m_eq.gainsDb[aeq::Mid]; }
    void setMidtone(double db) { setGain(aeq::Mid, db); }
    double treble() const { return m_eq.gainsDb[aeq::Treble]; }
    void setTreble(double db) { setGain(aeq::Treble, db); }
    double volume() const { return m_eq.volume; }
    void setVolume(double volume);
    bool bypass() const { return m_eq.bypass; }
    void setBypass(bool bypass);
    int rateIndex() const { return m_rateIndex; }
    void setRateIndex(int index);
    int latencyIndex() const { return m_latencyIndex; }
    void setLatencyIndex(int index);
    View view() const { return m_view; }
    void setView(View view);
    int displayChannel() const { return m_displayChannel; }
    void setDisplayChannel(int channel);
    int averagingIndex() const { return m_averagingIndex; }
    void setAveragingIndex(int index);
    bool showInput() const { return m_showInput; }
    int timeSpanIndex() const { return m_timeSpanIndex; }
    void setTimeSpanIndex(int index);
    bool autoScaleTime() const { return m_autoScaleTime; }
    void setAutoScaleTime(bool on);
    bool autoScaleSpectrum() const { return m_autoScaleSpectrum; }
    void setAutoScaleSpectrum(bool on);
    QStringList timeSpanNames() const;
    double timeSpan() const;
    void setShowInput(bool show);

    QStringList sourceNames() const;
    QStringList sinkNames() const;
    QStringList rateNames() const;
    QStringList latencyNames() const;
    QStringList averagingNames() const;
    QString routingError() const;
    bool feedbackInSimulator() const;
    double streamRate() const;
    double latencySeconds() const;
    double blockSeconds() const;

    QVariantList meters() const { return m_meters; }
    bool clipping() const { return m_clipping; }
    int dropouts() const { return m_dropouts; }
    mybench::plot::TraceBuffer* spectrumIn() { return &m_spectrumIn; }
    mybench::plot::TraceBuffer* spectrumOut() { return &m_spectrumOut; }
    mybench::plot::TraceBuffer* timeIn() { return &m_timeIn; }
    mybench::plot::TraceBuffer* timeOut() { return &m_timeOut; }
    mybench::plot::TraceBuffer* response() { return &m_response; }
    mybench::plot::TraceBuffer* responseBass() { return &m_responseBass; }
    mybench::plot::TraceBuffer* responseMid() { return &m_responseMid; }
    mybench::plot::TraceBuffer* responseTreble() { return &m_responseTreble; }

    // All three bands to 0 dB.
    Q_INVOKABLE void flat();
    // Simulator convenience: AI 0 / AI 1 inputs (their built-in test signals) instead of the
    // audio inputs, which the simulator wires to the audio outputs.
    // UI selection of an input / output side (0 = L, 1 = R): the other side follows to the same kind
    // (AI vs audio share the converters), with a notice. The plain setters do not.
    Q_INVOKABLE void chooseInput(int side, int source);
    Q_INVOKABLE void chooseOutput(int side, int sink);
    Q_INVOKABLE void useAnalogInputs();

signals:
    void routingChanged();
    void eqChanged();
    void streamChanged();
    void displayChanged();
    void metersChanged();

protected:
    WorkerFunction createWorker() override;
    void onFinished() override;
    DataTable exportTable() const override;

private:
    struct Display {
        int channel;
        double timeConstant;
        double timeSpan;
    };
    Display display() const;
    void setGain(aeq::Band band, double db);
    void updateResponse();
    void postLive();
    void resetMeters();

    Source m_inputLeft = SourceAudioInLeft;
    Source m_inputRight = SourceAudioInRight;
    Sink m_outputLeft = SinkAudioOutLeft;
    Sink m_outputRight = SinkAudioOutRight;
    aeq::EqSettings m_eq;
    int m_rateIndex = 0;      // 50 kS/s (ELVISmx)
    int m_latencyIndex = 2;   // 250 ms (ELVISmx)
    View m_view = SpectrumView;
    int m_displayChannel = 0;
    int m_averagingIndex = 1;   // 125 ms
    bool m_showInput = true;
    int m_timeSpanIndex = 1;   // 100 ms
    bool m_autoScaleTime = true;
    bool m_autoScaleSpectrum = true;

    QVariantList m_meters;
    bool m_clipping = false;
    int m_dropouts = 0;
    std::shared_ptr<Mailbox<aeq::EqSettings>> m_liveEq = std::make_shared<Mailbox<aeq::EqSettings>>();
    std::shared_ptr<Mailbox<Display>> m_liveDisplay = std::make_shared<Mailbox<Display>>();
    mybench::plot::TraceBuffer m_spectrumIn;
    mybench::plot::TraceBuffer m_spectrumOut;
    mybench::plot::TraceBuffer m_timeIn;
    mybench::plot::TraceBuffer m_timeOut;
    mybench::plot::TraceBuffer m_response;
    mybench::plot::TraceBuffer m_responseBass;
    mybench::plot::TraceBuffer m_responseMid;
    mybench::plot::TraceBuffer m_responseTreble;
};

void registerAeq(InstrumentRegistry& registry);

}  // namespace mybench::instruments
