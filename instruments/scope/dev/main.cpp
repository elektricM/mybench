// mybench-scope: the Oscilloscope panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "scopecontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("scope"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerScope(registry);
        },
    });
}
