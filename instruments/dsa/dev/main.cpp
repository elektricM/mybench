// mybench-dsa: the Spectrum Analyzer panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "dsacontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("dsa"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerDsa(registry);
        },
    });
}
