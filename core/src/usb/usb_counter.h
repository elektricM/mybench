// Counter ctr0 (edge count, frequency, period, pulse train) on libmydaq (mydaq_ctr_task).
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
    HandlePtr m_handle;
    CounterInputConfig m_config;
    mydaq_ctr_task* m_task = nullptr;
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
