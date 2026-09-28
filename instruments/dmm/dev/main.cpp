// mybench-dmm: the Multimeter panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "dmmcontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("dmm"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerDmm(registry);
        },
    });
}
