// DsaController: the Spectrum Analyzer (docs/instruments/dsa.md, ELVISmx "Dynamic Signal
// Analyzer"). SKELETON — to be completed.
//
// Acquires one AI channel and computes a windowed power spectrum; the dsa agent adds averaging,
// units/modes, triggering, cursors, harmonic markers, spectrogram/waterfall and the plots.
#pragma once

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>

#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace mybench::instruments {

class DsaController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // Settings
    Q_PROPERTY(int sourceChannel READ sourceChannel WRITE setSourceChannel NOTIFY sourceChannelChanged)
    Q_PROPERTY(double frequencySpan READ frequencySpan WRITE setFrequencySpan NOTIFY frequencySpanChanged)
    Q_PROPERTY(int resolutionIndex READ resolutionIndex WRITE setResolutionIndex NOTIFY resolutionIndexChanged)
    Q_PROPERTY(int windowIndex READ windowIndex WRITE setWindowIndex NOTIFY windowIndexChanged)
    // Lists and results
    Q_PROPERTY(QStringList resolutionLabels READ resolutionLabels CONSTANT)
    Q_PROPERTY(QStringList windowLabels READ windowLabels CONSTANT)
    Q_PROPERTY(double fundamentalFrequency READ fundamentalFrequency NOTIFY resultsChanged)
    Q_PROPERTY(double fundamentalPower READ fundamentalPower NOTIFY resultsChanged)
    // Traces: magnitude spectrum (x Hz, y dBVrms) and the time record (x s, y V).
    Q_PROPERTY(mybench::plot::TraceBuffer* spectrum READ spectrum CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* timeSignal READ timeSignal CONSTANT)

public:
    DsaController();
    ~DsaController() override;

    int sourceChannel() const { return m_source; }
    void setSourceChannel(int channel);
    double frequencySpan() const { return m_span; }
    void setFrequencySpan(double hertz);
    int resolutionIndex() const { return m_resolutionIndex; }
    void setResolutionIndex(int index);
    int windowIndex() const { return m_windowIndex; }
    void setWindowIndex(int index);

    QStringList resolutionLabels() const;
    QStringList windowLabels() const;
    double fundamentalFrequency() const { return m_fundamental; }
    double fundamentalPower() const { return m_power; }
    mybench::plot::TraceBuffer* spectrum() { return &m_spectrumTrace; }
    mybench::plot::TraceBuffer* timeSignal() { return &m_timeTrace; }

signals:
    void sourceChannelChanged();
    void frequencySpanChanged();
    void resolutionIndexChanged();
    void windowIndexChanged();
    void resultsChanged();

protected:
    WorkerFunction createWorker() override;
    DataTable exportTable() const override;

private:
    int m_source = 0;             // 0 = AI 0, 1 = AI 1
    double m_span = 40000;        // Hz
    int m_resolutionIndex = 2;    // 400 lines
    int m_windowIndex = 0;        // None
    double m_fundamental = 0;
    double m_power = 0;
    std::vector<double> m_frequencies;
    std::vector<double> m_spectrum;
    mybench::plot::TraceBuffer m_spectrumTrace;
    mybench::plot::TraceBuffer m_timeTrace;
};

void registerDsa(InstrumentRegistry& registry);

}  // namespace mybench::instruments
