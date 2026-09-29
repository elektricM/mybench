// Hot-plug on the real myDAQ, without touching the cable: while this test holds the device
// natively, it hands the myDAQ to the NI VM (qemu monitor device_add: macOS sees it leave the bus)
// and takes it back (device_del: it comes back). Runs only with MYDAQ_HW_HOTPLUG=vm (needs the
// VM running and tools/devlock.sh's mydaq-vm script; MYDAQ_VM overrides its path).
// Facts: DeviceHub reports Disconnected within a few seconds, a running Scope stops with
// "disconnected" (no crash, no hang), the device is listed again after it returns, and the
// Scope runs again.
#include "hwfixture.h"
#include "scopecontroller.h"

#include <QDir>
#include <QProcess>

using namespace Qt::StringLiterals;
using mybench::DeviceHub;
using mybench::instruments::ScopeController;

namespace {

bool vmMonitor(const QString& command)
{
    const QString vm = qEnvironmentVariable("MYDAQ_VM", QDir::homePath() + u"/projects/mydaq-mac/scripts/mydaq-vm"_s);
    QProcess p;
    p.start(vm, {u"mon"_s, command});
    return p.waitForFinished(20000) && p.exitCode() == 0;
}

}  // namespace

class TestHwHotplug : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("MYDAQ_HW_HOTPLUG") != u"vm"_s)
            QSKIP("set MYDAQ_HW_HOTPLUG=vm to hand the device to the NI VM and back");
        hw::initTestCase();
        QVERIFY(hw::selectDevice());
    }

    void unplugWhileRunning()
    {
        HW_REQUIRE_REAL_DEVICE();
        auto* hub = DeviceHub::instance();
        const QString id = hub->currentId();
        ScopeController scope;
        scope.initialize();
        scope.run();
        QVERIFY(hw::waitFor([&] { return scope.acquisitionCount() >= 3; }, 5000));

        QElapsedTimer clock;
        clock.start();
        QVERIFY(vmMonitor(u"device_add usb-host,bus=xhci.0,vendorid=0x3923,id=mydaq"_s));
        const bool gone = hw::waitFor([&] { return hub->connectionState() == DeviceHub::Disconnected; }, 10000);
        const qint64 goneMs = clock.elapsed();
        const bool stopped = hw::waitFor([&] { return scope.isIdle(); }, 5000);
        hw::report(u"unplug (to the VM)"_s, u"Disconnected after %1 ms, Scope idle %2, interrupted %3, error \"%4\", listed as \"%5\""_s
                                                .arg(goneMs).arg(stopped).arg(scope.interruptedByDisconnect())
                                                .arg(scope.errorText(), hub->name()));

        clock.restart();
        QVERIFY(vmMonitor(u"device_del mydaq"_s));   // back to this Mac
        const bool back = hw::waitFor([&] { return hub->isConnected() && hub->currentId() == id; }, 20000);
        const qint64 backMs = clock.elapsed();
        hw::report(u"replug (from the VM)"_s, u"Connected again after %1 ms (%2)"_s.arg(backMs).arg(back));
        QVERIFY(gone);
        QVERIFY(stopped);
        QVERIFY(scope.interruptedByDisconnect());
        QVERIFY(back);

        // The device settles after the VM had it (-201381 for a few seconds): Run until records come.
        QElapsedTimer settle;
        settle.start();
        int records = 0;
        while (settle.elapsed() < 20000 && records < 3) {
            scope.run();
            const int start = scope.acquisitionCount();
            hw::waitFor([&] { return scope.acquisitionCount() >= start + 3 || scope.isIdle(); }, 5000);
            records = scope.acquisitionCount() - start;
            if (records < 3)
                QTest::qWait(500);
        }
        hw::report(u"Scope after replug"_s, u"%1 records, %2 s after the device returned, error \"%3\""_s
                                               .arg(records).arg(settle.elapsed() / 1000.0, 0, 'f', 1).arg(scope.errorText()));
        QVERIFY(records >= 3);
        hw::stopAndWait(scope);
    }
};

QTEST_GUILESS_MAIN(TestHwHotplug)
#include "tst_hw_hotplug.moc"
