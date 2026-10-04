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
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <functional>
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
        // A view's pane id ("gen/editor") runs its instrument.
        if (InstrumentController* controller = registry->controller(id.trimmed().section(u'/', 0, 0)))
            once ? controller->runOnce() : controller->run();
    }
}

// Runs `start` once the device is usable: at once when the requested device (`requested`, from
// --device; empty: whichever is selected) is connected, or when nothing is requested or selected;
// else when DeviceHub reports it connected (a USB myDAQ is listed and opened ~0.5-4 s after
// launch), or after 15 s anyway (the instrument then reports the missing device as before).
void whenDeviceReady(const QString& requested, std::function<void()> start)
{
    DeviceHub* hub = DeviceHub::instance();
    auto ready = [hub, requested] {
        return hub->isConnected() && (requested.isEmpty() || hub->currentId() == requested);
    };
    if (ready() || (requested.isEmpty() && hub->currentId().isEmpty())) {
        start();
        return;
    }
    auto done = std::make_shared<bool>(false);
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto fire = [done, connection, start] {
        if (*done)
            return;
        *done = true;
        QObject::disconnect(*connection);
        start();
    };
    auto check = [ready, fire] {
        if (ready())
            fire();
    };
    *connection = QObject::connect(hub, &DeviceHub::connectionChanged, hub, check);
    QObject::connect(hub, &DeviceHub::currentChanged, hub, check);
    QTimer::singleShot(15000, hub, fire);
}

const char* const kShellHelp =
    "MYBENCH options:\n"
    "  --panel <id>             show this instrument or view (gen/editor); repeatable, the last one is\n"
    "                           shown (alias --instrument)\n"
    "  --split <id>             show <id> in a second pane beside the panel\n"
    "  --run <ids|all>          start these instruments after launch (--run alone: the panel)\n"
    "  --single=<ids|all>       one Single acquisition of these instruments after launch\n"
    "  --device <id>            use this device (sim:0, usb:036CD1C6); --simulated = --device sim:0\n"
    "                           (--simulated and --screenshot do not open USB devices unless --device usb:...)\n"
    "  --open <file.mybench>    open a session (also a positional file argument)\n"
    "  --page overview          show Overview (alias: --page home)\n"
    "  --sidebar full|compact|hidden    sidebar form for this run\n"
    "  --inspector shown|hidden         inspector for this run\n"
    "  --show-help              show the help page of the inspector\n"
    "  --palette, --log         open the command palette / the event log\n"
    "  --preferences, --about, --device-info, --simulator-settings\n"
    "                           open that window (offscreen: shown inside the main window)\n"
    "  --dark-displays          plots and readouts stay dark in the light theme\n"
    "  --system-palette <w,b,t,a>  play a platform palette (window, base, text, accent colors) for the\n"
    "                           system-follow theme, e.g. \"#31363b,#1b1e20,#fcfcfc,#3daee9\" (KDE Breeze dark)\n"
    "  --print-dialog           open the Print / export options of the focused panel\n"
    "  --tile                   dev executables: show the Overview tile instead of the panel\n"
    "  --fresh                  temporary empty settings (implied by --screenshot unless --keep-settings)\n"
    "  --no-animations          disable animations\n"
    "  --no-unified-titlebar    macOS: keep the system title bar above the toolbar\n"
    "  --style native|mybench|<name>  controls style for this run (default: the preference\n"
    "                           Appearance > Controls, Native; screenshots use Fusion)\n"
    "  --grab <file.png>        show the real window (transparent, no focus) and grab it after --delay;\n"
    "                           never opens USB, fresh settings like --screenshot\n"
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
    // Controls style: "native" (the platform's own style; Fusion offscreen) unless the preference
    // Appearance > Controls is "mybench". Screenshot and fresh runs ignore the stored preference.
    app.style = u"native"_s;
    {
        bool fresh = false;
        for (int i = 1; i < argc; ++i) {
            const QByteArray argument(argv[i]);
            if (argument == "--fresh" || argument.startsWith("--screenshot") || argument.startsWith("--grab"))
                fresh = true;
            else if (argument == "--keep-settings")
                fresh = false;
        }
        if (!fresh) {
            const QSettings settings(QSettings::UserScope, QStringLiteral(MYBENCH_APP_NAME), QStringLiteral(MYBENCH_APP_NAME));
            if (settings.value(u"appearance/controls"_s).toString() == u"mybench"_s)
                app.style = u"mybench"_s;
        }
    }
    app.defaultSize = config.defaultSize;
    app.setup = [config](QQmlEngine& engine, const LaunchOptions& options, QVariantMap&) {
        QCoreApplication::setApplicationVersion(QStringLiteral(MYBENCH_VERSION));
        // The platform UI font at its default size (docs/UI.md §3). Only offscreen rendering
        // (screenshots, tests) forces 13 px and a family that exists, so the images are
        // reproducible.
        if (QGuiApplication::platformName() == u"offscreen"_s || QGuiApplication::platformName() == u"minimal"_s) {
            QFont font = QGuiApplication::font();
            font.setPixelSize(13);
            if (!QFontDatabase::hasFamily(font.family()))
                font.setFamily(QFontInfo(font).family());
            QGuiApplication::setFont(font);
        }
        const QIcon icon(u":/qt/qml/MyBench/Controls/icons/app-icon-color.svg"_s);
        if (!icon.isNull())
            QGuiApplication::setWindowIcon(icon);

        AppInfo* info = AppInfo::instance();
        info->setLaunch(options.arguments, options.screenshot(), options.theme);
        const bool fresh = info->flag(u"fresh"_s) || (options.capture() && !info->flag(u"keep-settings"_s));
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
        if ((options.capture() || info->flag(u"simulated"_s)) && !wantsUsb && !qEnvironmentVariableIsSet("MYBENCH_NO_USB"))
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
            const QString list = info->value(u"run"_s), fallback = info->instrument();
            const QString split = info->flag(u"run"_s) ? info->value(u"split"_s) : QString();
            whenDeviceReady(device, [list, fallback, split] {
                startRequested(list, fallback);
                if (!split.isEmpty())
                    startRequested(split, {});
            });
        }

        if (info->flag(u"single"_s) || !info->value(u"single"_s).isEmpty()) {
            const QString list = info->value(u"single"_s), fallback = info->instrument();
            whenDeviceReady(device, [list, fallback] { startRequested(list, fallback, true); });
        }

        // Stop and join every worker before the device and the application go away.
        auto shutdown = [] { InstrumentRegistry::instance()->shutdown(); };
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, shutdown);
        QObject::connect(&engine, &QObject::destroyed, shutdown);
    };
    return runQmlApp(argc, argv, app);
}

}  // namespace mybench
