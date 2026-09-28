// Hardware-timed analog input on libmydaq (mydaq_ai_task).
#pragma once

#include "usb_device.h"

namespace mydaq::usb {

class UsbAiTask final : public AiTask {
public:
    UsbAiTask(HandlePtr handle, const AiConfig& config);   // reserves AI (ResourceBusy)
    ~UsbAiTask() override;

    const AiConfig& config() const override { return m_config; }
    double sampleRate() const override;
    double channelSkew() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;
    std::vector<std::vector<double>> read(int64_t n, int timeoutMs) override;
    int64_t available() const override;

private:
    HandlePtr m_handle;
    AiConfig m_config;
    mydaq_ai_task* m_task = nullptr;
};

}  // namespace mydaq::usb
