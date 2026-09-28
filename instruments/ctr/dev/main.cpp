// mybench-ctr: the Counter/Timer panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "ctrcontroller.h"

#include <mybench/devicehub.h>
#include <mybench/shell.h>

#include <QString>
#include <QStringList>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("ctr"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerCtr(registry);
            // Preview of a limited backend: MYBENCH_SIM_UNSUPPORTED="counter.pulseWidth,counter.fallingEdge,..."
            // greys those options (device.h `mydaq::feature` names).
            if (const QString list = qEnvironmentVariable("MYBENCH_SIM_UNSUPPORTED"); !list.isEmpty())
                mybench::DeviceHub::instance()->setSimulatorUnsupportedFeatures(list.split(u','));
        },
    });
}
