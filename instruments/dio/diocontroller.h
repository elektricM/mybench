// DioController: Digital I/O (docs/instruments/dio.md) — ELVISmx Digital Reader + Digital
// Writer merged: per-line direction, line LEDs, manual switches, patterns, a timeline of line
// states. SKELETON — to be completed.
#pragma once

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/logicbuffer.h>

#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

namespace mybench::instruments {

class DioController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // Settings: bit i = DIO i.
    Q_PROPERTY(int outputMask READ outputMask WRITE setOutputMask NOTIFY outputMaskChanged)     // 1 = output
    Q_PROPERTY(int outputValue READ outputValue WRITE setOutputValue NOTIFY outputValueChanged) // written levels
    // Results
    Q_PROPERTY(int lineStates READ lineStates NOTIFY lineStatesChanged)                         // read levels
    // Timeline of the 8 lines (x = seconds since Run, last 30 s), written by the worker.
    Q_PROPERTY(mybench::plot::LogicBuffer* timeline READ timeline CONSTANT)

public:
    DioController();
    ~DioController() override;

    int outputMask() const { return m_outputMask; }
    void setOutputMask(int mask);
    int outputValue() const { return m_outputValue; }
    void setOutputValue(int value);
    int lineStates() const { return m_lineStates; }
    mybench::plot::LogicBuffer* timeline() { return &m_timeline; }

    Q_INVOKABLE bool isOutput(int line) const { return (m_outputMask >> line) & 1; }
    Q_INVOKABLE bool lineLevel(int line) const { return (m_lineStates >> line) & 1; }
    Q_INVOKABLE void toggleLine(int line) { setOutputValue(m_outputValue ^ (1 << line)); }
    Q_INVOKABLE void setLineDirection(int line, bool output);

signals:
    void outputMaskChanged();
    void outputValueChanged();
    void lineStatesChanged();

protected:
    WorkerFunction createWorker() override;

private:
    int m_outputMask = 0x0F;   // DIO 0-3 outputs, 4-7 inputs
    int m_outputValue = 0;
    int m_lineStates = 0;
    // Live values for the running worker (written on the UI thread, read by the worker).
    std::shared_ptr<std::atomic<int>> m_live = std::make_shared<std::atomic<int>>(0);
    mybench::plot::LogicBuffer m_timeline;
};

void registerDio(InstrumentRegistry& registry);

}  // namespace mybench::instruments
