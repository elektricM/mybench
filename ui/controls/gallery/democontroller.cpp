#include "democontroller.h"

#include <mybench/instrumentregistry.h>

using namespace Qt::StringLiterals;

namespace mybench::gallery {

DemoController::DemoController() : InstrumentController(u"demo"_s)
{
    setRequiredResources(ResourceArbiter::Multimeter);
}

DemoController::~DemoController()
{
    shutdown();   // the worker captures `this`
}

void DemoController::setFrequency(double frequency)
{
    if (frequency == m_frequency)
        return;
    m_frequency = frequency;
    emit frequencyChanged();
}

void DemoController::setTimeDivIndex(int index)
{
    if (index == m_timeDivIndex)
        return;
    m_timeDivIndex = index;
    emit timeDivIndexChanged();
}

InstrumentController::WorkerFunction DemoController::createWorker()
{
    return [this](WorkerContext& ctx) {
        const auto ranges = ctx.device().dmmRanges(mydaq::DmmFunction::DcVoltage);
        const double range = ranges.size() > 2 ? ranges[2].max : 20.0;
        while (!ctx.stopRequested()) {
            const mydaq::DmmReading r = ctx.device().dmmRead(mydaq::DmmFunction::DcVoltage, range);
            ctx.addSamples(1);
            ctx.publish([this, r] {
                m_reading = r.value;
                m_overload = r.overload;
                emit readingChanged();
            });
            ctx.setStatus(tr("Measuring DC voltage"));
            ctx.acquisitionDone();
            if (ctx.once())
                break;
        }
    };
}

void registerDemo(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"demo"_s,
        .title = u"Demo Meter"_s,
        .shortTitle = u"Demo"_s,
        .description = u"Gallery demo: DC volts from the simulated DMM"_s,
        .module = u"MyBench.Gallery"_s,
        .icon = u"instrument-dmm"_s,
        .helpId = u"app"_s,
        .createController = [] { return new DemoController; },
    });
}

}  // namespace mybench::gallery
