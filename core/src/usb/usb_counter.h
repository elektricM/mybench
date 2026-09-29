// Counter ctr0 on libmydaq (mydaq_ctr_task): edge count (up, or down on the host), frequency, period, pulse width, semi-period (from the device's
// pulse-time mode), encoders, pulse trains.
#pragma once

#include "usb_device.h"

namespace mydaq::usb {

class UsbCounterInputTask final : public CounterInputTask {
public:
    UsbCounterInputTask(HandlePtr handle, const CounterInputConfig& config);   // reserves ctr0 + line
    ~UsbCounterInputTask() override;

    const CounterInputConfig& config() const override { return m_config; }
    uint8_t dioLinesUsed() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    double readScalar(int timeoutMs) override;
    std::vector<double> readBuffered(int64_t n, int timeoutMs) override;

private:
    double scale(uint32_t raw) const;   // count edges / encoders: raw count -> value

    HandlePtr m_handle;
    CounterInputConfig m_config;
    mydaq_ctr_task* m_task = nullptr;
    double m_semi[2] = {0, 0};   // semi-period: the other half of the last pulse-time read
    int m_semiPending = 0;
};

class UsbCounterOutputTask final : public CounterOutputTask {
public:
    UsbCounterOutputTask(HandlePtr handle, const PulseOutputConfig& config);   // reserves ctr0 + DIO3
    ~UsbCounterOutputTask() override;

    const PulseOutputConfig& config() const override { return m_config; }
    double actualFrequency() const override;
    double actualDutyCycle() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    void update(double frequency, double dutyCycle) override;
    bool waitUntilDone(int timeoutMs) override;

private:
    mydaq_ctr_task* create(const PulseOutputConfig& config);

    HandlePtr m_handle;
    PulseOutputConfig m_config;
    mydaq_ctr_task* m_task = nullptr;
};

}  // namespace mydaq::usb
