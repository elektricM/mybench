#include "mydaq/device_manager.h"

#include "sim/sim_provider.h"
#ifdef MYDAQ_HAVE_USB
#include "usb/usb_provider.h"
#endif

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QtGlobal>

#include <mutex>
#include <vector>

namespace mydaq {

namespace {

// Hot-plug polling, 2x/s like ELVISmx (ui-inventory.md). Cheap: the USB provider's scan() only
// returns the list its own background thread keeps (libusb enumeration without opening devices,
// UsbProvider::Options::pollInterval), and the simulator's scan is a flag check.
constexpr int kRescanIntervalMs = 500;

}  // namespace

struct DeviceManager::Private {
    mutable std::mutex mutex;   // guards providers, devices, deviceIds, timerApp
    std::mutex scanMutex;       // serializes rescans
    std::vector<std::unique_ptr<DeviceProvider>> providers;
    QList<DevicePtr> devices;
    QStringList deviceIds;      // ids of `devices`, same order
    QPointer<QCoreApplication> timerApp;   // application that owns the rescan timer

    void ensureTimer(DeviceManager* manager);
};

// The periodic rescan runs in the application's thread with a timer owned by the application, so
// it is created lazily once a QCoreApplication exists and disappears with it.
void DeviceManager::Private::ensureTimer(DeviceManager* manager)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app)
        return;
    {
        std::lock_guard lock(mutex);
        if (timerApp == app)
            return;
        timerApp = app;
    }
    QMetaObject::invokeMethod(app, [manager, app] {
        auto* timer = new QTimer(app);
        QObject::connect(timer, &QTimer::timeout, manager, &DeviceManager::rescan, Qt::DirectConnection);
        timer->start(kRescanIntervalMs);
    });
}

DeviceManager::DeviceManager() : d(std::make_unique<Private>())
{
#ifdef MYDAQ_HAVE_USB
    d->providers.push_back(makeUsbProvider());
#endif
    if (qEnvironmentVariableIntValue("MYBENCH_NO_SIM") == 0)
        d->providers.push_back(makeSimProvider());
}

DeviceManager& DeviceManager::instance()
{
    // Never destroyed: devices may be in use by worker threads until the process exits.
    static DeviceManager* const manager = [] {
        auto* m = new DeviceManager;
        m->rescan();
        return m;
    }();
    manager->d->ensureTimer(manager);
    return *manager;
}

void DeviceManager::addProvider(std::unique_ptr<DeviceProvider> provider)
{
    {
        std::lock_guard lock(d->mutex);
        d->providers.push_back(std::move(provider));
    }
    rescan();
}

QList<DevicePtr> DeviceManager::devices() const
{
    std::lock_guard lock(d->mutex);
    return d->devices;
}

DevicePtr DeviceManager::device(const QString& id) const
{
    for (const DevicePtr& device : devices()) {
        if (device->info().id == id)
            return device;
    }
    return nullptr;
}

DevicePtr DeviceManager::defaultDevice() const
{
    const QList<DevicePtr> all = devices();
    for (const DevicePtr& device : all) {
        if (!device->info().simulated)
            return device;
    }
    return all.isEmpty() ? nullptr : all.front();
}

void DeviceManager::rescan()
{
    std::lock_guard scanLock(d->scanMutex);
    std::vector<DeviceProvider*> providers;
    {
        std::lock_guard lock(d->mutex);
        for (const auto& p : d->providers)
            providers.push_back(p.get());
    }

    QList<DevicePtr> found;
    QStringList ids;
    for (DeviceProvider* provider : providers) {
        try {
            for (const DevicePtr& device : provider->scan()) {
                const QString id = device->info().id;
                if (!ids.contains(id)) {
                    ids << id;
                    found << device;
                }
            }
        } catch (const std::exception& e) {
            qWarning("Device scan failed (%s): %s", qPrintable(provider->name()), e.what());
        }
    }

    bool changed = false;
    {
        std::lock_guard lock(d->mutex);
        changed = d->deviceIds != ids;
        d->devices = found;
        d->deviceIds = ids;
    }
    if (changed)
        emit devicesChanged();
}

}  // namespace mydaq

// Compiled here so the moc code sees the complete Private type (implicit destructor of the pimpl).
#include "mydaq/moc_device_manager.cpp"
