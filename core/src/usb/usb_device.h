// Native NI myDAQ backend: mydaq::Device implemented on libmydaq (libmydaq/include/libmydaq).
#pragma once

#include "mydaq/device.h"

#include <libmydaq/mydaq.h>

#include <atomic>
#include <memory>

namespace mydaq::usb {

// libmydaq context shared by the provider and all its devices (freed after the last one).
using ContextPtr = std::shared_ptr<mydaq_context>;
ContextPtr makeContext(bool withUsb = true);   // throws DeviceError; routes library logs to Qt

// Maps a libmydaq error code to DeviceError (throws only if rc != MYDAQ_OK).
void check(int rc, const QString& what);
[[noreturn]] void raise(int rc, const QString& what);

// An open libmydaq device. Shared by the UsbDevice and its tasks, so a task may outlive the
// Device object; closed when the last owner goes away.
struct Handle {
    Handle(ContextPtr context, mydaq_device* device);
    ~Handle();
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    ContextPtr context;
    mydaq_device* const device;
    std::atomic<bool> gone{false};   // the provider saw the device leave the bus

    // Throws NotConnected once the device is gone.
    void requireConnected() const;
};
using HandlePtr = std::shared_ptr<Handle>;

class UsbDevice final : public Device {
public:
    // Takes ownership of `device` (opened on `context`).
    UsbDevice(ContextPtr context, mydaq_device* device);
    ~UsbDevice() override;

    DeviceInfo info() const override;
    bool isConnected() const override;

    double maxAiRate() const override;
    double maxAoRate() const override;
    double coerceAiRate(double requested) const override;
    double coerceAoRate(double requested) const override;

    std::unique_ptr<AiTask> createAiTask(const AiConfig& config) override;
    std::unique_ptr<AoTask> createAoTask(const AoConfig& config) override;

    double aiReadStatic(AiChannel channel, double range) override;
    void aoWriteStatic(AoChannel channel, double volts) override;

    std::vector<DmmRange> dmmRanges(DmmFunction function) const override;
    DmmReading dmmRead(DmmFunction function, double rangeMax) override;

    void dioSetDirection(uint8_t mask, bool output) override;
    uint8_t dioDirection() const override;
    void dioWrite(uint8_t value, uint8_t mask = 0xFF) override;
    uint8_t dioRead() override;

    void reset() override;

    // Provider side.
    QString location() const { return m_location; }
    uint8_t usbAddress() const { return m_address; }
    void markGone();                          // unplugged: every call throws NotConnected
    mydaq_device* handle() const { return m_handle->device; }

private:
    HandlePtr m_handle;
    DeviceInfo m_info;
    QString m_location;
    uint8_t m_address = 0;
};

// Conversions between the Qt-side enums and libmydaq's.
mydaq_ai_channel toLib(AiChannel channel);
mydaq_ao_channel toLib(AoChannel channel);
mydaq_dmm_function toLib(DmmFunction function);
mydaq_ai_range aiRange(AiChannel channel, double range);   // throws InvalidConfig

}  // namespace mydaq::usb
