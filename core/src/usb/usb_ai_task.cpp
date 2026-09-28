#include "usb_ai_task.h"

#include <limits>

namespace mydaq::usb {

UsbAiTask::UsbAiTask(HandlePtr handle, const AiConfig& config) : m_handle(std::move(handle)), m_config(config)
{
    const auto& channels = config.channels;
    if (channels.empty() || channels.size() > MYDAQ_AI_MAX_CHANNELS)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("An AI task needs 1 or 2 channels"));
    if (channels.size() == 2) {
        if (channels[0].channel == channels[1].channel)
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("%1 is listed twice").arg(toString(channels[0].channel)));
        if (isAudio(channels[0].channel) != isAudio(channels[1].channel))
            throw DeviceError(DeviceError::InvalidConfig,
                              QStringLiteral("AI and AudioInput channels share the converters and cannot be mixed"));
    }
    if (config.samplesPerChannel <= 0)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("samplesPerChannel must be positive"));

    mydaq_ai_config c{};
    for (size_t i = 0; i < channels.size(); ++i)
        c.channels[i] = {toLib(channels[i].channel), aiRange(channels[i].channel, channels[i].range)};
    c.num_channels = channels.size();
    c.sample_rate = config.sampleRate;
    c.mode = config.mode == SampleMode::Finite ? MYDAQ_FINITE : MYDAQ_CONTINUOUS;
    c.samples_per_channel = uint64_t(config.samplesPerChannel);

    // Continuous: samplesPerChannel is the buffer hint; libmydaq keeps at least 2 s on the host.
    const int rc = mydaq_ai_task_create(m_handle->device, &c, &m_task);
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("Analog input is in use by another task"));
    if (rc == MYDAQ_ERR_INVALID_ARG)
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("Invalid AI configuration (rate %1 S/s, %2 samples)").arg(config.sampleRate).arg(config.samplesPerChannel));
    check(rc, QStringLiteral("AI task"));
}

UsbAiTask::~UsbAiTask() { mydaq_ai_task_destroy(m_task); }

double UsbAiTask::sampleRate() const { return mydaq_ai_task_rate(m_task); }
double UsbAiTask::channelSkew() const { return mydaq_ai_task_channel_skew(m_task); }

void UsbAiTask::start()
{
    m_handle->requireConnected();
    check(mydaq_ai_task_start(m_task), QStringLiteral("AI start"));
}

void UsbAiTask::stop() { mydaq_ai_task_stop(m_task); }

bool UsbAiTask::isRunning() const { return mydaq_ai_task_is_running(m_task) != 0; }

int64_t UsbAiTask::available() const { return int64_t(mydaq_ai_task_available(m_task)); }

std::vector<std::vector<double>> UsbAiTask::read(int64_t n, int timeoutMs)
{
    const size_t nch = m_config.channels.size();
    std::vector<std::vector<double>> out(nch);
    if (n < 0) {
        n = available();
        timeoutMs = 0;
    }
    if (n == 0)
        return out;
    if (m_config.mode == SampleMode::Finite && uint64_t(n) > mydaq_ai_task_remaining(m_task))
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("Requested %1 samples, only %2 remain in this acquisition")
                              .arg(n)
                              .arg(qulonglong(mydaq_ai_task_remaining(m_task))));
    std::vector<double> flat(size_t(n) * nch);
    const int rc = mydaq_ai_task_read(m_task, flat.data(), size_t(n), MYDAQ_GROUP_BY_CHANNEL, timeoutMs);
    switch (rc) {
    case MYDAQ_OK: break;
    case MYDAQ_ERR_TIMEOUT:
        throw DeviceError(DeviceError::Timeout, QStringLiteral("%1 samples did not arrive within %2 ms").arg(n).arg(timeoutMs));
    case MYDAQ_ERR_STATE: throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("The acquisition is not running"));
    default: raise(rc, QStringLiteral("AI read"));
    }
    for (size_t c = 0; c < nch; ++c)
        out[c].assign(flat.begin() + ptrdiff_t(c * size_t(n)), flat.begin() + ptrdiff_t((c + 1) * size_t(n)));
    return out;
}

}  // namespace mydaq::usb
