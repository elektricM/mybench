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
    // Keep the refusals in sync with UsbDevice::unsupportedFeatures().
    mydaq_ctr_config c{};
    c.falling = config.edge == Edge::Falling;
    switch (config.measurement) {
    case CounterMeasurement::CountEdges:
        if (config.direction == CounterInputConfig::Direction::External)
            // DAQmx refuses it on the myDAQ, and the encoder decoders do not count with a static
            // direction level on B (libmydaq_hw: X1 with B low counts 0 edges), so no emulation.
            throw unsupported(QStringLiteral("Counting with an external direction input"));
        // The device only counts up (DAQmx refuses down counting on the myDAQ): counting down
        // from the initial count is initial - edges on the host. After a reset on DIO 1 the
        // device restarts from 0, which the host cannot see: so down + reset needs initial 0.
        if (config.direction == CounterInputConfig::Direction::Down && config.resetOnDio1 && config.initialCount != 0)
            throw unsupported(QStringLiteral("Counting down from a non-zero initial count with a reset on DIO 1"));
        c.kind = MYDAQ_CTR_COUNT_EDGES;
        c.initial_count = config.direction == CounterInputConfig::Direction::Up ? config.initialCount : 0;
        c.reset_on_dio1 = config.resetOnDio1;
        break;
    case CounterMeasurement::Frequency: c.kind = MYDAQ_CTR_FREQUENCY; break;
    case CounterMeasurement::Period: c.kind = MYDAQ_CTR_PERIOD; break;
    case CounterMeasurement::PulseWidth: c.kind = MYDAQ_CTR_PULSE_WIDTH; break;   // falling: low time
    case CounterMeasurement::TwoEdgeSeparation:   // first edge DIO2, second edge DIO1
        c.kind = MYDAQ_CTR_TWO_EDGE_SEPARATION;
        c.second_falling = config.secondEdge == Edge::Falling;
        break;
    case CounterMeasurement::SemiPeriod:
        c.kind = MYDAQ_CTR_PULSE_TIME;   // no semi-period mode on the myDAQ: both halves per read
        c.falling = 0;
        break;
    case CounterMeasurement::LinearEncoder:
    case CounterMeasurement::AngularEncoder:
        if (config.zIndexEnabled && config.zIndexValue != 0.0)
            throw unsupported(QStringLiteral("A Z index value other than 0"));
        if (config.measurement == CounterMeasurement::LinearEncoder && !(config.distancePerPulse > 0.0))
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("The distance per pulse must be positive"));
        if (config.measurement == CounterMeasurement::AngularEncoder && config.pulsesPerRevolution < 1)
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("The pulses per revolution must be at least 1"));
        c.kind = config.measurement == CounterMeasurement::LinearEncoder ? MYDAQ_CTR_LINEAR_ENCODER : MYDAQ_CTR_ANGULAR_ENCODER;
        c.falling = 0;
        switch (config.decoding) {
        case CounterInputConfig::Decoding::X1: c.decoding = MYDAQ_CTR_X1; break;
        case CounterInputConfig::Decoding::X2: c.decoding = MYDAQ_CTR_X2; break;
        case CounterInputConfig::Decoding::X4: c.decoding = MYDAQ_CTR_X4; break;
        case CounterInputConfig::Decoding::TwoPulse: c.decoding = MYDAQ_CTR_TWO_PULSE; break;
        }
        c.z_index = config.zIndexEnabled;
        break;
    default: throw unsupported(toString(config.measurement));
    }
    if (config.buffered)
        throw unsupported(QStringLiteral("Buffered counter input (the myDAQ's counter has no data stream)"));
    checkCreate(mydaq_ctr_task_create(m_handle->device, &c, &m_task));
}

