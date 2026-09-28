#include "usb_ao_task.h"

#include <cmath>

namespace mydaq::usb {

UsbAoTask::UsbAoTask(HandlePtr handle, const AoConfig& config) : m_handle(std::move(handle)), m_config(config)
{
    const auto& channels = config.channels;
    if (channels.empty() || channels.size() > 2)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("An AO task needs 1 or 2 channels"));
    if (channels.size() == 2) {
        if (channels[0].channel == channels[1].channel)
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("%1 is listed twice").arg(toString(channels[0].channel)));
        if (isAudio(channels[0].channel) != isAudio(channels[1].channel))
            throw DeviceError(DeviceError::InvalidConfig,
                              QStringLiteral("AO and AudioOutput channels share the converters and cannot be mixed"));
    }
    mydaq_ao_config c{};
    for (size_t i = 0; i < channels.size(); ++i) {
        const double range = isAudio(channels[i].channel) ? 2.0 : 10.0;
        if (std::abs(channels[i].range - range) > 1e-9) {
            if (!isAudio(channels[i].channel) && std::abs(channels[i].range - 2.0) <= 1e-9)
                throw DeviceError(DeviceError::NotSupported,
                                  QStringLiteral("The ±2 V range of %1 is not supported by the native myDAQ driver yet")
                                      .arg(toString(channels[i].channel)));
            throw DeviceError(DeviceError::InvalidConfig,
                              QStringLiteral("%1 range must be ±%2 V").arg(toString(channels[i].channel)).arg(range));
        }
        c.channels[i] = toLib(channels[i].channel);
    }
    c.num_channels = channels.size();
    c.sample_rate = config.sampleRate;
    c.mode = config.mode == SampleMode::Finite ? MYDAQ_FINITE : MYDAQ_CONTINUOUS;
    c.regenerate = config.regenerate ? 1 : 0;
    c.buffer_samples = size_t(std::max(4096.0, 2.0 * config.sampleRate));   // streaming: 2 s
    const int rc = mydaq_ao_task_create(m_handle->device, &c, &m_task);
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("Analog output is in use by another task"));
    if (rc == MYDAQ_ERR_INVALID_ARG)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Invalid AO configuration (rate %1 S/s)").arg(config.sampleRate));
    check(rc, QStringLiteral("AO task"));
}

UsbAoTask::~UsbAoTask() { mydaq_ao_task_destroy(m_task); }

double UsbAoTask::sampleRate() const { return mydaq_ao_task_rate(m_task); }

void UsbAoTask::write(const std::vector<std::vector<double>>& data, int timeoutMs)
{
    const size_t nch = m_config.channels.size();
    if (data.size() != nch)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Expected data for %1 channel(s)").arg(nch));
    const size_t n = data.front().size();
    for (const auto& ch : data)
        if (ch.size() != n)
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("All channels need the same number of samples"));
    if (n == 0)
        return;
    if (m_config.mode == SampleMode::Continuous && m_config.regenerate && isRunning())
        throw DeviceError(DeviceError::NotSupported,
                          QStringLiteral("A regenerated output cannot be rewritten while running (stop, write, start)"));
    std::vector<double> flat;
    flat.reserve(n * nch);
    for (const auto& ch : data)
        flat.insert(flat.end(), ch.begin(), ch.end());
    const int rc = mydaq_ao_task_write(m_task, flat.data(), n, MYDAQ_GROUP_BY_CHANNEL, timeoutMs);
    switch (rc) {
    case MYDAQ_OK: return;
    case MYDAQ_ERR_TIMEOUT:
        throw DeviceError(DeviceError::Timeout, QStringLiteral("The output buffer did not free up within %1 ms").arg(timeoutMs));
    case MYDAQ_ERR_INVALID_ARG:
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Output value out of range, or block larger than the buffer"));
    case MYDAQ_ERR_STATE:
        throw DeviceError(DeviceError::NotSupported, QStringLiteral("The output buffer cannot change while generating"));
    default: raise(rc, QStringLiteral("AO write"));
    }
}

void UsbAoTask::start()
{
    m_handle->requireConnected();
    const int rc = mydaq_ao_task_start(m_task);
    if (rc == MYDAQ_ERR_STATE)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Write the output data before starting"));
    check(rc, QStringLiteral("AO start"));
}

void UsbAoTask::stop() { mydaq_ao_task_stop(m_task); }

bool UsbAoTask::isRunning() const { return mydaq_ao_task_is_running(m_task) != 0; }

bool UsbAoTask::waitUntilDone(int timeoutMs)
{
    const int rc = mydaq_ao_task_wait_done(m_task, timeoutMs);
    if (rc == MYDAQ_OK)
        return true;
    if (rc == MYDAQ_ERR_TIMEOUT || rc == MYDAQ_ERR_STATE)
        return false;
    raise(rc, QStringLiteral("AO generation"));
}

int64_t UsbAoTask::spaceAvailable() const
{
    const int err = mydaq_ao_task_error(m_task);
    if (err == MYDAQ_ERR_UNDERFLOW)
        throw DeviceError(DeviceError::Underflow, QStringLiteral("The output ran out of data (underflow)"));
    return int64_t(mydaq_ao_task_space_available(m_task));
}

}  // namespace mydaq::usb
