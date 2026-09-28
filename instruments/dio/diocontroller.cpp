#include "diocontroller.h"

#include <chrono>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

DioController::DioController() : InstrumentController(u"dio"_s)
{
    setRequiredResources(ResourceArbiter::DigitalLines, 0xFF);
    m_timeline.setLaneCount(8);
    m_timeline.setMaximumSpan(30.0);
}

DioController::~DioController()
{
    shutdown();
}

void DioController::setOutputMask(int mask)
{
    mask &= 0xFF;
    if (mask == m_outputMask)
        return;
    m_outputMask = mask;
    emit outputMaskChanged();
    restart();   // directions are set when the run starts
}

void DioController::setOutputValue(int value)
{
    value &= 0xFF;
    if (value == m_outputValue)
        return;
    m_outputValue = value;
    m_live->store(value);   // picked up by the running worker (live change)
    emit outputValueChanged();
}

void DioController::setLineDirection(int line, bool output)
{
    setOutputMask(output ? (m_outputMask | (1 << line)) : (m_outputMask & ~(1 << line)));
}

InstrumentController::WorkerFunction DioController::createWorker()
{
    const uint8_t mask = uint8_t(m_outputMask);
    m_live->store(m_outputValue);

    // TODO(dio agent): line groups (0-3, 4-7, 0-7), patterns (ramp, alternating, walking 1s),
    // toggle/rotate/shift, timeline of line states (docs/instruments/dio.md).
    m_timeline.clear();
    return [this, mask, live = m_live](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        const auto start = std::chrono::steady_clock::now();
        device.dioSetDirection(uint8_t(~mask), false);
        device.dioSetDirection(mask, true);
        ctx.setStatus(tr("Lines 0–7"));
        int written = -1;
        do {
            const int value = live->load();
            if (value != written) {
                device.dioWrite(uint8_t(value), mask);
                written = value;
            }
            const int states = device.dioRead();
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            m_timeline.append(t, quint64(states));   // LogicBuffer is thread-safe
            m_timeline.setEnd(t);
            ctx.addSamples(1);
            ctx.publish([this, states] {
                if (states != m_lineStates) {
                    m_lineStates = states;
                    emit lineStatesChanged();
                }
            });
            ctx.acquisitionDone();
        } while (!ctx.once() && ctx.sleepMs(50));
        device.dioSetDirection(0xFF, false);   // safe state: every line back to input
    };
}

void registerDio(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"dio"_s,
        .title = QObject::tr("Digital I/O"),
        .shortTitle = QObject::tr("DIO"),
        .description = QObject::tr("Read and write DIO 0–7, patterns, timeline"),
        .module = u"MyBench.Instruments.Dio"_s,
        .icon = u"instrument-dio"_s,
        .order = 60,
        .createController = [] { return new DioController; },
    });
}

}  // namespace mybench::instruments
