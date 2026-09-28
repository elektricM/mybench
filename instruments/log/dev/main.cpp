// mybench-log: the Data Logger panel alone in a minimal shell (device selector, toasts, help).
// Options: see instruments/README.md ("Dev executable").
#include "logcontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .instrumentId = QStringLiteral("log"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) {
            mybench::instruments::registerLog(registry);
        },
    });
}
