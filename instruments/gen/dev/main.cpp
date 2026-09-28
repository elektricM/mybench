// mybench-gen: the Waveform Generator panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "gencontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("gen"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerGen(registry);
        },
    });
}
