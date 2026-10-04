// mybench-scopehal-bridge: the NI myDAQ as an oscilloscope in ngscopeclient (README.md).
//
// Speaks the protocol of scopehal's "digilent" driver over the "twinlan" transport, so
// ngscopeclient connects with driver "digilent", transport "twinlan", path host:port:dataport.

#include "bridge_server.h"

#include "mydaq/device_manager.h"
#include "mydaq/sim_device.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <csignal>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace scopehal_bridge;

namespace {

std::atomic<bool> g_quit{false};
void onSignal(int) { g_quit = true; }

constexpr int kDeviceWaitMs = 20000;   // USB: the provider opens a new device after ~2 s settling

bool matches(const mydaq::DevicePtr& device, const QString& serial)
{
    const mydaq::DeviceInfo info = device->info();
    return !info.simulated && (serial.isEmpty() || info.serialNumber.compare(serial, Qt::CaseInsensitive) == 0);
}

mydaq::DevicePtr findDevice(const QString& serial)
{
    for (const mydaq::DevicePtr& device : mydaq::DeviceManager::instance().devices()) {
        if (matches(device, serial))
            return device;
    }
    return nullptr;
}

QString fieldText(QString text)
{
    // *IDN? fields: no separators, and the client reads the version up to the first blank.
    text.replace(u',', u' ').replace(u';', u' ');
    return text.trimmed();
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    QCoreApplication::setApplicationName(u"mybench-scopehal-bridge"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(MYBENCH_BRIDGE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"Serves the NI myDAQ analog inputs AI0/AI1 to ngscopeclient as an oscilloscope.\n"
        u"In ngscopeclient add an oscilloscope with driver \"digilent\", transport \"twinlan\"\n"
        u"and path HOST:PORT:DATAPORT (default localhost:5025:5026)."_s);
    parser.addHelpOption();
    const QCommandLineOption version(u"version"_s, u"Show the version and exit."_s);
    const QCommandLineOption sim(u"sim"_s, u"Use the simulated myDAQ instead of a real one."_s);
    const QCommandLineOption serial(u"serial"_s, u"Use the myDAQ with this serial number."_s, u"S"_s);
    const QCommandLineOption bind(u"bind"_s, u"Address to listen on (default 127.0.0.1; 0.0.0.0 = all)."_s,
                                  u"ADDR"_s, u"127.0.0.1"_s);
    const QCommandLineOption port(u"port"_s, u"Control (SCPI) port, default 5025; 0 = any free port."_s, u"N"_s,
                                  u"5025"_s);
    const QCommandLineOption dataPort(u"data-port"_s, u"Waveform data port, default control port + 1."_s, u"N"_s);
    const QCommandLineOption autoTrigger(
        u"auto-trigger-ms"_s,
        u"In normal (START) mode, send an untriggered frame when no trigger came for N ms (auto mode). "
        u"Default 0: off, frames only on triggers or FORCE."_s,
        u"N"_s, u"0"_s);
    const QCommandLineOption verbose({u"v"_s, u"verbose"_s}, u"Log every command, reply and frame rate."_s);
    parser.addOptions({version, sim, serial, bind, port, dataPort, autoTrigger, verbose});
    parser.process(app);
    if (parser.isSet(version)) {
        std::printf("mybench-scopehal-bridge %s\n", MYBENCH_BRIDGE_VERSION);
        return 0;
    }

    bool ok = false;
    const uint controlPortValue = parser.value(port).toUInt(&ok);
    if (!ok || controlPortValue > 65535) {
        logLine(u"invalid --port %1"_s.arg(parser.value(port)));
        return 1;
    }
    uint dataPortValue = controlPortValue == 0 ? 0 : controlPortValue + 1;
    if (parser.isSet(dataPort)) {
        dataPortValue = parser.value(dataPort).toUInt(&ok);
        if (!ok || dataPortValue > 65535) {
            logLine(u"invalid --data-port %1"_s.arg(parser.value(dataPort)));
            return 1;
        }
    }
    const int autoTriggerMs = parser.value(autoTrigger).toInt(&ok);
    if (!ok || autoTriggerMs < 0) {
        logLine(u"invalid --auto-trigger-ms %1"_s.arg(parser.value(autoTrigger)));
        return 1;
    }
    QHostAddress address;
    const QString bindText = parser.value(bind);
    if (bindText.compare(u"localhost"_s, Qt::CaseInsensitive) == 0)
        address = QHostAddress(QHostAddress::LocalHost);
    else if (!address.setAddress(bindText)) {
        logLine(u"invalid --bind address %1"_s.arg(bindText));
        return 1;
    }

    // The device is opened once here and kept; the AI task exists only while the client is armed.
    Acquisition::DeviceSource deviceSource;
    Identity identity;
    identity.vendor = u"National Instruments"_s;
    identity.version = u"MYBENCH-"_s + QCoreApplication::applicationVersion();
    if (parser.isSet(sim)) {
        // Never through DeviceManager: its USB provider would open any myDAQ on the bus.
        auto device = std::make_shared<mydaq::SimDevice>();
        deviceSource = [device] { return device; };
        identity.model = u"NI myDAQ (simulated)"_s;
        identity.serial = u"SIM0"_s;
        logLine(u"using the simulated myDAQ"_s);
    } else {
        qputenv("MYBENCH_NO_SIM", "1");
        const QString wanted = parser.value(serial);
        logLine(wanted.isEmpty() ? u"waiting for a myDAQ on USB..."_s
                                 : u"waiting for the myDAQ %1 on USB..."_s.arg(wanted));
        QElapsedTimer waited;
        waited.start();
        mydaq::DevicePtr device;
        while (!(device = findDevice(wanted)) && waited.elapsed() < kDeviceWaitMs && !g_quit) {
            mydaq::DeviceManager::instance().rescan();
            QThread::msleep(200);
        }
        if (!device) {
            QStringList seen;
            for (const mydaq::DevicePtr& d : mydaq::DeviceManager::instance().devices())
                seen << d->info().name;
            logLine(u"no myDAQ found%1 (is it plugged in, and not in use by MYBENCH, NI software or another "
                    u"program?)"_s.arg(seen.isEmpty() ? QString() : u"; found: "_s + seen.join(u", "_s)));
            return 2;
        }
        const mydaq::DeviceInfo info = device->info();
        identity.model = fieldText(info.productType.isEmpty() ? u"NI myDAQ"_s : info.productType);
        identity.serial = fieldText(info.serialNumber);
        if (identity.serial.isEmpty())
            identity.serial = u"UNKNOWN"_s;
        logLine(u"using %1"_s.arg(info.name));
        // Re-resolved at every arm, so a re-plugged device is picked up again.
        deviceSource = [wanted] { return findDevice(wanted); };
    }
    identity.version = fieldText(identity.version).replace(u' ', u'-');

    Acquisition::Options options;
    options.autoTriggerMs = autoTriggerMs;
    options.verbose = parser.isSet(verbose);
    BridgeServer server(deviceSource, identity, options, parser.isSet(verbose));
    QString error;
    if (!server.listen(address, quint16(controlPortValue), quint16(dataPortValue), &error)) {
        logLine(u"cannot listen: %1"_s.arg(error));
        return 1;
    }
    // One machine-readable line on stdout (tests parse it): the ngscopeclient path.
    std::printf("listening on %s:%u:%u\n", address.toString().toUtf8().constData(), unsigned(server.controlPort()),
                unsigned(server.dataPort()));
    std::fflush(stdout);
    logLine(u"ngscopeclient: add an oscilloscope with driver \"digilent\", transport \"twinlan\", path %1:%2:%3"_s
                .arg(address.toString())
                .arg(server.controlPort())
                .arg(server.dataPort()));

    QTimer quitPoll;
    QObject::connect(&quitPoll, &QTimer::timeout, &app, [] {
        if (g_quit)
            QCoreApplication::quit();
    });
    quitPoll.start(100);

    const int rc = app.exec();
    logLine(u"stopped"_s);
    return rc;
}
