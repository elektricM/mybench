#include "usb_device.h"

#include "usb_ai_task.h"
#include "usb_ao_task.h"
#include "usb_counter.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcLibmydaq, "mybench.libmydaq")

namespace mydaq::usb {

namespace {

void logToQt(void*, mydaq_log_level level, const char* message)
{
    switch (level) {
    case MYDAQ_LOG_ERROR:
    case MYDAQ_LOG_WARNING: qCWarning(lcLibmydaq, "%s", message); break;
    case MYDAQ_LOG_INFO: qCInfo(lcLibmydaq, "%s", message); break;
    default: qCDebug(lcLibmydaq, "%s", message); break;
    }
}

DeviceError::Code codeOf(int rc)
{
    switch (rc) {
    case MYDAQ_ERR_INVALID_ARG: return DeviceError::InvalidConfig;
    case MYDAQ_ERR_NO_DEVICE:
    case MYDAQ_ERR_NOT_FOUND: return DeviceError::NotConnected;
    case MYDAQ_ERR_BUSY: return DeviceError::ResourceBusy;
    case MYDAQ_ERR_TIMEOUT: return DeviceError::Timeout;
    case MYDAQ_ERR_OVERFLOW: return DeviceError::Overflow;
    case MYDAQ_ERR_UNDERFLOW: return DeviceError::Underflow;
    case MYDAQ_ERR_USB:
    case MYDAQ_ERR_PROTOCOL:
    case MYDAQ_ERR_DEVICE:
    case MYDAQ_ERR_ACCESS: return DeviceError::UsbError;
    case MYDAQ_ERR_NOT_SUPPORTED: return DeviceError::NotSupported;
    default: return DeviceError::Generic;
    }
}

QString dmmLabel(const char* ascii)
{
    // libmydaq labels are ASCII ("20 MOhm"); the UI shows the symbol.
    QString label = QString::fromLatin1(ascii);
    label.replace(QLatin1String("Ohm"), QStringLiteral("Ω"));
    return label;
}

}  // namespace

ContextPtr makeContext(bool withUsb)
{
    mydaq_context* ctx = nullptr;
    const int rc = withUsb ? mydaq_context_new(&ctx) : mydaq_context_new_no_usb(&ctx);
    check(rc, QStringLiteral("Cannot initialise USB access"));
    // Default: warnings and errors only (no chatter when no device is attached or another program
    // holds it); QT_LOGGING_RULES="mybench.libmydaq.debug=true" for details, MYBENCH_USB_TRACE=1
    // for every frame.
    const mydaq_log_level level = qEnvironmentVariableIntValue("MYBENCH_USB_TRACE") ? MYDAQ_LOG_TRACE
                                  : lcLibmydaq().isDebugEnabled()                  ? MYDAQ_LOG_DEBUG
                                                                                   : MYDAQ_LOG_WARNING;
    mydaq_context_set_log(ctx, level, logToQt, nullptr);
    return ContextPtr(ctx, mydaq_context_free);
}

void raise(int rc, const QString& what)
{
    throw DeviceError(codeOf(rc), QStringLiteral("%1: %2").arg(what, QString::fromUtf8(mydaq_strerror(rc))));
}

void check(int rc, const QString& what)
{
    if (rc != MYDAQ_OK)
        raise(rc, what);
}

mydaq_ai_channel toLib(AiChannel channel)
{
    switch (channel) {
    case AiChannel::AI0: return MYDAQ_AI0;
    case AiChannel::AI1: return MYDAQ_AI1;
    case AiChannel::AudioInLeft: return MYDAQ_AUDIO_IN_LEFT;
    case AiChannel::AudioInRight: return MYDAQ_AUDIO_IN_RIGHT;
    }
    return MYDAQ_AI0;
}

mydaq_ao_channel toLib(AoChannel channel)
{
    switch (channel) {
    case AoChannel::AO0: return MYDAQ_AO0;
    case AoChannel::AO1: return MYDAQ_AO1;
    case AoChannel::AudioOutLeft: return MYDAQ_AUDIO_OUT_LEFT;
    case AoChannel::AudioOutRight: return MYDAQ_AUDIO_OUT_RIGHT;
    }
    return MYDAQ_AO0;
}

mydaq_dmm_function toLib(DmmFunction function)
{
    switch (function) {
    case DmmFunction::DcVoltage: return MYDAQ_DMM_DC_VOLTS;
    case DmmFunction::AcVoltage: return MYDAQ_DMM_AC_VOLTS;
    case DmmFunction::DcCurrent: return MYDAQ_DMM_DC_CURRENT;
    case DmmFunction::AcCurrent: return MYDAQ_DMM_AC_CURRENT;
    case DmmFunction::Resistance: return MYDAQ_DMM_RESISTANCE;
    case DmmFunction::Diode: return MYDAQ_DMM_DIODE;
    case DmmFunction::Continuity: return MYDAQ_DMM_CONTINUITY;
    }
    return MYDAQ_DMM_DC_VOLTS;
}

mydaq_ai_range aiRange(AiChannel channel, double range)
{
    if (isAudio(channel)) {
        if (std::abs(range - 2.0) > 1e-9)
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("%1 has a fixed ±2 V range").arg(toString(channel)));
        return MYDAQ_AI_RANGE_2V;
    }
    if (std::abs(range - 10.0) <= 1e-9)
        return MYDAQ_AI_RANGE_10V;
    if (std::abs(range - 2.0) <= 1e-9)
        return MYDAQ_AI_RANGE_2V;
    throw DeviceError(DeviceError::InvalidConfig,
                      QStringLiteral("%1 range must be ±10 V or ±2 V (got %2 V)").arg(toString(channel)).arg(range));
}

