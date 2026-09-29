#include "mybench/shell.h"

#include "mybench/appinfo.h"
#include "mybench/devicehub.h"
#include "mybench/instrumentcontroller.h"
#include "mybench/instrumentregistry.h"
#include "mybench/sessionmanager.h"

#include <mybench/offscreen.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <memory>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

// Settings of --fresh runs live in a temporary folder for the lifetime of the process. For
// screenshots of a prepared state, MYBENCH_SETTINGS_TEMPLATE may name an INI file copied into it.
void useTemporarySettings()
{
    static QTemporaryDir* const folder = new QTemporaryDir;   // leaked on purpose: used until exit
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, folder->path());
    const QString seed = qEnvironmentVariable("MYBENCH_SETTINGS_TEMPLATE");
    if (!seed.isEmpty()) {
        const QString target = QSettings().fileName();
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(seed, target))
            qWarning("MYBENCH_SETTINGS_TEMPLATE: cannot copy %s", qPrintable(seed));
    }
}

void startRequested(const QString& list, const QString& fallback, bool once = false)
{
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    QStringList ids;
    if (list == u"all"_s)
        ids = registry->ids();
    else if (!list.isEmpty())
        ids = list.split(u',', Qt::SkipEmptyParts);
    else if (!fallback.isEmpty())
        ids = {fallback};
    for (const QString& id : std::as_const(ids)) {
        if (InstrumentController* controller = registry->controller(id.trimmed()))
            once ? controller->runOnce() : controller->run();
    }
}

const char* const kShellHelp =
    "MYBENCH options:\n"
    "  --panel <id>             open and focus this instrument (repeatable; alias --instrument)\n"
    "  --split <id>             show <id> in a second pane beside the panel\n"
    "  --run <ids|all>          start these instruments after launch (--run alone: the panel)\n"
    "  --single=<ids|all>       one Single acquisition of these instruments after launch\n"
    "  --device <id>            use this device (sim:0, usb:036CD1C6); --simulated = --device sim:0\n"
    "                           (--simulated and --screenshot do not open USB devices unless --device usb:...)\n"
    "  --open <file.mybench>    open a session (also a positional file argument)\n"
    "  --page home              show Home\n"
    "  --dashboard              pin the dashboard column\n"
    "  --show-help              open the help drawer\n"
    "  --palette, --log         open the command palette / the event log\n"
    "  --preferences            open the Preferences dialog\n"
    "  --system-palette <w,b,t,a>  play a platform palette (window, base, text, accent colors) for the\n"
    "                           system-follow theme, e.g. \"#31363b,#1b1e20,#fcfcfc,#3daee9\" (KDE Breeze dark)\n"
    "  --print-dialog           open the Print / export options of the focused panel\n"
    "  --tile                   dev executables: show the dashboard tiles instead of the panel\n"
    "  --fresh                  temporary empty settings (implied by --screenshot unless --keep-settings)\n"
    "  --no-animations          disable animations\n"
    "  --unified-titlebar       macOS: content under a transparent title bar (experimental)\n"
    "  --version                print the version\n";

}  // namespace

int runShell(int argc, char* argv[], const ShellConfig& config)
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        if (argument == "--version") {
            std::printf("%s %s\n", MYBENCH_APP_NAME, MYBENCH_VERSION);
            return 0;
        }
        if (argument == "--help" || argument == "-h") {
            std::printf("%s", kShellHelp);
            std::printf("\n%s", qPrintable(launchOptionsHelp()));
            return 0;
        }
    }

    QmlApp app;
    app.module = config.module;
    app.type = config.type;
    app.applicationName = QStringLiteral(MYBENCH_APP_NAME);
    app.organizationName = QStringLiteral(MYBENCH_APP_NAME);
    app.style = u"MyBench.Style"_s;
    app.defaultSize = config.defaultSize;
    app.setup = [config](QQmlEngine& engine, const LaunchOptions& options, QVariantMap&) {
        QCoreApplication::setApplicationVersion(QStringLiteral(MYBENCH_VERSION));
        // One pixel size on every platform (points map to different pixel sizes per DPI and
        // platform plugin); the family stays the platform UI font.
        QFont font = QGuiApplication::font();
        font.setPixelSize(13);
        QGuiApplication::setFont(font);
        const QIcon icon(u":/qt/qml/MyBench/Controls/icons/app-icon-color.svg"_s);
        if (!icon.isNull())
            QGuiApplication::setWindowIcon(icon);

        AppInfo* info = AppInfo::instance();
        info->setLaunch(options.arguments, options.screenshot(), options.theme);
        const bool fresh = info->flag(u"fresh"_s) || (options.screenshot() && !info->flag(u"keep-settings"_s));
        if (fresh)
            useTemporarySettings();
        // Preference "Restore last session on start" (default on).
        InstrumentController::setRestoreSavedSettings(QSettings().value(u"preferences/restoreSession"_s, true).toBool());

        InstrumentRegistry* registry = InstrumentRegistry::instance();
        if (config.registerInstruments)
            config.registerInstruments(*registry);

        // Positional arguments: an instrument id or a session file.
        for (const QString& argument : info->arguments()) {
            if (info->instrument().isEmpty() && registry->contains(argument))
                info->setValue(u"instrument"_s, argument);
            else if (info->value(u"open"_s).isEmpty() && argument.endsWith(u".mybench"_s))
                info->setValue(u"open"_s, argument);
        }
        if (info->instrument().isEmpty() && !config.instrumentId.isEmpty())
            info->setValue(u"instrument"_s, config.instrumentId);

        // Screenshots and --simulated never touch real hardware (deterministic, and a myDAQ may be
        // in use by another program), unless a USB device is asked for explicitly.
        const bool wantsUsb = info->value(u"device"_s).startsWith(u"usb"_s);
        if ((options.screenshot() || info->flag(u"simulated"_s)) && !wantsUsb && !qEnvironmentVariableIsSet("MYBENCH_NO_USB"))
            qputenv("MYBENCH_NO_USB", "1");
        DeviceHub* hub = DeviceHub::instance();
        const QString device = info->flag(u"simulated"_s) ? u"sim:0"_s : info->value(u"device"_s);
        if (!device.isEmpty())
            hub->select(device);
        registry->createAll();
        SessionManager::instance();

        const QString session = info->value(u"open"_s);
        if (!session.isEmpty())
            SessionManager::instance()->load(QUrl::fromLocalFile(QFileInfo(session).absoluteFilePath()));

        if (info->flag(u"run"_s) || !info->value(u"run"_s).isEmpty()) {
            QString fallback = info->instrument();
            startRequested(info->value(u"run"_s), fallback);
            const QString split = info->value(u"split"_s);
            if (info->flag(u"run"_s) && !split.isEmpty())
                startRequested(split, {});
        }

        if (info->flag(u"single"_s) || !info->value(u"single"_s).isEmpty())
            startRequested(info->value(u"single"_s), info->instrument(), true);

        // Stop and join every worker before the device and the application go away.
        auto shutdown = [] { InstrumentRegistry::instance()->shutdown(); };
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, shutdown);
        QObject::connect(&engine, &QObject::destroyed, shutdown);
    };
    return runQmlApp(argc, argv, app);
}

}  // namespace mybench
