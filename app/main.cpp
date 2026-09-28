// mybench: the MYBENCH application shell (app/Main.qml) with every built instrument.
#include <mybench/instrumentregistry.h>
#include <mybench/shell.h>

void registerBuiltInstruments(mybench::InstrumentRegistry& registry);   // generated instruments.cpp

int main(int argc, char* argv[])
{
    return mybench::runShell(argc, argv, {
        .module = QStringLiteral("MyBench.App"),
        .type = QStringLiteral("Main"),
        .registerInstruments = registerBuiltInstruments,
        .defaultSize = {1440, 900},
    });
}
