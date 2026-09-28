// Hardware-timed analog output on libmydaq (mydaq_ao_task): finite, continuous regenerated,
// continuous streamed; AO0/AO1 (±10 V) or the audio outputs (±2 V).
#pragma once

#include "usb_device.h"

namespace mydaq::usb {

class UsbAoTask final : public AoTask {
public:
    UsbAoTask(HandlePtr handle, const AoConfig& config);   // reserves timed AO (ResourceBusy)
    ~UsbAoTask() override;

    const AoConfig& config() const override { return m_config; }
    double sampleRate() const override;
    void write(const std::vector<std::vector<double>>& data, int timeoutMs = 10000) override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    bool waitUntilDone(int timeoutMs) override;
    int64_t spaceAvailable() const override;

private:
    HandlePtr m_handle;
    AoConfig m_config;
    mydaq_ao_task* m_task = nullptr;
};

}  // namespace mydaq::usb
