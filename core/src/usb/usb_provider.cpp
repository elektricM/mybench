#include "usb_provider.h"

#include "usb_device.h"

#include <QLoggingCategory>
#include <QtGlobal>

#include <set>

Q_LOGGING_CATEGORY(lcUsbProvider, "mybench.usb")

namespace mydaq {

namespace usb {

namespace {

class LibusbBus final : public Bus {
public:
    LibusbBus() : m_context(makeContext(true)) {}

    std::vector<Attached> attached() override
    {
        mydaq_device_desc list[16];
        size_t count = 0;
        std::vector<Attached> out;
        if (mydaq_enumerate(m_context.get(), list, 16, &count, 0) != MYDAQ_OK)
            return out;
        for (size_t i = 0; i < count && i < 16; ++i)
            out.push_back({list[i].location, list[i].address});
        return out;
    }

    std::shared_ptr<UsbDevice> open(const std::string& location) override
    {
        mydaq_device* dev = nullptr;
        check(mydaq_open_location(m_context.get(), location.c_str(), 0, &dev),
              QStringLiteral("Opening the myDAQ at USB %1").arg(QString::fromStdString(location)));
        return std::make_shared<UsbDevice>(m_context, dev);
    }

private:
    ContextPtr m_context;
};

}  // namespace

std::unique_ptr<Bus> makeLibusbBus() { return std::make_unique<LibusbBus>(); }

UsbProvider::UsbProvider(std::unique_ptr<Bus> bus, Options options) : m_bus(std::move(bus)), m_options(options) {}

UsbProvider::~UsbProvider()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

QList<DevicePtr> UsbProvider::scan()
{
    std::lock_guard lock(m_mutex);
    if (m_options.startThread && !m_thread.joinable() && !m_stop)
        m_thread = std::thread([this] { run(); });
    QList<DevicePtr> out;
    for (const auto& [location, device] : m_devices)
        out << device;
    return out;
}

void UsbProvider::run()
{
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        lock.unlock();
        try {
            poll();
        } catch (const std::exception& e) {
            qCWarning(lcUsbProvider, "USB scan failed: %s", e.what());
        }
        lock.lock();
        m_wake.wait_for(lock, m_options.pollInterval, [this] { return m_stop; });
    }
}

void UsbProvider::poll()
{
    std::lock_guard pollLock(m_pollMutex);
    const auto now = Clock::now();
    const std::vector<Bus::Attached> attached = m_bus->attached();
    std::map<std::string, uint8_t> present;
    for (const auto& a : attached)
        present[a.location] = a.address;

    // Devices that left the bus, were re-plugged (new address) or reported an unplug.
    {
        std::lock_guard lock(m_mutex);
        for (auto it = m_devices.begin(); it != m_devices.end();) {
            const auto p = present.find(it->first);
            const bool gone = p == present.end() || p->second != it->second->usbAddress() || !it->second->isConnected();
            if (gone) {
                qCInfo(lcUsbProvider, "%s disconnected", qPrintable(it->second->info().name));
                it->second->markGone();
                it = m_devices.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Forget pending entries that disappeared or changed address.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        const auto p = present.find(it->first);
        if (p == present.end() || p->second != it->second.address)
            it = m_pending.erase(it);
        else
            ++it;
    }

    // New devices: open them once they have settled.
    for (const auto& [location, address] : present) {
        {
            std::lock_guard lock(m_mutex);
            if (m_devices.count(location) || m_stop)
                continue;
        }
        const auto it = m_pending.try_emplace(location, Pending{address, now + m_options.settleTime, false}).first;
        if (now < it->second.retryAt)
            continue;
        try {
            std::shared_ptr<UsbDevice> device = m_bus->open(location);
            qCInfo(lcUsbProvider, "%s ready at USB %s", qPrintable(device->info().name), location.c_str());
            m_pending.erase(it);
            std::lock_guard lock(m_mutex);
            m_devices[location] = std::move(device);
        } catch (const DeviceError& e) {
            const bool busy = e.code() == DeviceError::ResourceBusy;
            if (!it->second.reported)   // once per attach, not per retry
                qCInfo(lcUsbProvider, "myDAQ at USB %s not available: %s", location.c_str(), e.what());
            it->second.reported = true;
            it->second.retryAt = now + (busy ? m_options.retryBusy : m_options.retryError);
        }
    }
}

}  // namespace usb

namespace {

// Placeholder when USB access is disabled or unavailable: finds nothing.
class NoUsbProvider final : public DeviceProvider {
public:
    QString name() const override { return QStringLiteral("USB (disabled)"); }
    QList<DevicePtr> scan() override { return {}; }
};

}  // namespace

std::unique_ptr<DeviceProvider> makeUsbProvider()
{
    if (qEnvironmentVariableIntValue("MYBENCH_NO_USB") != 0)
        return std::make_unique<NoUsbProvider>();
    try {
        return std::make_unique<usb::UsbProvider>(usb::makeLibusbBus(), usb::UsbProvider::Options{});
    } catch (const std::exception& e) {
        qCWarning(lcUsbProvider, "Native USB backend unavailable: %s", e.what());
        return std::make_unique<NoUsbProvider>();
    }
}

}  // namespace mydaq
