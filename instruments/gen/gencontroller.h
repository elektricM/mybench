// GenController: the Waveform Generator (docs/instruments/gen.md) — FGEN + ARB + Waveform Editor
// merged: AO 0 and AO 1 each generate a function (sine/square/triangle, sweep) or an arbitrary
// waveform, together in one AO task. SKELETON — to be completed.
#pragma once

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>

#include <QtQml/qqmlregistration.h>

#include <array>

namespace mybench::instruments {

class GenController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // Settings: AO 0
    Q_PROPERTY(bool ch0Enabled READ ch0Enabled WRITE setCh0Enabled NOTIFY channelsChanged)
    Q_PROPERTY(Mode ch0Mode READ ch0Mode WRITE setCh0Mode NOTIFY channelsChanged)
    Q_PROPERTY(Waveform ch0Waveform READ ch0Waveform WRITE setCh0Waveform NOTIFY channelsChanged)
    Q_PROPERTY(double ch0Frequency READ ch0Frequency WRITE setCh0Frequency NOTIFY channelsChanged)
    Q_PROPERTY(double ch0Amplitude READ ch0Amplitude WRITE setCh0Amplitude NOTIFY channelsChanged)
    Q_PROPERTY(double ch0Offset READ ch0Offset WRITE setCh0Offset NOTIFY channelsChanged)
    // Settings: AO 1
    Q_PROPERTY(bool ch1Enabled READ ch1Enabled WRITE setCh1Enabled NOTIFY channelsChanged)
    Q_PROPERTY(Waveform ch1Waveform READ ch1Waveform WRITE setCh1Waveform NOTIFY channelsChanged)
    Q_PROPERTY(double ch1Frequency READ ch1Frequency WRITE setCh1Frequency NOTIFY channelsChanged)
    Q_PROPERTY(double ch1Amplitude READ ch1Amplitude WRITE setCh1Amplitude NOTIFY channelsChanged)
    // Results
    Q_PROPERTY(QString outputSummary READ outputSummary NOTIFY summaryChanged)
    // Preview of two periods of each output (x in s, y in V), updated when the settings change.
    Q_PROPERTY(mybench::plot::TraceBuffer* preview0 READ preview0 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* preview1 READ preview1 CONSTANT)

public:
    enum Mode { Function, Arbitrary };
    Q_ENUM(Mode)
    enum Waveform { Sine, Square, Triangle };
    Q_ENUM(Waveform)

    GenController();
    ~GenController() override;

    bool ch0Enabled() const { return m_ch[0].enabled; }
    void setCh0Enabled(bool v) { set(m_ch[0].enabled, v); }
    Mode ch0Mode() const { return m_ch0Mode; }
    void setCh0Mode(Mode v) { set(m_ch0Mode, v); }
    Waveform ch0Waveform() const { return m_ch[0].waveform; }
    void setCh0Waveform(Waveform v) { set(m_ch[0].waveform, v); }
    double ch0Frequency() const { return m_ch[0].frequency; }
    void setCh0Frequency(double v) { set(m_ch[0].frequency, v); }
    double ch0Amplitude() const { return m_ch[0].amplitude; }
    void setCh0Amplitude(double v) { set(m_ch[0].amplitude, v); }
    double ch0Offset() const { return m_ch[0].offset; }
    void setCh0Offset(double v) { set(m_ch[0].offset, v); }
    bool ch1Enabled() const { return m_ch[1].enabled; }
    void setCh1Enabled(bool v) { set(m_ch[1].enabled, v); }
    Waveform ch1Waveform() const { return m_ch[1].waveform; }
    void setCh1Waveform(Waveform v) { set(m_ch[1].waveform, v); }
    double ch1Frequency() const { return m_ch[1].frequency; }
    void setCh1Frequency(double v) { set(m_ch[1].frequency, v); }
    double ch1Amplitude() const { return m_ch[1].amplitude; }
    void setCh1Amplitude(double v) { set(m_ch[1].amplitude, v); }

    QString outputSummary() const;
    mybench::plot::TraceBuffer* preview0() { return &m_preview[0]; }
    mybench::plot::TraceBuffer* preview1() { return &m_preview[1]; }

signals:
    // One NOTIFY for all settings keeps the skeleton short; the gen agent may split it.
    void channelsChanged();
    void summaryChanged();

protected:
    WorkerFunction createWorker() override;
    void onStarted() override { emit summaryChanged(); }
    void onFinished() override { emit summaryChanged(); }

private:
    struct Channel {
        bool enabled = true;
        Waveform waveform = Sine;
        double frequency = 1000;   // Hz
        double amplitude = 2.0;    // Vpp
        double offset = 0;         // V
    };
    template <typename T>
    void set(T& member, T value)
    {
        if (member == value)
            return;
        member = value;
        emit channelsChanged();
        emit summaryChanged();
        updatePreview();
        restart();   // regenerated buffer: new task with the new waveform (no-op when idle)
    }

    void updatePreview();

    std::array<Channel, 2> m_ch{Channel{}, Channel{false, Square, 100, 1.0, 0}};
    Mode m_ch0Mode = Function;
    std::array<mybench::plot::TraceBuffer, 2> m_preview;
};

void registerGen(InstrumentRegistry& registry);

}  // namespace mybench::instruments
