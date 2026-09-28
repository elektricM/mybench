// mybench-dio: the Digital I/O panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "diocontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("dio"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerDio(registry);
        },
    });
}