double UsbCounterInputTask::scale(uint32_t raw) const
{
    const CounterInputConfig& c = m_config;
    if (c.measurement == CounterMeasurement::CountEdges) {
        if (c.direction == CounterInputConfig::Direction::Down)
            return double(uint32_t(c.initialCount - raw));   // 32-bit, wraps like the counter
        return double(raw);
    }
    const double counts = double(int32_t(raw));   // encoders: signed position
    const double perPulse = c.decoding == CounterInputConfig::Decoding::X2   ? 2.0
                            : c.decoding == CounterInputConfig::Decoding::X4 ? 4.0
                                                                             : 1.0;
    if (c.measurement == CounterMeasurement::LinearEncoder)
        return counts * c.distancePerPulse / perPulse;
    return counts * 360.0 / (double(c.pulsesPerRevolution) * perPulse);
}

UsbCounterInputTask::~UsbCounterInputTask() { mydaq_ctr_task_destroy(m_task); }

uint8_t UsbCounterInputTask::dioLinesUsed() const { return mydaq_ctr_task_dio_lines(m_task); }

void UsbCounterInputTask::start()
{
    m_handle->requireConnected();
    m_semiPending = 0;
    check(mydaq_ctr_task_start(m_task), QStringLiteral("Counter start"));
}

void UsbCounterInputTask::stop() { mydaq_ctr_task_stop(m_task); }

bool UsbCounterInputTask::isRunning() const { return mydaq_ctr_task_is_running(m_task) != 0; }

double UsbCounterInputTask::readScalar(int timeoutMs)
{
    const int timeout = timeoutMs < 0 ? 10000 : timeoutMs;
    auto fail = [&](int rc) {
        if (rc == MYDAQ_ERR_TIMEOUT)
            throw DeviceError(DeviceError::Timeout, QStringLiteral("No complete measurement within %1 ms").arg(timeoutMs));
        if (rc == MYDAQ_ERR_STATE)
            throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("The counter is not running"));
        check(rc, QStringLiteral("Counter read"));
    };
    switch (m_config.measurement) {
    case CounterMeasurement::CountEdges:
    case CounterMeasurement::LinearEncoder:
    case CounterMeasurement::AngularEncoder: {
        uint32_t raw = 0;
        const int rc = mydaq_ctr_task_read_count(m_task, &raw, timeout);
        if (rc != MYDAQ_OK)
            fail(rc);
        return scale(raw);
    }
    case CounterMeasurement::SemiPeriod: {
        // One pulse-time read gives both halves of a period: hand them out one per call.
        if (m_semiPending > 0)
            return m_semi[2 - m_semiPending--];
        uint32_t high = 0, low = 0;
        const int rc = mydaq_ctr_task_read_pulse_time(m_task, &high, &low, timeout);
        if (rc != MYDAQ_OK)
            fail(rc);
        m_semi[0] = high / 100e6;
        m_semi[1] = low / 100e6;
        m_semiPending = 1;
        return m_semi[0];
    }
    default: {
        double value = 0;
        const int rc = mydaq_ctr_task_read(m_task, &value, timeout);
        if (rc != MYDAQ_OK)
            fail(rc);
        return value;
    }
    }
}

std::vector<double> UsbCounterInputTask::readBuffered(int64_t, int)
{
    throw unsupported(QStringLiteral("Buffered counter input (the myDAQ's counter has no data stream)"));
}

// ---- output --------------------------------------------------------------------------------

mydaq_ctr_task* UsbCounterOutputTask::create(const PulseOutputConfig& config)
{
    if (!(config.initialDelay >= 0.0) || config.initialDelay > 42.0)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("The initial delay must be between 0 and 42 s"));
    mydaq_ctr_config c{};
    c.idle_high = config.idleHigh;
    c.initial_delay = config.initialDelay;
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
    PulseOutputConfig next = m_config;
    next.frequency = frequency;
    next.dutyCycle = dutyCycle;
    if (mydaq_ctr_coerce_pulse(frequency, dutyCycle, nullptr, nullptr, nullptr, nullptr) != MYDAQ_OK)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("Invalid pulse frequency or duty cycle"));
    const bool running = isRunning();
    if (running && m_config.mode == SampleMode::Continuous) {
        // Live load update (ctr.1e + ctr.18): the train switches at the next period, no glitch.
        check(mydaq_ctr_task_update_pulse(m_task, frequency, dutyCycle), QStringLiteral("Pulse train update"));
        m_config = next;
        return;
    }
    // Not running (or finite): a new task with the new timing.
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
