// Demo data for mybench-plot-gallery: a worker thread feeds every buffer like an instrument
// controller would (TraceBuffer/LogicBuffer/WaterfallBuffer setters called off the GUI thread).
#pragma once

#include <mybench/plot/logicbuffer.h>
#include <mybench/plot/tracebuffer.h>
#include <mybench/plot/waterfall.h>

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <mutex>
#include <thread>

class GalleryData : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::TraceBuffer *scope0 READ scope0 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *scope1 READ scope1 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *deep0 READ deep0 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *deep1 READ deep1 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *bodeGain READ bodeGain CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *bodePhase READ bodePhase CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *bodeGainRef READ bodeGainRef CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *bodePhaseRef READ bodePhaseRef CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *spectrum READ spectrum CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *spectrumAverage READ spectrumAverage CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *liveSpectrum READ liveSpectrum CONSTANT)
    Q_PROPERTY(mybench::plot::WaterfallBuffer *waterfall READ waterfall CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *trend READ trend CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *xy READ xy CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *recording READ recording CONSTANT)
    Q_PROPERTY(mybench::plot::LogicBuffer *logic READ logic CONSTANT)
    Q_PROPERTY(QVariantList harmonics READ harmonics CONSTANT)
    Q_PROPERTY(double cutoff READ cutoff CONSTANT)
    Q_PROPERTY(double phaseAtCutoff READ phaseAtCutoff CONSTANT)
    Q_PROPERTY(double trendMean READ trendMean NOTIFY statsChanged)
    Q_PROPERTY(double trendSigma READ trendSigma NOTIFY statsChanged)
    Q_PROPERTY(double trendLast READ trendLast NOTIFY statsChanged)
    Q_PROPERTY(double now READ now NOTIFY statsChanged)
    Q_PROPERTY(qint64 recordedSamples READ recordedSamples NOTIFY statsChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int deepSamples READ deepSamples WRITE setDeepSamples NOTIFY deepSamplesChanged)

public:
    explicit GalleryData(QObject *parent = nullptr);
    ~GalleryData() override;

    mybench::plot::TraceBuffer *scope0() { return &m_scope0; }
    mybench::plot::TraceBuffer *scope1() { return &m_scope1; }
    mybench::plot::TraceBuffer *deep0() { return &m_deep0; }
    mybench::plot::TraceBuffer *deep1() { return &m_deep1; }
    mybench::plot::TraceBuffer *bodeGain() { return &m_bodeGain; }
    mybench::plot::TraceBuffer *bodePhase() { return &m_bodePhase; }
    mybench::plot::TraceBuffer *bodeGainRef() { return &m_bodeGainRef; }
    mybench::plot::TraceBuffer *bodePhaseRef() { return &m_bodePhaseRef; }
    mybench::plot::TraceBuffer *spectrum() { return &m_spectrum; }
    mybench::plot::TraceBuffer *spectrumAverage() { return &m_spectrumAverage; }
    mybench::plot::TraceBuffer *liveSpectrum() { return &m_liveSpectrum; }
    mybench::plot::WaterfallBuffer *waterfall() { return &m_waterfall; }
    mybench::plot::TraceBuffer *trend() { return &m_trend; }
    mybench::plot::TraceBuffer *xy() { return &m_xy; }
    mybench::plot::TraceBuffer *recording() { return &m_recording; }
    mybench::plot::LogicBuffer *logic() { return &m_logic; }
    QVariantList harmonics() const { return m_harmonics; }
    double cutoff() const { return m_cutoff; }
    double phaseAtCutoff() const { return m_phaseAtCutoff; }
    double trendMean() const;
    double trendSigma() const;
    double trendLast() const;
    double now() const;
    qint64 recordedSamples() const { return m_recording.count(); }
    bool running() const { return m_running.load(); }
    void setRunning(bool on);
    int deepSamples() const { return m_deepSamples.load(); }
    void setDeepSamples(int n);
    // Pushes a new deep-memory record (2 x deepSamples) right now; for benchmarks.
    Q_INVOKABLE void pushDeepFrame();

signals:
    void statsChanged();
    void runningChanged();
    void deepSamplesChanged();

private:
    void computeStatic();
    void worker();
    void recorder();

    mybench::plot::TraceBuffer m_scope0, m_scope1, m_deep0, m_deep1;
    mybench::plot::TraceBuffer m_bodeGain, m_bodePhase, m_bodeGainRef, m_bodePhaseRef;
    mybench::plot::TraceBuffer m_spectrum, m_spectrumAverage, m_liveSpectrum;
    mybench::plot::WaterfallBuffer m_waterfall;
    mybench::plot::TraceBuffer m_trend, m_xy, m_recording;
    mybench::plot::LogicBuffer m_logic;
    QVariantList m_harmonics;
    double m_cutoff = 0;
    double m_phaseAtCutoff = 0;

    mutable std::mutex m_statsMutex;
    double m_mean = 0, m_sigma = 0, m_last = 0, m_now = 0;

    std::atomic<bool> m_running{true};
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_deepSamples{200000};
    std::vector<double> m_deepA, m_deepB;     // 2 s + 2 M samples of pre-computed signal
    std::atomic<size_t> m_deepFrame{0};
    std::thread m_thread;
    std::thread m_recorderThread;   // a second writer thread: the long recording
};
