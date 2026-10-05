// Bootstrap shared by every MYBENCH executable (the app, the dev executables, the gallery).
//
//     int main(int argc, char* argv[])
//     {
//         return mybench::runShell(argc, argv, {
//             .instrumentId = QStringLiteral("scope"),
//             .registerInstruments = [](mybench::InstrumentRegistry& r) {
//                 mybench::instruments::registerScope(r);
//             },
//         });
//     }
//
// runShell() sets the application identity (MYBENCH_APP_NAME), the MyBench.Style Controls style
// and the window icon, points QSettings at a temporary empty store for --fresh (implied by
// --screenshot unless --keep-settings, so screenshots are deterministic), registers the
// instruments, creates their controllers, passes the launch options to AppInfo, loads the root
// (MyBench.Controls/DevShell by default) through mybench::runQmlApp() — hence --screenshot,
// --delay, --size, --theme, --dpr work everywhere — and shuts the controllers down at exit.
#pragma once

#include <QSize>
#include <QString>

#include <functional>

namespace mybench {

class InstrumentRegistry;

struct ShellConfig {
    // Root QML type (a Window/ApplicationWindow or an Item).
    QString module = QStringLiteral("MyBench.Controls");
    QString type = QStringLiteral("DevShell");
    // Instrument of a dev executable (the DevShell's `instrumentId`; --instrument overrides).
    QString instrumentId;
    // Registers the instruments built into the executable (called before QML loads).
    std::function<void(InstrumentRegistry&)> registerInstruments;
    QSize defaultSize{1280, 800};
};

int runShell(int argc, char* argv[], const ShellConfig& config);

// The controls style a normal start uses: "mybench" when the preference Appearance ▸ Controls
// (QSettings key `appearance/controls`, written by AppPreferences through QtCore's Settings) is
// "mybench", else "native". Reads QSettings() of the current organization/application names, i.e.
// the same store as QML Settings without a `location`.
QString storedControlsStyle();

}  // namespace mybench
