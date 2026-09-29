#include "sim_counter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mydaq::sim {

namespace {

using Clock = Core::Clock;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kCounterRange = 4294967296.0;   // 32-bit counter
constexpr double kPulseTimebase = 100e6;
constexpr double kMaxPulseFrequency = kPulseTimebase / 4.0;             // 2 ticks high + 2 low
constexpr double kMinPulseFrequency = kPulseTimebase / kCounterRange;   // 32-bit period
constexpr int64_t kMinBuffer = 10000;   // buffered continuous: values kept before Overflow (at least)
constexpr size_t kWakeLookahead = 4096; // intervals simulated ahead to schedule a wake-up

DeviceError invalid(const QString& message) { return DeviceError(DeviceError::InvalidConfig, message); }

bool isEncoder(CounterMeasurement m)
{
    return m == CounterMeasurement::LinearEncoder || m == CounterMeasurement::AngularEncoder;
}

bool isInterval(CounterMeasurement m) { return m != CounterMeasurement::CountEdges && !isEncoder(m); }

// Counts per encoder cycle for a decoding.
int decodingFactor(CounterInputConfig::Decoding d)
{
    switch (d) {
    case CounterInputConfig::Decoding::X1: return 1;
    case CounterInputConfig::Decoding::X2: return 2;
    case CounterInputConfig::Decoding::X4: return 4;
    case CounterInputConfig::Decoding::TwoPulse: return 1;
    }
    return 1;
}

double wrap32(double count)
{
    const double r = std::fmod(count, kCounterRange);
    return r < 0.0 ? r + kCounterRange : r;
}

// Count change of one encoder input change; `which`: 0 = A, 1 = B.
int encoderStep(CounterInputConfig::Decoding decoding, int which, bool oldA, bool oldB, bool a, bool b)
{
    using Decoding = CounterInputConfig::Decoding;
    switch (decoding) {
    case Decoding::X4: {
        // Gray sequence while turning forward: AB = 10, 11, 01, 00.
        const auto index = [](bool x, bool y) { return x ? (y ? 1 : 0) : (y ? 2 : 3); };
        const int step = (index(a, b) - index(oldA, oldB) + 4) % 4;
        return step == 1 ? 1 : step == 3 ? -1 : 0;
    }
    case Decoding::X2:
        if (which != 0)
            return 0;
        return a ? (b ? -1 : 1) : (b ? 1 : -1);
    case Decoding::X1:
        if (which != 0 || b)
            return 0;
        return a ? 1 : -1;
    case Decoding::TwoPulse:
        if (which == 0)
            return a ? 1 : 0;
        return b ? -1 : 0;
    }
    return 0;
}

// Visits every change of the given lines (null = unused) in (from, until], in time order.
template <size_t N, class Visit>
void forEachChange(const std::array<const DigitalTimeline*, N>& lines, double from, double until, Visit&& visit)
{
    std::array<DigitalChange, N> next{};
    for (size_t i = 0; i < N; ++i)
        next[i] = lines[i] ? lines[i]->nextChange(from) : DigitalChange{kInf, false};
    for (;;) {
        size_t k = 0;
        for (size_t i = 1; i < N; ++i)
            if (next[i].time < next[k].time)
                k = i;
        if (!(next[k].time <= until))
            return;
        visit(k, next[k]);
        next[k] = lines[k]->nextChange(next[k].time);
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Validation and coercion

void validateCounterInput(const CounterInputConfig& config)
{
    const CounterMeasurement m = config.measurement;
    if (isInterval(m)) {
        if (!(config.minValue > 0.0) || !(config.maxValue > config.minValue))
            throw invalid(QStringLiteral("The expected range needs 0 < minValue < maxValue"));
    } else if (config.buffered) {
        throw DeviceError(DeviceError::NotSupported,
                          QStringLiteral("Buffered %1 needs a sample clock, which the myDAQ counter does not have")
                              .arg(toString(m)));
    }
    if (m == CounterMeasurement::LinearEncoder && !(config.distancePerPulse > 0.0))
        throw invalid(QStringLiteral("distancePerPulse must be positive"));
    if (m == CounterMeasurement::AngularEncoder && config.pulsesPerRevolution < 1)
        throw invalid(QStringLiteral("pulsesPerRevolution must be at least 1"));
    if (config.buffered && config.samplesPerChannel <= 0)
        throw invalid(QStringLiteral("samplesPerChannel must be positive"));
}

void validatePulseOutput(const PulseOutputConfig& config)
{
    if (!(config.frequency >= kMinPulseFrequency * (1.0 - 1e-9)) || config.frequency > kMaxPulseFrequency * (1.0 + 1e-9))
        throw invalid(QStringLiteral("Pulse frequency %1 Hz is outside %2 Hz .. 25 MHz")
                          .arg(config.frequency)
                          .arg(kMinPulseFrequency));
    if (!(config.dutyCycle > 0.0) || !(config.dutyCycle < 1.0))
        throw invalid(QStringLiteral("The duty cycle must be between 0 and 1 (exclusive)"));
    if (!(config.initialDelay >= 0.0))
        throw invalid(QStringLiteral("The initial delay cannot be negative"));
    if (config.mode == SampleMode::Finite && config.pulseCount < 1)
        throw invalid(QStringLiteral("A finite pulse train needs at least one pulse"));
}

uint8_t counterInputLines(const CounterInputConfig& config)
{
    switch (config.measurement) {
    case CounterMeasurement::CountEdges:
        return uint8_t(lineMask(0) | (config.direction == CounterInputConfig::Direction::External ? lineMask(2) : 0)
                       | (config.resetOnDio1 ? lineMask(1) : 0));
    case CounterMeasurement::Frequency:
    case CounterMeasurement::Period:
    case CounterMeasurement::PulseWidth:
    case CounterMeasurement::SemiPeriod: return lineMask(1);
    case CounterMeasurement::TwoEdgeSeparation: return uint8_t(lineMask(2) | lineMask(1));
    case CounterMeasurement::LinearEncoder:
    case CounterMeasurement::AngularEncoder:
        return uint8_t(lineMask(0) | lineMask(2) | (config.zIndexEnabled ? lineMask(1) : 0));
    }
    return 0;
}

double counterInputTimebase(const CounterInputConfig& config)
{
    if (!isInterval(config.measurement))
        return kPulseTimebase;
    // Longest interval to count: a period of the slowest expected frequency, or the largest value.
    const double longest = config.measurement == CounterMeasurement::Frequency ? 1.0 / config.minValue
                                                                               : config.maxValue;
    for (double timebase : {100e6, 20e6}) {
        if (longest * timebase < kCounterRange)
            return timebase;
    }
    return 100e3;   // slowest; longer intervals wrap the 32-bit count like the hardware
}

double coercePulseFrequency(double requested)
{
    if (!(requested > 0.0))
        return kMinPulseFrequency;
    return kPulseTimebase / std::clamp(std::round(kPulseTimebase / requested), 4.0, kCounterRange);
}

double coercePulseDuty(double frequency, double duty)
{
    const double ticks = std::round(kPulseTimebase / frequency);
    return std::clamp(std::round(duty * ticks), 2.0, ticks - 2.0) / ticks;
}

double coerceFreqOut(double requested)
{
    double best = kInf;
    for (double timebase : {20e6, 10e6, 100e3}) {
        for (int n = 1; n <= 16; ++n) {
            const double f = timebase / n;
            if (std::abs(f - requested) < std::abs(best - requested))
                best = f;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Counter input

SimCounterInputTask::SimCounterInputTask(std::shared_ptr<Core> core, const CounterInputConfig& config)
    : m_core(std::move(core)),
      m_config(config),
      m_lines(counterInputLines(config)),
      m_timebase(counterInputTimebase(config)),
      m_capacity(std::max<int64_t>(config.samplesPerChannel, kMinBuffer))
{
    m_core->ciTask = this;
    m_core->counterLines = m_lines;
}

SimCounterInputTask::~SimCounterInputTask()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
    if (m_core->ciTask == this) {
        m_core->ciTask = nullptr;
        m_core->counterLines = 0;
    }
}

bool SimCounterInputTask::countsEvents() const { return !isInterval(m_config.measurement); }

SimCounterInputTask::IntervalSpec SimCounterInputTask::intervalSpec() const
{
    const Polarity edge = m_config.edge == Edge::Rising ? Polarity::Rising : Polarity::Falling;
    const Polarity opposite = edge == Polarity::Rising ? Polarity::Falling : Polarity::Rising;
    switch (m_config.measurement) {
    case CounterMeasurement::PulseWidth: return {1, edge, 1, opposite, false};
    case CounterMeasurement::SemiPeriod: return {1, Polarity::Any, 1, Polarity::Any, true};
    case CounterMeasurement::TwoEdgeSeparation: {
        const Polarity second = m_config.secondEdge == Edge::Rising ? Polarity::Rising : Polarity::Falling;
        return {2, edge, 1, second, false};
    }
    default: return {1, edge, 1, edge, true};   // Frequency, Period
    }
}

DigitalChange SimCounterInputTask::nextOf(int line, double t, Polarity p) const
{
    const DigitalTimeline& signal = m_core->dioExternal[size_t(line)];
    return p == Polarity::Any ? signal.nextChange(t) : signal.nextEdge(t, p == Polarity::Rising);
}

DigitalChange SimCounterInputTask::previousOf(int line, double t, Polarity p) const
{
    const DigitalTimeline& signal = m_core->dioExternal[size_t(line)];
    return p == Polarity::Any ? signal.previousChange(t) : signal.previousEdge(t, p == Polarity::Rising);
}

double SimCounterInputTask::valueOf(double from, double to) const
{
    // Timebase edges counted between the two input edges, in a 32-bit register.
    double ticks = std::fmod(std::floor(to * m_timebase) - std::floor(from * m_timebase), kCounterRange);
    ticks = std::max(ticks, 1.0);
    return m_config.measurement == CounterMeasurement::Frequency ? m_timebase / ticks : ticks / m_timebase;
}

bool SimCounterInputTask::latestInterval(double t, double& from, double& to) const
{
    const IntervalSpec s = intervalSpec();
    DigitalChange end = previousOf(s.endLine, t, s.endPolarity);
    for (int i = 0; i < 64 && end.time >= m_start; ++i) {
        if (s.chained) {
            const DigitalChange begin = previousOf(s.startLine, end.time, s.startPolarity);
            if (begin.time < m_start)
                return false;
            from = begin.time;
            to = end.time;
            return true;
        }
        // The interval ending at `end` began at the first start edge after the previous end edge.
        const DigitalChange previousEnd = previousOf(s.endLine, end.time, s.endPolarity);
        const DigitalChange begin = nextOf(s.startLine, std::max(previousEnd.time, m_start), s.startPolarity);
        if (begin.time < end.time) {
            from = begin.time;
            to = end.time;
            return true;
        }
        end = previousEnd;
    }
    return false;
}

double SimCounterInputTask::firstIntervalEnd() const
{
    const IntervalSpec s = intervalSpec();
    const DigitalChange begin = nextOf(s.startLine, m_start, s.startPolarity);
    return begin.time < kInf ? nextOf(s.endLine, begin.time, s.endPolarity).time : kInf;
}

double SimCounterInputTask::scan(Scanner& scanner, double until, size_t limit, std::deque<double>* values) const
{
    const IntervalSpec s = intervalSpec();
    double lastEnd = kInf;
    for (size_t produced = 0; produced < limit; ++produced) {
        if (!scanner.hasStart) {
            const DigitalChange begin = nextOf(s.startLine, scanner.cursor, s.startPolarity);
            if (begin.time == kInf || begin.time > until) {
                scanner.cursor = std::max(scanner.cursor, until);
                break;
            }
            scanner.hasStart = true;
            scanner.pendingStart = scanner.cursor = begin.time;
        }
        const DigitalChange end = nextOf(s.endLine, scanner.pendingStart, s.endPolarity);
        if (end.time == kInf || end.time > until)
            break;
        if (values)
            values->push_back(valueOf(scanner.pendingStart, end.time));
        lastEnd = end.time;
        scanner.cursor = end.time;
        scanner.pendingStart = end.time;
        scanner.hasStart = s.chained;
    }
    return lastEnd;
}

void SimCounterInputTask::produceLocked(double until) const
{
    if (!m_config.buffered || !m_started)
        return;
    // Continuous: stop one value past the buffer, which is enough to detect an overflow.
    const size_t limit = m_config.mode == SampleMode::Finite
        ? size_t(m_config.samplesPerChannel - m_produced)
        : size_t(std::max<int64_t>(0, m_capacity + 1 - int64_t(m_queue.size())));
    const size_t before = m_queue.size();
    scan(m_scanner, std::min(until, m_stop), limit, &m_queue);
    m_produced += int64_t(m_queue.size() - before);
}

double SimCounterInputTask::countValueLocked(double until) const
{
    const auto& lines = m_core->dioExternal;
    const CounterInputConfig& c = m_config;
    if (c.measurement == CounterMeasurement::CountEdges) {
        const bool rising = c.edge == Edge::Rising;
        if (c.direction != CounterInputConfig::Direction::External) {
            double from = m_start;
            double base = double(c.initialCount);
            if (c.resetOnDio1) {   // count reset to 0 on each rising edge of DIO1
                const DigitalChange reset = lines[1].previousEdge(until, true);
                if (reset.time >= m_start) {
                    from = reset.time;
                    base = 0.0;
                }
            }
            const double sign = c.direction == CounterInputConfig::Direction::Up ? 1.0 : -1.0;
            return wrap32(base + sign * double(lines[0].countEdges(from, until, rising)));
        }
        // External direction (DIO2 high counts up): walk the edges.
        const std::array<const DigitalTimeline*, 2> inputs{&lines[0], c.resetOnDio1 ? &lines[1] : nullptr};
        forEachChange(inputs, m_eventCursor, until, [&](size_t which, const DigitalChange& change) {
            if (which == 0 && change.level == rising)
                m_count += lines[2].levelAt(change.time) ? 1 : -1;
            else if (which == 1 && change.level)
                m_count = 0;
        });
        m_eventCursor = std::max(m_eventCursor, until);
        return wrap32(double(m_count));
    }

    // Encoders: A on DIO0, B on DIO2, Z on DIO1.
    const int factor = decodingFactor(c.decoding);
    const double scale = c.measurement == CounterMeasurement::LinearEncoder
        ? c.distancePerPulse / factor
        : 360.0 / (double(c.pulsesPerRevolution) * factor);
    const auto zCounts = int64_t(std::llround(c.zIndexValue / scale));
    const std::array<const DigitalTimeline*, 3> inputs{&lines[0], &lines[2], c.zIndexEnabled ? &lines[1] : nullptr};
    forEachChange(inputs, m_eventCursor, until, [&](size_t which, const DigitalChange& change) {
        const bool oldA = m_a;
        const bool oldB = m_b;
        if (which == 0)
            m_a = change.level;
        else if (which == 1)
            m_b = change.level;
        else
            m_z = change.level;
        if (which < 2)
            m_count += encoderStep(c.decoding, int(which), oldA, oldB, m_a, m_b);
        if (c.zIndexEnabled) {   // reload when entering Z high with A and B high
            const bool condition = m_z && m_a && m_b;
            if (condition && !m_zCondition)
                m_count = zCounts;
            m_zCondition = condition;
        }
    });
    m_eventCursor = std::max(m_eventCursor, until);
    return double(m_count) * scale;
}

double SimCounterInputTask::oldestNeededTime(double now) const
{
    if (!m_started)
        return now;
    if (m_config.buffered)
        return m_scanner.cursor;
    if (m_config.measurement == CounterMeasurement::CountEdges
        && m_config.direction != CounterInputConfig::Direction::External)
        return m_start;   // counted in closed form from the start
    return countsEvents() ? m_eventCursor : now;
}

void SimCounterInputTask::stopLocked(double t)
{
    if (m_started && t < m_stop) {
        m_stop = std::max(t, m_start);
        m_core->changed.notify_all();
    }
}

void SimCounterInputTask::start()
{
    std::lock_guard lock(m_core->mutex);
    m_core->requireConnected();
    const double t = m_core->now();
    const auto& lines = m_core->dioExternal;
    m_started = true;
    m_start = t;
    m_stop = kForever;
    m_scanner = Scanner{t, t, false};
    m_queue.clear();
    m_produced = 0;
    m_eventCursor = t;
    m_count = m_config.measurement == CounterMeasurement::CountEdges ? int64_t(m_config.initialCount) : 0;
    m_a = lines[0].levelAt(t);
    m_b = lines[2].levelAt(t);
    m_z = lines[1].levelAt(t);
    m_zCondition = m_z && m_a && m_b;
    m_core->changed.notify_all();
}

void SimCounterInputTask::stop()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
}

bool SimCounterInputTask::isRunning() const
{
    std::lock_guard lock(m_core->mutex);
    if (!m_core->connected || !m_started)
        return false;
    const double now = m_core->now();
    if (now >= m_stop)
        return false;
    if (m_config.buffered && m_config.mode == SampleMode::Finite) {
        produceLocked(now);
        return m_produced < m_config.samplesPerChannel;
    }
    return true;
}

double SimCounterInputTask::readScalar(int timeoutMs)
{
    std::unique_lock lock(m_core->mutex);
    m_core->requireConnected();
    if (!m_started)
        throw DeviceError(DeviceError::Generic, QStringLiteral("The counter task is not started"));
    if (countsEvents()) {
        const double value = countValueLocked(std::min(m_core->now(), m_stop));
        m_core->prune();
        return value;
    }
    const Clock::time_point deadline = deadlineFor(timeoutMs);
    for (;;) {
        const double now = m_core->now();
        double from = 0.0;
        double to = 0.0;
        if (latestInterval(std::min(now, m_stop), from, to))
            return valueOf(from, to);
        if (now >= m_stop)
            throw DeviceError(DeviceError::Generic,
                              QStringLiteral("The counter task stopped before a measurement completed"));
        if (Clock::now() >= deadline)
            throw DeviceError(DeviceError::Timeout, QStringLiteral("No complete %1 measurement within %2 ms (no input edges?)")
                                                        .arg(toString(m_config.measurement))
                                                        .arg(timeoutMs));
        Clock::time_point wake = deadline;
        const double first = firstIntervalEnd();
        if (first < kInf)
            wake = std::min(wake, m_core->timePoint(first + kWakeSlack));
        m_core->waitUntil(lock, wake);
        m_core->requireConnected();
    }
}

std::vector<double> SimCounterInputTask::readBuffered(int64_t n, int timeoutMs)
{
    std::unique_lock lock(m_core->mutex);
    m_core->requireConnected();
    if (!m_config.buffered)
        throw DeviceError(DeviceError::InvalidConfig, QStringLiteral("This counter task is on-demand: use readScalar()"));
    if (n == 0 || (n < 0 && !m_started))
        return {};
    if (!m_started)
        throw DeviceError(DeviceError::Generic, QStringLiteral("The counter task is not started"));
    const bool finite = m_config.mode == SampleMode::Finite;
    const int64_t remaining = m_config.samplesPerChannel - (m_produced - int64_t(m_queue.size()));
    if (finite && n > remaining)
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("Cannot read %1 values: %2 remain in this measurement").arg(n).arg(remaining));
    if (!finite && n > m_capacity)
        throw DeviceError(DeviceError::InvalidConfig,
                          QStringLiteral("Cannot read %1 values at once: the buffer holds %2").arg(n).arg(m_capacity));

    const Clock::time_point deadline = deadlineFor(timeoutMs);
    size_t count = 0;
    for (;;) {
        const double now = m_core->now();
        produceLocked(now);
        if (!finite && int64_t(m_queue.size()) > m_capacity) {
            stopLocked(now);
            m_started = false;
            m_queue.clear();
            throw DeviceError(DeviceError::Overflow,
                              QStringLiteral("Counter buffer overflow: values were not read fast enough"));
        }
        if (n < 0 || int64_t(m_queue.size()) >= n) {
            count = n < 0 ? m_queue.size() : size_t(n);
            break;
        }
        if (now >= m_stop)
            throw DeviceError(DeviceError::Generic,
                              QStringLiteral("The counter task stopped before %1 values were measured").arg(n));
        if (Clock::now() >= deadline)
            throw DeviceError(DeviceError::Timeout, QStringLiteral("Timed out waiting for %1 counter values (%2 measured)")
                                                        .arg(n)
                                                        .arg(m_queue.size()));
        // Wake when the missing values will have been measured (bounded look-ahead).
        Scanner ahead = m_scanner;
        const double ready = scan(ahead, kInf, std::min(size_t(n) - m_queue.size(), kWakeLookahead), nullptr);
        Clock::time_point wake = deadline;
        if (ready < kInf)
            wake = std::min(wake, m_core->timePoint(ready + kWakeSlack));
        m_core->waitUntil(lock, wake);
        m_core->requireConnected();
    }
    std::vector<double> values(m_queue.begin(), m_queue.begin() + ptrdiff_t(count));
    m_queue.erase(m_queue.begin(), m_queue.begin() + ptrdiff_t(count));
    m_core->prune();
    return values;
}

// ---------------------------------------------------------------------------------------------
// Pulse train output (DIO3)

SimCounterOutputTask::SimCounterOutputTask(std::shared_ptr<Core> core, const PulseOutputConfig& config)
    : m_core(std::move(core)),
      m_config(config),
      m_frequency(coercePulseFrequency(config.frequency)),
      m_duty(coercePulseDuty(m_frequency, config.dutyCycle))
{
    m_core->coTask = this;
    m_core->counterLines = lineMask(kPulseOutputLine);
    m_core->pulseOutput.setConstant(m_core->now(), config.idleHigh);
}

SimCounterOutputTask::~SimCounterOutputTask()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
    if (m_core->coTask == this) {
        m_core->coTask = nullptr;
        m_core->counterLines = 0;
    }
}

double SimCounterOutputTask::actualFrequency() const
{
    std::lock_guard lock(m_core->mutex);
    return m_frequency;
}

double SimCounterOutputTask::actualDutyCycle() const
{
    std::lock_guard lock(m_core->mutex);
    return m_duty;
}

void SimCounterOutputTask::schedulePulses(double t)
{
    // Idle low: each period is high then low. Idle high: low then high (duty = high fraction).
    m_segmentBegin = t;
    m_core->pulseOutput.setSquare(t, m_config.idleHigh ? m_duty : 0.0, m_frequency, m_duty);
}

void SimCounterOutputTask::start()
{
    std::lock_guard lock(m_core->mutex);
    m_core->requireConnected();
    const double t = m_core->now();
    stopLocked(t);
    m_running = true;
    m_firstPulse = t + m_config.initialDelay;
    m_core->pulseOutput.setConstant(t, m_config.idleHigh);
    schedulePulses(m_firstPulse);
    m_end = kForever;
    if (finite()) {
        m_end = m_firstPulse + double(m_config.pulseCount) / m_frequency;
        m_core->pulseOutput.setConstant(m_end, m_config.idleHigh);
    }
    m_core->changed.notify_all();
    m_core->prune();
}

void SimCounterOutputTask::stopLocked(double t)
{
    if (!m_running)
        return;
    m_core->pulseOutput.setConstant(t, m_config.idleHigh);
    m_running = false;
    m_core->changed.notify_all();
}

void SimCounterOutputTask::stop()
{
    std::lock_guard lock(m_core->mutex);
    stopLocked(m_core->now());
}

bool SimCounterOutputTask::isRunning() const
{
    std::lock_guard lock(m_core->mutex);
    return m_core->connected && m_running && (!finite() || m_core->now() < m_end);
}

void SimCounterOutputTask::update(double frequency, double dutyCycle)
{
    std::lock_guard lock(m_core->mutex);
    m_core->requireConnected();
    PulseOutputConfig next = m_config;
    next.frequency = frequency;
    next.dutyCycle = dutyCycle;
    validatePulseOutput(next);
    const double t = m_core->now();
    if (m_running && finite() && t < m_end)
        throw DeviceError(DeviceError::NotSupported,
                          QStringLiteral("update() changes a continuous pulse train; stop a finite one first"));
    // The new timing starts at the next period boundary (or with the first pulse).
    double boundary = m_firstPulse;
    if (t >= m_firstPulse)
        boundary = m_segmentBegin > t
            ? m_segmentBegin
            : m_segmentBegin + (std::floor((t - m_segmentBegin) * m_frequency) + 1.0) / m_frequency;
    m_config = next;
    m_frequency = coercePulseFrequency(frequency);
    m_duty = coercePulseDuty(m_frequency, dutyCycle);
    if (m_running && !finite())
        schedulePulses(boundary);
    m_core->prune();
}

bool SimCounterOutputTask::waitUntilDone(int timeoutMs)
{
    std::unique_lock lock(m_core->mutex);
    m_core->requireConnected();
    const Clock::time_point deadline = deadlineFor(timeoutMs);
    for (;;) {
        if (!m_running || (finite() && m_core->now() >= m_end))
            return true;
        if (Clock::now() >= deadline)
            return false;
        Clock::time_point wake = deadline;
        if (finite())
            wake = std::min(wake, m_core->timePoint(m_end + kWakeSlack));
        m_core->waitUntil(lock, wake);
        m_core->requireConnected();
    }
}

}  // namespace mydaq::sim
