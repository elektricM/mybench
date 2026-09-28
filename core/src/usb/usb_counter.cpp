#include "usb_counter.h"

namespace mydaq::usb {

namespace {

DeviceError unsupported(const QString& what)
{
    return DeviceError(DeviceError::NotSupported, what + QStringLiteral(" is not supported by the native myDAQ driver yet"));
}

void checkCreate(int rc)
{
    if (rc == MYDAQ_ERR_BUSY)
        throw DeviceError(DeviceError::ResourceBusy,
                          QStringLiteral("The counter or its DIO line is in use (another counter task, FREQOUT or a DIO output)"));
    if (rc == MYDAQ_ERR_INVALID_ARG)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Invalid counter configuration"));
    check(rc, QStringLiteral("Counter task"));
}

}  // namespace

// ---- input ---------------------------------------------------------------------------------

UsbCounterInputTask::UsbCounterInputTask(HandlePtr handle, const CounterInputConfig& config)
    : m_handle(std::move(handle)), m_config(config)
{
    mydaq_ctr_config c{};
    switch (config.measurement) {
    case CounterMeasurement::CountEdges:
        if (config.direction != CounterInputConfig::Direction::Up || config.initialCount != 0 || config.resetOnDio1)
            throw unsupported(QStringLiteral("Counting down, an initial count or a count reset"));
        c.kind = MYDAQ_CTR_COUNT_EDGES;
        break;
    case CounterMeasurement::Frequency: c.kind = MYDAQ_CTR_FREQUENCY; break;
    case CounterMeasurement::Period: c.kind = MYDAQ_CTR_PERIOD; break;
    default: throw unsupported(toString(config.measurement));
    }
    if (config.edge != Edge::Rising)
        throw unsupported(QStringLiteral("Falling-edge counting"));
    if (config.buffered)
        throw unsupported(QStringLiteral("Buffered counter input"));
    checkCreate(mydaq_ctr_task_create(m_handle->device, &c, &m_task));
}

UsbCounterInputTask::~UsbCounterInputTask() { mydaq_ctr_task_destroy(m_task); }

uint8_t UsbCounterInputTask::dioLinesUsed() const { return mydaq_ctr_task_dio_lines(m_task); }

void UsbCounterInputTask::start()
{
    m_handle->requireConnected();
    check(mydaq_ctr_task_start(m_task), QStringLiteral("Counter start"));
}

void UsbCounterInputTask::stop() { mydaq_ctr_task_stop(m_task); }

bool UsbCounterInputTask::isRunning() const { return mydaq_ctr_task_is_running(m_task) != 0; }

double UsbCounterInputTask::readScalar(int timeoutMs)
{
    double value = 0;
    const int rc = mydaq_ctr_task_read(m_task, &value, timeoutMs < 0 ? 10000 : timeoutMs);
    if (rc == MYDAQ_ERR_TIMEOUT)
        throw DeviceError(DeviceError::Timeout, QStringLiteral("No complete measurement within %1 ms").arg(timeoutMs));
    if (rc == MYDAQ_ERR_STATE)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("The counter is not running"));
    check(rc, QStringLiteral("Counter read"));
    return value;
}

std::vector<double> UsbCounterInputTask::readBuffered(int64_t, int)
{
    throw unsupported(QStringLiteral("Buffered counter input"));
}

// ---- output --------------------------------------------------------------------------------

mydaq_ctr_task* UsbCounterOutputTask::create(const PulseOutputConfig& config)
{
    if (config.idleHigh)
        throw unsupported(QStringLiteral("An idle-high pulse train"));
    if (config.initialDelay != 0.0)
        throw unsupported(QStringLiteral("A pulse-train initial delay"));
    mydaq_ctr_config c{};
    c.kind = MYDAQ_CTR_PULSE_TRAIN;
    c.frequency = config.frequency;
    c.duty_cycle = config.dutyCycle;
    c.mode = config.mode == SampleMode::Finite ? MYDAQ_FINITE : MYDAQ_CONTINUOUS;
    c.pulses = config.pulseCount > 0 ? uint64_t(config.pulseCount) : 0;
    mydaq_ctr_task* task = nullptr;
    checkCreate(mydaq_ctr_task_create(m_handle->device, &c, &task));
    return task;
}

UsbCounterOutputTask::UsbCounterOutputTask(HandlePtr handle, const PulseOutputConfig& config)
    : m_handle(std::move(handle)), m_config(config)
{
    m_task = create(config);
}

UsbCounterOutputTask::~UsbCounterOutputTask() { mydaq_ctr_task_destroy(m_task); }

double UsbCounterOutputTask::actualFrequency() const
{
    double f = 0;
    mydaq_ctr_task_pulse(m_task, &f, nullptr);
    return f;
}

double UsbCounterOutputTask::actualDutyCycle() const
{
    double d = 0;
    mydaq_ctr_task_pulse(m_task, nullptr, &d);
    return d;
}

void UsbCounterOutputTask::start()
{
    m_handle->requireConnected();
    check(mydaq_ctr_task_start(m_task), QStringLiteral("Pulse train start"));
}

void UsbCounterOutputTask::stop() { mydaq_ctr_task_stop(m_task); }

bool UsbCounterOutputTask::isRunning() const { return mydaq_ctr_task_is_running(m_task) != 0; }

void UsbCounterOutputTask::update(double frequency, double dutyCycle)
{
    // The on-the-fly load update (ctr method 18) is not captured yet: restart with the new
    // timing (a glitch of one period at most).
    PulseOutputConfig next = m_config;
    next.frequency = frequency;
    next.dutyCycle = dutyCycle;
    if (mydaq_ctr_coerce_pulse(frequency, dutyCycle, nullptr, nullptr, nullptr, nullptr) != MYDAQ_OK)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Invalid pulse frequency or duty cycle"));
    const bool running = isRunning();
    mydaq_ctr_task_destroy(m_task);   // releases ctr0 so the new task can take it
    m_task = nullptr;
    m_task = create(next);
    m_config = next;
    if (running)
        start();
}

bool UsbCounterOutputTask::waitUntilDone(int timeoutMs)
{
    const int rc = mydaq_ctr_task_wait_done(m_task, timeoutMs);
    if (rc == MYDAQ_OK)
        return true;
    if (rc == MYDAQ_ERR_TIMEOUT || rc == MYDAQ_ERR_STATE)
        return false;
    raise(rc, QStringLiteral("Pulse train"));
}

}  // namespace mydaq::usb
