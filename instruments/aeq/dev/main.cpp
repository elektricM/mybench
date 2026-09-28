// mybench-aeq: the Audio Equalizer panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "aeqcontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("aeq"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerAeq(registry);
        },
    });
}