// ---------------------------------------------------------------------------------------------

Handle::Handle(ContextPtr ctx, mydaq_device* dev) : context(std::move(ctx)), device(dev) {}

Handle::~Handle() { mydaq_close(device); }

void Handle::requireConnected() const
{
    if (gone.load() || !mydaq_is_connected(device))
        throw DeviceError(DeviceError::NotConnected, QStringLiteral("The myDAQ was disconnected"));
}

UsbDevice::UsbDevice(ContextPtr context, mydaq_device* device)
    : m_handle(std::make_shared<Handle>(std::move(context), device))
{
    mydaq_info li{};
    mydaq_get_info(device, &li);
    const QString serial = QString::fromLatin1(li.serial);
    m_location = QString::fromLatin1(li.location);
    m_address = li.usb_address;
    m_info.id = QStringLiteral("usb:") + (serial.isEmpty() ? m_location : serial);
    m_info.name = serial.isEmpty() ? QStringLiteral("myDAQ (%1)").arg(m_location) : QStringLiteral("myDAQ (%1)").arg(serial);
    m_info.productType = QStringLiteral("NI myDAQ");
    m_info.serialNumber = serial;
    m_info.simulated = false;
    // The device cannot report the DIO directions and libmydaq caches them from power-up
    // (inputs); another program may have left outputs driven, so start from the safe state.
    mydaq_dio_set_direction(device, 0xff, 0);
}

UsbDevice::~UsbDevice() = default;

DeviceInfo UsbDevice::info() const { return m_info; }

bool UsbDevice::isConnected() const { return !m_handle->gone.load() && mydaq_is_connected(m_handle->device); }

void UsbDevice::markGone() { m_handle->gone = true; }

double UsbDevice::maxAiRate() const { return MYDAQ_AI_MAX_RATE; }
double UsbDevice::maxAoRate() const { return MYDAQ_AO_MAX_RATE; }
double UsbDevice::coerceAiRate(double requested) const { return mydaq_ai_coerce_rate(requested, 1); }
double UsbDevice::coerceAoRate(double requested) const
{
    // Same 100 MHz / 32-bit divider as the AI sample clock (docs/device.md).
    return mydaq_ai_coerce_rate(requested, 1);
}

std::unique_ptr<AiTask> UsbDevice::createAiTask(const AiConfig& config)
{
    m_handle->requireConnected();
    return std::make_unique<UsbAiTask>(m_handle, config);
}

std::unique_ptr<AoTask> UsbDevice::createAoTask(const AoConfig& config)
{
    m_handle->requireConnected();
    return std::make_unique<UsbAoTask>(m_handle, config);
}

std::unique_ptr<CounterInputTask> UsbDevice::createCounterInputTask(const CounterInputConfig& config)
{
    m_handle->requireConnected();
    return std::make_unique<UsbCounterInputTask>(m_handle, config);
}

std::unique_ptr<CounterOutputTask> UsbDevice::createCounterOutputTask(const PulseOutputConfig& config)
{
    m_handle->requireConnected();
    return std::make_unique<UsbCounterOutputTask>(m_handle, config);
}

double UsbDevice::coerceCounterFrequency(double requested) const
{
    double actual = requested;
    mydaq_ctr_coerce_pulse(requested, 0.5, nullptr, nullptr, &actual, nullptr);
    return actual;
}

