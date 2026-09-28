// mybench-controls-gallery: every MyBench.Controls component on one page.
#include "democontroller.h"

#include <mybench/shell.h>

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .module = QStringLiteral("MyBench.Gallery"),
        .type = QStringLiteral("Gallery"),
        .instrumentId = QStringLiteral("demo"),
        .registerInstruments = [](mybench::InstrumentRegistry& registry) { mybench::gallery::registerDemo(registry); },
        .defaultSize = {1440, 1000},
    });
}
