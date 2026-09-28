// Native USB backend entry point (libusb through libmydaq). See docs/libmydaq.md.
#pragma once

#include "mydaq/device_manager.h"

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mydaq {

// Finds NI myDAQ devices (USB 3923:755b) and opens them in the background. Disabled when the
// environment variable MYBENCH_NO_USB=1 is set (e.g. for tests that must not touch hardware).
std::unique_ptr<DeviceProvider> makeUsbProvider();

namespace usb {

class UsbDevice;

// What the provider needs from the bus; the real one uses libmydaq/libusb, tests use mocks.
class Bus {
public:
    struct Attached {
        std::string location;   // stable per USB port
        uint8_t address = 0;    // changes on re-plug
    };
    virtual ~Bus() = default;
    virtual std::vector<Attached> attached() = 0;                            // never opens a device
    virtual std::shared_ptr<UsbDevice> open(const std::string& location) = 0;   // throws DeviceError
};

std::unique_ptr<Bus> makeLibusbBus();

// Hot-plug by periodic enumeration in a monitor thread: new devices are opened (and
// initialised, ~2 s) there, so scan() never blocks; devices that leave the bus are marked
// disconnected (their calls throw NotConnected) and dropped from the list.
class UsbProvider final : public DeviceProvider {
public:
    struct Options {
        std::chrono::milliseconds pollInterval{1000};
        // A newly attached device is opened only once it has been on the bus this long (lets
        // enumeration settle; avoids grabbing a device another program is about to open).
        std::chrono::milliseconds settleTime{2000};
        std::chrono::milliseconds retryBusy{5000};    // after "in use by another program"
        std::chrono::milliseconds retryError{10000};  // after other open failures
        bool startThread = true;                      // false: call poll() yourself (tests)
    };

    UsbProvider(std::unique_ptr<Bus> bus, Options options);
    ~UsbProvider() override;

    QString name() const override { return QStringLiteral("USB"); }
    QList<DevicePtr> scan() override;   // snapshot; starts the monitor on first use

    void poll();   // one monitor pass (called by the monitor thread)

private:
    using Clock = std::chrono::steady_clock;
    struct Pending {
        uint8_t address = 0;
        Clock::time_point retryAt;
        bool reported = false;   // open failure already logged
    };

    void run();

    std::unique_ptr<Bus> m_bus;
    const Options m_options;
    std::mutex m_pollMutex;   // serialises poll()
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::thread m_thread;
    std::map<std::string, std::shared_ptr<UsbDevice>> m_devices;   // by location (m_mutex)
    std::map<std::string, Pending> m_pending;                      // seen, not open (poll only)
};

}  // namespace usb
}  // namespace mydaq