double UsbDevice::coerceFrequencyOutput(double requested) const { return mydaq_freqout_coerce(requested); }

double UsbDevice::frequencyOutputStart(double frequency)
{
    m_handle->requireConnected();
    double actual = 0;
    const int rc = mydaq_freqout_start(m_handle->device, frequency, &actual);
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("FREQOUT is running, or DIO 4 is in use"));
    check(rc, QStringLiteral("FREQOUT start"));
    return actual;
}

void UsbDevice::frequencyOutputStop() { mydaq_freqout_stop(m_handle->device); }

bool UsbDevice::frequencyOutputRunning() const { return mydaq_freqout_running(m_handle->device) != 0; }

double UsbDevice::aiReadStatic(AiChannel channel, double range)
{
    m_handle->requireConnected();
    const mydaq_ai_channel_config ch{toLib(channel), aiRange(channel, range)};
    double volts = 0;
    if (isAudio(channel))
        throw DeviceError(DeviceError::NotSupported,
                          QStringLiteral("%1 has no on-demand reads: it needs a sample clock (use an AI task)").arg(toString(channel)));
    check(mydaq_ai_read_scan(m_handle->device, &ch, 1, &volts), QStringLiteral("%1 read").arg(toString(channel)));
    return volts;
}

void UsbDevice::aoWriteStatic(AoChannel channel, double volts)
{
    m_handle->requireConnected();
    if (isAudio(channel))
        throw DeviceError(DeviceError::NotSupported,
                          QStringLiteral("%1 has no static writes: it needs a sample clock (use an AO task)").arg(toString(channel)));
    if (!std::isfinite(volts) || std::abs(volts) > 10.0 + 1e-9)
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("%1 V is outside the range of %2").arg(volts).arg(toString(channel)));
    const int rc = mydaq_ao_write(m_handle->device, toLib(channel), std::clamp(volts, -10.0, 10.0));
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("%1 is driven by a hardware-timed task").arg(toString(channel)));
    check(rc, QStringLiteral("%1 write").arg(toString(channel)));
}

std::vector<DmmRange> UsbDevice::dmmRanges(DmmFunction function) const
{
    const mydaq_dmm_range_info* ranges = nullptr;
    size_t count = 0;
    std::vector<DmmRange> out;
    if (mydaq_dmm_ranges(toLib(function), &ranges, &count) != MYDAQ_OK)
        return out;
    for (size_t i = 0; i < count; ++i)
        out.push_back({ranges[i].full_scale, dmmLabel(ranges[i].label)});
    return out;
}

DmmReading UsbDevice::dmmRead(DmmFunction function, double rangeMax)
{
    m_handle->requireConnected();
    mydaq_dmm_reading r{};
    const int rc = mydaq_dmm_read(m_handle->device, toLib(function), rangeMax, &r);
    if (rc == MYDAQ_ERR_NOT_SUPPORTED)
        throw DeviceError(DeviceError::NotSupported,
                          QStringLiteral("DMM %1 is not supported by the native myDAQ driver yet").arg(toString(function)));
    if (rc == MYDAQ_ERR_INVALID_ARG)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("%1 is not a %2 range").arg(rangeMax).arg(toString(function)));
    check(rc, QStringLiteral("DMM reading"));
    DmmReading reading;
    reading.value = r.value;
    reading.overload = r.overload != 0;
    return reading;
}

void UsbDevice::dioSetDirection(uint8_t mask, bool output)
{
    m_handle->requireConnected();
    const int rc = mydaq_dio_set_direction(m_handle->device, mask, output ? 1 : 0);
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("A counter or FREQOUT uses one of these DIO lines"));
    check(rc, QStringLiteral("DIO direction"));
}

uint8_t UsbDevice::dioDirection() const
{
    uint8_t dir = 0;
    mydaq_dio_get_direction(m_handle->device, &dir);
    return dir;
}

void UsbDevice::dioWrite(uint8_t value, uint8_t mask)
{
    m_handle->requireConnected();
    const int rc = mydaq_dio_write(m_handle->device, value, mask);
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("A counter or FREQOUT uses one of these DIO lines"));
    check(rc, QStringLiteral("DIO write"));
}

uint8_t UsbDevice::dioRead()
{
    m_handle->requireConnected();
    uint8_t levels = 0;
    check(mydaq_dio_read(m_handle->device, &levels), QStringLiteral("DIO read"));
    return levels;
}

void UsbDevice::reset()
{
    m_handle->requireConnected();
    check(mydaq_reset(m_handle->device), QStringLiteral("Device reset"));
}

}  // namespace mydaq::usb
