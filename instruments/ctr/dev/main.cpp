// mybench-ctr: the Counter/Timer panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "ctrcontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("ctr"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerCtr(registry);
        },
    });
}
