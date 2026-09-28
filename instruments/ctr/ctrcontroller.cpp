#include "ctrcontroller.h"

#include <mybench/devicehub.h>
#include <mybench/resourcearbiter.h>
#include <mybench/units.h>

#include <QDateTime>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <array>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

using mydaq::CounterInputConfig;
using mydaq::CounterMeasurement;

double nowSeconds() { return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0; }

int decodingFactor(CtrController::Decoding decoding)
{
    switch (decoding) {
    case CtrController::X2:
        return 2;
    case CtrController::X4:
        return 4;
    case CtrController::X1:
    case CtrController::TwoPulse:
        break;
    }
    return 1;
}

// Decimals that show one count of an encoder's resolution.
int decimalsFor(double resolution)
{
    if (!(resolution > 0))
        return 3;
    return std::clamp(int(std::ceil(-std::log10(resolution) - 1e-9)), 0, 6);
}

QString lineList(int mask)
{
    return mask ? ResourceArbiter::dioLinesLabel(mask) : QObject::tr("none");
}

// Stops FREQOUT when the run ends (normally or by an exception).
struct FreqOutGuard {
    mydaq::Device& device;
    bool running = false;
    ~FreqOutGuard()
    {
        if (!running)
            return;
        try {
            device.frequencyOutputStop();
        } catch (const mydaq::DeviceError&) {
            // unplugged: nothing left to stop
        }
    }
};

}  // namespace

CtrController::CtrController() : InstrumentController(u"ctr"_s)
{
    m_trend.setMaximumCount(100000);
    updateResources();
    updatePins();
    updatePreview();
    connect(ResourceArbiter::instance(), &ResourceArbiter::holdersChanged, this, &CtrController::updatePins);
    connect(this, &InstrumentController::stateChanged, this, &CtrController::updatePins);
    connect(DeviceHub::instance(), &DeviceHub::capabilitiesChanged, this, &CtrController::configChanged);
}

CtrController::~CtrController()
{
    shutdown();   // the worker lambda captures `this`
}

// --- Settings ---------------------------------------------------------------------------------

void CtrController::inputChanged(bool restartRun)
{
    if (!isRunning())
        updateResources();
    updatePins();
    emit configChanged();
    if (restartRun && m_counterFunction == Measure)
        restart();
}

void CtrController::setCounterFunction(CounterFunction function)
{
    if (function == m_counterFunction)
        return;
    m_counterFunction = function;
    emit counterFunctionChanged();
    if (!isRunning())
        updateResources();
    updatePins();
    emit configChanged();
    restart();
}

void CtrController::setMeasurement(Measurement measurement)
{
    if (measurement == m_measurement)
        return;
    m_measurement = measurement;
    if (!intervalMeasurement() && m_sampling == Buffered) {
        m_sampling = OnDemand;   // counting measurements have no buffered mode on the myDAQ
        emit samplingChanged();
    }
    clearResults();
    emit measurementChanged();
    inputChanged();
}

void CtrController::setActiveEdge(Slope edge)
{
    if (edge == m_activeEdge)
        return;
    m_activeEdge = edge;
    emit inputSettingsChanged();
    inputChanged();
}

void CtrController::setSecondEdge(Slope edge)
{
    if (edge == m_secondEdge)
        return;
    m_secondEdge = edge;
    emit inputSettingsChanged();
    inputChanged();
}

void CtrController::setCountDirection(CountDirection direction)
{
    if (direction == m_countDirection)
        return;
    m_countDirection = direction;
    emit inputSettingsChanged();
    inputChanged();
}

void CtrController::setInitialCount(double count)
{
    count = std::clamp(std::round(count), 0.0, ctr::kCounterRange - 1.0);
    if (!std::isfinite(count) || count == m_initialCount)
        return;
    m_initialCount = count;
    emit inputSettingsChanged();
    inputChanged();
}

void CtrController::setResetOnDio1(bool reset)
{
    if (reset == m_resetOnDio1)
        return;
    m_resetOnDio1 = reset;
    emit inputSettingsChanged();
    inputChanged();
}

void CtrController::setFrequencyMin(double hertz)
{
    if (!(hertz > 0) || hertz == m_frequencyMin)
        return;
    m_frequencyMin = hertz;
    emit rangeChanged();
    inputChanged(m_measurement == Frequency);
}

void CtrController::setFrequencyMax(double hertz)
{
    if (!(hertz > 0) || hertz == m_frequencyMax)
        return;
    m_frequencyMax = hertz;
    emit rangeChanged();
    inputChanged(m_measurement == Frequency);
}

void CtrController::setTimeMin(double seconds)
{
    if (!(seconds > 0) || seconds == m_timeMin)
        return;
    m_timeMin = seconds;
    emit rangeChanged();
    inputChanged(intervalMeasurement() && m_measurement != Frequency);
}

void CtrController::setTimeMax(double seconds)
{
    if (!(seconds > 0) || seconds == m_timeMax)
        return;
    m_timeMax = seconds;
    emit rangeChanged();
    inputChanged(intervalMeasurement() && m_measurement != Frequency);
}

void CtrController::setDecoding(Decoding decoding)
{
    if (decoding == m_decoding)
        return;
    m_decoding = decoding;
    emit inputSettingsChanged();
    inputChanged(encoderMeasurement());
}

void CtrController::setDistancePerPulse(double distance)
{
    if (!(distance > 0) || distance == m_distancePerPulse)
        return;
    m_distancePerPulse = distance;
    emit inputSettingsChanged();
    inputChanged(m_measurement == LinearEncoder);
}

void CtrController::setDistanceUnit(const QString& unit)
{
    const QString trimmed = unit.trimmed().left(8);
    if (trimmed == m_distanceUnit)
        return;
    m_distanceUnit = trimmed;
    emit inputSettingsChanged();
    emit configChanged();
    emit resultsChanged();
}

void CtrController::setPulsesPerRevolution(int pulses)
{
    pulses = std::clamp(pulses, 1, 1 << 24);
    if (pulses == m_pulsesPerRevolution)
        return;
    m_pulsesPerRevolution = pulses;
    emit inputSettingsChanged();
    inputChanged(m_measurement == AngularEncoder);
}

void CtrController::setZIndexEnabled(bool enabled)
{
    if (enabled == m_zIndexEnabled)
        return;
    m_zIndexEnabled = enabled;
    emit inputSettingsChanged();
    inputChanged(encoderMeasurement());
}

void CtrController::setZIndexValue(double value)
{
    if (!std::isfinite(value) || value == m_zIndexValue)
        return;
    m_zIndexValue = value;
    emit inputSettingsChanged();
    inputChanged(encoderMeasurement() && m_zIndexEnabled);
}

void CtrController::setSampling(Sampling sampling)
{
    if (sampling == Buffered && !intervalMeasurement())
        sampling = OnDemand;
    if (sampling == m_sampling)
        return;
    m_sampling = sampling;
    emit samplingChanged();
    inputChanged();
}

void CtrController::setUpdateInterval(double seconds)
{
    seconds = std::clamp(seconds, 0.01, 10.0);
    if (!std::isfinite(seconds) || seconds == m_updateInterval)
        return;
    m_updateInterval = seconds;
    emit samplingChanged();
    if (m_counterFunction == Measure && m_sampling == OnDemand)
        restart();
}

void CtrController::setBufferedSamples(int samples)
{
    samples = std::clamp(samples, 1, 1000000);
    if (samples == m_bufferedSamples)
        return;
    m_bufferedSamples = samples;
    emit samplingChanged();
}

void CtrController::setPulseDefinition(PulseDefinition definition)
{
    if (definition == m_pulseDefinition)
        return;
    m_pulseDefinition = definition;
    emit pulseChanged();
}

void CtrController::postLive()
{
    m_live->post({m_pulseFrequency, m_dutyCycle, m_freqOutFrequency});
}

void CtrController::setPulseFrequency(double hertz)
{
    hertz = std::clamp(hertz, minimumPulseFrequency(), maximumPulseFrequency());
    if (!std::isfinite(hertz) || hertz == m_pulseFrequency)
        return;
    m_pulseFrequency = hertz;
    postLive();   // applied live by a running continuous pulse train
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain && m_pulseGeneration == FinitePulses)
        restart();
}

void CtrController::setDutyCycle(double duty)
{
    duty = std::clamp(duty, 0.0001, 0.9999);
    if (!std::isfinite(duty) || duty == m_dutyCycle)
        return;
    m_dutyCycle = duty;
    postLive();
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain && m_pulseGeneration == FinitePulses)
        restart();
}

void CtrController::setHighLowTimes(double high, double low)
{
    if (!(high > 0) || !(low > 0))
        return;
    const double period = high + low;
    const double duty = high / period;
    // Both changes in one live update.
    m_pulseFrequency = std::clamp(1.0 / period, minimumPulseFrequency(), maximumPulseFrequency());
    m_dutyCycle = std::clamp(duty, 0.0001, 0.9999);
    postLive();
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain && m_pulseGeneration == FinitePulses)
        restart();
}

void CtrController::setIdleHigh(bool high)
{
    if (high == m_idleHigh)
        return;
    m_idleHigh = high;
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain)
        restart();
}

void CtrController::setInitialDelay(double seconds)
{
    seconds = std::clamp(seconds, 0.0, 100.0);
    if (!std::isfinite(seconds) || seconds == m_initialDelay)
        return;
    m_initialDelay = seconds;
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain)
        restart();
}

void CtrController::setPulseGeneration(Generation generation)
{
    if (generation == m_pulseGeneration)
        return;
    m_pulseGeneration = generation;
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain)
        restart();
}

void CtrController::setPulseCount(int count)
{
    count = std::clamp(count, 1, 1000000000);
    if (count == m_pulseCount)
        return;
    m_pulseCount = count;
    updatePreview();
    emit pulseChanged();
    if (m_counterFunction == PulseTrain && m_pulseGeneration == FinitePulses)
        restart();
}

void CtrController::setFreqOutEnabled(bool enabled)
{
    if (enabled == m_freqOutEnabled)
        return;
    m_freqOutEnabled = enabled;
    emit freqOutChanged();
    if (!isRunning())
        updateResources();
    updatePins();
    emit configChanged();
    restart();
}

void CtrController::setFreqOutFrequency(double hertz)
{
    if (!(hertz > 0) || hertz == m_freqOutFrequency)
        return;
    m_freqOutFrequency = hertz;
    postLive();   // FREQOUT follows live
    emit freqOutChanged();
}

void CtrController::selectFreqOut(int index)
{
    const auto& table = ctr::freqOutTable();
    if (index >= 0 && index < int(table.size()))
        setFreqOutFrequency(table[size_t(index)].frequency);
}

// --- Derived ----------------------------------------------------------------------------------

QStringList CtrController::measurementNames() const
{
    return {tr("Edge Count"), tr("Frequency"), tr("Period"), tr("Pulse Width"), tr("Semi-Period"),
            tr("Two-Edge Separation"), tr("Linear Encoder"), tr("Angular Encoder")};
}

bool CtrController::intervalMeasurement() const
{
    return ctr::isIntervalMeasurement(CounterMeasurement(int(m_measurement)));
}

bool CtrController::encoderMeasurement() const
{
    return ctr::isEncoderMeasurement(CounterMeasurement(int(m_measurement)));
}

QString CtrController::unit() const
{
    switch (m_measurement) {
    case Frequency:
        return u"Hz"_s;
    case Period:
    case PulseWidth:
    case SemiPeriod:
    case TwoEdgeSeparation:
        return u"s"_s;
    case AngularEncoder:
        return u"°"_s;
    case LinearEncoder:
        return m_distanceUnit;
    case CountEdges:
        break;
    }
    return {};
}

QString CtrController::displayName() const
{
    switch (m_counterFunction) {
    case Measure:
        return measurementNames().value(m_measurement);
    case PulseTrain:
        return tr("Pulse Train");
    case CounterOff:
        break;
    }
    return m_freqOutEnabled ? tr("Frequency Output") : tr("Counter off");
}

double CtrController::timebase() const
{
    const CounterInputConfig config = inputConfig();
    return ctr::inputTimebase(config.measurement, config.minValue, config.maxValue);
}

QString CtrController::timebaseText() const
{
    if (!intervalMeasurement())
        return {};
    const double tb = timebase();
    return tr("Timebase %1 · resolution %2 · longest %3")
        .arg(Units::format(tb, u"Hz"_s, 3), Units::format(1.0 / tb, u"s"_s, 3),
             Units::format(ctr::longestInterval(tb), u"s"_s, 3));
}

int CtrController::plannedLines() const
{
    int mask = 0;
    if (m_counterFunction == Measure)
        mask |= ctr::inputLines(inputConfig());
    else if (m_counterFunction == PulseTrain)
        mask |= 1 << ctr::kPulseOutputLine;
    if (m_freqOutEnabled)
        mask |= 1 << ctr::kFreqOutLine;
    return mask;
}

QString CtrController::terminals() const { return lineList(plannedLines()); }

double CtrController::coercedPulseFrequency() const { return ctr::coercePulseFrequency(m_pulseFrequency); }
double CtrController::coercedDutyCycle() const { return ctr::coercePulseDuty(m_pulseFrequency, m_dutyCycle); }
double CtrController::highTime() const { return coercedDutyCycle() / coercedPulseFrequency(); }
double CtrController::lowTime() const { return (1.0 - coercedDutyCycle()) / coercedPulseFrequency(); }

double CtrController::coercedFreqOut() const { return ctr::coerceFreqOut(m_freqOutFrequency).frequency; }

QString CtrController::freqOutDivider() const
{
    const ctr::FreqOutSetting s = ctr::coerceFreqOut(m_freqOutFrequency);
    return u"%1 ÷ %2"_s.arg(Units::format(s.timebase, u"Hz"_s, 3)).arg(s.divisor);
}

QStringList CtrController::freqOutChoices() const
{
    QStringList labels;
    for (const ctr::FreqOutSetting& s : ctr::freqOutTable())
        labels.append(Units::format(s.frequency, u"Hz"_s, 4));
    return labels;
}

int CtrController::freqOutIndex() const
{
    const double f = coercedFreqOut();
    const auto& table = ctr::freqOutTable();
    for (size_t i = 0; i < table.size(); ++i) {
        if (table[i].frequency == f)
            return int(i);
    }
    return -1;
}

QString CtrController::valueText() const
{
    const double v = m_value;
    switch (m_measurement) {
    case CountEdges:
        return std::isfinite(v) ? QString::number(qint64(std::llround(v))) : u"---"_s;
    case LinearEncoder: {
        const int decimals = decimalsFor(m_distancePerPulse / decodingFactor(m_decoding));
        const QString number = std::isfinite(v) ? QString::number(v, 'f', decimals) : u"---"_s;
        return m_distanceUnit.isEmpty() ? number : number + u' ' + m_distanceUnit;
    }
    case AngularEncoder: {
        const int decimals = decimalsFor(360.0 / (m_pulsesPerRevolution * decodingFactor(m_decoding)));
        return (std::isfinite(v) ? QString::number(v, 'f', decimals) : u"---"_s) + u" °"_s;
    }
    default:
        return Units::format(v, unit(), 6, false);
    }
}

QString CtrController::secondaryText() const
{
    const double v = m_value;
    switch (m_measurement) {
    case Frequency:
        return std::isfinite(v) && v > 0 ? tr("Period %1").arg(Units::format(1.0 / v, u"s"_s, 6, false)) : QString();
    case Period:
        return std::isfinite(v) && v > 0 ? tr("Frequency %1").arg(Units::format(1.0 / v, u"Hz"_s, 6, false)) : QString();
    case CountEdges:
        return std::isfinite(m_rate) ? tr("Edge rate %1").arg(Units::format(m_rate, u"Hz"_s, 4)) : QString();
    case LinearEncoder:
        return std::isfinite(m_rate) ? tr("Velocity %1 %2/s").arg(QString::number(m_rate, 'g', 5), m_distanceUnit)
                                     : QString();
    case AngularEncoder:
        return std::isfinite(m_rate)
            ? tr("Speed %1 rpm · %2 °/s").arg(QString::number(m_rate / 6.0, 'f', 1), QString::number(m_rate, 'g', 5))
            : QString();
    case PulseWidth:
    case SemiPeriod:
    case TwoEdgeSeparation:
        break;
    }
    return {};
}

CounterInputConfig CtrController::inputConfig() const
{
    CounterInputConfig c;
    c.measurement = CounterMeasurement(int(m_measurement));
    c.edge = m_activeEdge == Rising ? mydaq::Edge::Rising : mydaq::Edge::Falling;
    c.secondEdge = m_secondEdge == Rising ? mydaq::Edge::Rising : mydaq::Edge::Falling;
    c.direction = m_countDirection == CountUp     ? CounterInputConfig::Direction::Up
                : m_countDirection == CountDown   ? CounterInputConfig::Direction::Down
                                                  : CounterInputConfig::Direction::External;
    c.initialCount = uint32_t(m_initialCount);
    c.resetOnDio1 = m_resetOnDio1;
    if (m_measurement == Frequency) {
        c.minValue = m_frequencyMin;
        c.maxValue = m_frequencyMax;
    } else {
        c.minValue = m_timeMin;
        c.maxValue = m_timeMax;
    }
    c.decoding = CounterInputConfig::Decoding(int(m_decoding));
    c.zIndexEnabled = m_zIndexEnabled;
    c.zIndexValue = m_zIndexValue;
    c.distancePerPulse = m_distancePerPulse;
    c.pulsesPerRevolution = m_pulsesPerRevolution;
    c.buffered = m_sampling == Buffered && intervalMeasurement();
    return c;
}

mydaq::PulseOutputConfig CtrController::pulseConfig() const
{
    mydaq::PulseOutputConfig c;
    c.frequency = m_pulseFrequency;
    c.dutyCycle = m_dutyCycle;
    c.idleHigh = m_idleHigh;
    c.initialDelay = m_initialDelay;
    c.mode = m_pulseGeneration == FinitePulses ? mydaq::SampleMode::Finite : mydaq::SampleMode::Continuous;
    c.pulseCount = m_pulseCount;
    return c;
}

void CtrController::updateResources()
{
    ResourceArbiter::Resources resources;
    if (m_counterFunction != CounterOff)
        resources |= ResourceArbiter::Counter;
    if (m_freqOutEnabled)
        resources |= ResourceArbiter::FrequencyOutput;
    const quint8 lines = quint8(plannedLines());
    if (lines)
        resources |= ResourceArbiter::DigitalLines;
    setRequiredResources(resources, lines);
}

void CtrController::updatePins()
{
    QVariantList pins;
    QStringList conflicts;
    std::vector<ctr::PinRole> roles;
    if (m_counterFunction == Measure)
        roles = ctr::inputPinRoles(inputConfig());
    const int planned = plannedLines();
    for (int line = 0; line < mydaq::kDioLines; ++line) {
        QVariantMap pin;
        pin[u"line"_s] = line;
        QString role;
        QString signal;
        QString owner;
        for (const ctr::PinRole& r : roles) {
            if (r.line == line) {
                role = tr(r.role);
                signal = tr(r.signal);
                owner = u"input"_s;
            }
        }
        if (m_counterFunction == PulseTrain && line == ctr::kPulseOutputLine) {
            role = tr("Out");
            signal = tr("Pulse train output");
            owner = u"pulse"_s;
        }
        if (m_freqOutEnabled && line == ctr::kFreqOutLine) {
            role = tr("FREQ");
            signal = tr("FREQOUT square wave");
            owner = u"freqout"_s;
        }
        QString heldBy;
        for (InstrumentController* holder : ResourceArbiter::instance()->dioHolders(quint8(1u << line))) {
            if (holder != this)
                heldBy = holder->shortTitle();
        }
        const bool conflict = !heldBy.isEmpty() && (planned & (1 << line));
        if (conflict)
            conflicts.append(tr("DIO %1 (%2)").arg(line).arg(heldBy));
        // Counter terminal name of the line (fixed on the myDAQ).
        static const char* const names[] = {"CTR 0 SOURCE", "CTR 0 GATE", "CTR 0 AUX", "CTR 0 OUT", "FREQOUT", "", "", ""};
        pin[u"terminal"_s] = QString::fromLatin1(names[line]);
        pin[u"role"_s] = role;
        pin[u"signal"_s] = signal;
        pin[u"owner"_s] = owner;
        pin[u"heldBy"_s] = heldBy;
        pin[u"conflict"_s] = conflict;
        pins.append(pin);
    }
    QString text;
    if (!conflicts.isEmpty() && !isRunning())
        text = tr("In use by another instrument: %1. Run pauses it.").arg(conflicts.join(u", "_s));
    if (pins != m_pins || text != m_conflictText) {
        m_pins = pins;
        m_conflictText = text;
        emit pinsChanged();
    }
}

void CtrController::updatePreview()
{
    // A few periods of the pulse train as a step trace (level 0 = low, 1 = high), from just
    // before the first pulse.
    const double f = coercedPulseFrequency();
    const double duty = coercedDutyCycle();
    const double period = 1.0 / f;
    const double delay = m_initialDelay;
    const int shown = m_pulseGeneration == FinitePulses ? std::min(m_pulseCount, 4) : 4;
    const double start = std::max(0.0, delay - period);
    const double end = delay + (shown + (m_pulseGeneration == FinitePulses && m_pulseCount <= 4 ? 1.0 : 0.0)) * period;
    const double idle = m_idleHigh ? 1.0 : 0.0;
    const double first = m_idleHigh ? 0.0 : 1.0;   // level of the first part of each period
    const double firstLength = m_idleHigh ? (1.0 - duty) * period : duty * period;

    std::vector<double> x;
    std::vector<double> y;
    auto add = [&](double t, double level) {
        x.push_back(t - delay);   // time relative to the first pulse
        y.push_back(level);
    };
    add(start, idle);
    double t = delay;
    for (int k = 0; k < shown; ++k) {
        add(t, k == 0 ? idle : 1.0 - first);
        add(t, first);
        add(t + firstLength, first);
        add(t + firstLength, 1.0 - first);
        t += period;
    }
    if (m_pulseGeneration == FinitePulses && m_pulseCount <= 4) {
        add(t, 1.0 - first);
        add(t, idle);
    }
    add(std::max(t, end), m_pulseGeneration == FinitePulses && m_pulseCount <= 4 ? idle : 1.0 - first);
    m_pulsePreview.setXY(x, y);
}

void CtrController::clearResults()
{
    m_value = qQNaN();
    m_rate = qQNaN();
    m_stats.reset();
    m_trend.clear();
    emit resultsChanged();
}

void CtrController::resetMeasurement()
{
    clearResults();
    m_resetRequest->store(true);
}

// --- Device capabilities -----------------------------------------------------------------------

QStringList CtrController::featuresInUse() const
{
    namespace F = mydaq::feature;
    QStringList list;
    auto need = [&list](const char* name) { list << QString::fromLatin1(name); };
    if (m_counterFunction == Measure) {
        switch (m_measurement) {
        case CountEdges:
            if (m_countDirection == CountDown)
                need(F::CounterCountDown);
            if (m_countDirection == CountExternal)
                need(F::CounterExternalDirection);
            if (m_initialCount != 0.0)
                need(F::CounterInitialCount);
            if (m_resetOnDio1)
                need(F::CounterResetOnDio);
            [[fallthrough]];
        case Frequency:
        case Period:
            if (m_activeEdge == Falling)
                need(F::CounterFallingEdge);
            break;
        case PulseWidth: need(F::CounterPulseWidth); break;
        case SemiPeriod: need(F::CounterSemiPeriod); break;
        case TwoEdgeSeparation: need(F::CounterTwoEdgeSeparation); break;
        case LinearEncoder: need(F::CounterLinearEncoder); break;
        case AngularEncoder: need(F::CounterAngularEncoder); break;
        }
        if (m_sampling == Buffered && intervalMeasurement())
            need(F::CounterBuffered);
    } else if (m_counterFunction == PulseTrain) {
        if (m_idleHigh)
            need(F::PulseIdleHigh);
        if (m_initialDelay != 0.0)
            need(F::PulseInitialDelay);
    }
    if (m_freqOutEnabled)
        need(F::FrequencyOutput);
    return list;
}

QStringList CtrController::unsupportedInUse() const
{
    QStringList refused;
    for (const QString& feature : featuresInUse()) {
        if (!DeviceHub::instance()->supports(feature))
            refused << featureTitle(feature);
    }
    return refused;
}

bool CtrController::measurementSupported(int measurement) const
{
    static constexpr std::array<const char*, 8> features{
        nullptr, nullptr, nullptr, mydaq::feature::CounterPulseWidth, mydaq::feature::CounterSemiPeriod,
        mydaq::feature::CounterTwoEdgeSeparation, mydaq::feature::CounterLinearEncoder,
        mydaq::feature::CounterAngularEncoder};
    if (measurement < 0 || measurement >= int(features.size()) || !features[size_t(measurement)])
        return true;
    return DeviceHub::instance()->supports(QString::fromLatin1(features[size_t(measurement)]));
}

QString CtrController::featureTitle(const QString& feature)
{
    namespace F = mydaq::feature;
    static const QHash<QString, QString> titles{
        {QString::fromLatin1(F::CounterPulseWidth), tr("Pulse Width")},
        {QString::fromLatin1(F::CounterSemiPeriod), tr("Semi-Period")},
        {QString::fromLatin1(F::CounterTwoEdgeSeparation), tr("Two-Edge Separation")},
        {QString::fromLatin1(F::CounterLinearEncoder), tr("Linear Encoder")},
        {QString::fromLatin1(F::CounterAngularEncoder), tr("Angular Encoder")},
        {QString::fromLatin1(F::CounterFallingEdge), tr("Falling edge")},
        {QString::fromLatin1(F::CounterCountDown), tr("Counting down")},
        {QString::fromLatin1(F::CounterExternalDirection), tr("External count direction")},
        {QString::fromLatin1(F::CounterInitialCount), tr("Initial count")},
        {QString::fromLatin1(F::CounterResetOnDio), tr("Reset on DIO 1")},
        {QString::fromLatin1(F::CounterBuffered), tr("Buffered sampling")},
        {QString::fromLatin1(F::PulseIdleHigh), tr("Idle state High")},
        {QString::fromLatin1(F::PulseInitialDelay), tr("Initial delay")},
        {QString::fromLatin1(F::FrequencyOutput), tr("FREQOUT")},
    };
    return titles.value(feature, feature);
}

// --- Run --------------------------------------------------------------------------------------

InstrumentController::WorkerFunction CtrController::createWorker()
{
    if (m_counterFunction == CounterOff && !m_freqOutEnabled)
        throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig,
                                 tr("nothing to run: choose Measure or Pulse Train, or enable FREQOUT"));
    if (const QStringList refused = unsupportedInUse(); !refused.isEmpty())
        throw InvalidInput(tr("This device cannot run: %1.").arg(refused.join(u", "_s)));
    if (m_counterFunction == Measure) {
        clearResults();   // a new run (Stop -> Run, or a restart after a setting change) starts a fresh trend
    }
    if (m_counterFunction == Measure && intervalMeasurement()) {
        const CounterInputConfig c = inputConfig();
        if (!(c.minValue < c.maxValue))
            throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig,
                                     tr("the expected minimum must be below the expected maximum"));
    }
    updateResources();
    updatePins();
    m_live->clear();
    m_resetRequest->store(false);

    const CounterFunction function = m_counterFunction;
    const bool freqOut = m_freqOutEnabled;
    const double freqOutFrequency = m_freqOutFrequency;
    WorkerFunction counterLoop;
    if (function == Measure)
        counterLoop = measureLoop(m_resetRequest);

    const mydaq::PulseOutputConfig pulse = pulseConfig();
    return [this, function, freqOut, freqOutFrequency, pulse, counterLoop, live = m_live](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        FreqOutGuard guard{device};
        if (freqOut) {
            const double actual = device.frequencyOutputStart(freqOutFrequency);
            guard.running = true;
            ctx.post([this, actual] {
                m_freqOutActual = actual;
                emit outputChanged();
            });
        }

        if (function == Measure) {
            counterLoop(ctx);
            return;
        }

        // FREQOUT's live frequency (the other loops handle it through the same mailbox).
        auto applyFreqOut = [&](const Live& l) {
            if (!freqOut)
                return;
            const double actual = device.frequencyOutputStart(l.freqOut);
            ctx.post([this, actual] {
                m_freqOutActual = actual;
                emit outputChanged();
            });
        };

        if (function == CounterOff) {
            ctx.setStatus(tr("FREQOUT on DIO 4"));
            while (ctx.sleepMs(50)) {
                if (const auto l = live->take())
                    applyFreqOut(*l);
            }
            return;
        }

        // Pulse train on DIO 3.
        auto task = device.createCounterOutputTask(pulse);
        auto publishActual = [&] {
            const double f = task->actualFrequency();
            const double d = task->actualDutyCycle();
            ctx.post([this, f, d] {
                m_outputFrequency = f;
                m_outputDutyCycle = d;
                emit outputChanged();
            });
        };
        publishActual();
        task->start();
        const bool finite = pulse.mode == mydaq::SampleMode::Finite;
        ctx.post([this, finite] {
            m_outputState = finite ? tr("Generating") : tr("Running");
            emit outputChanged();
        });
        if (finite) {
            ctx.setStatus(tr("%n pulse(s) on DIO 3", nullptr, int(pulse.pulseCount)));
            while (!ctx.stopRequested()) {
                if (task->waitUntilDone(50)) {
                    ctx.post([this] {
                        m_outputState = tr("Done");
                        emit outputChanged();
                    });
                    ctx.acquisitionDone();
                    if (freqOut) {
                        // FREQOUT is independent of the counter (ctr.md 2): keep it running and
                        // serving live changes until Stop.
                        ctx.setStatus(tr("Pulses done · FREQOUT on DIO 4"));
                        while (ctx.sleepMs(50)) {
                            if (const auto l = live->take())
                                applyFreqOut(*l);
                        }
                    }
                    return;
                }
                if (const auto l = live->take())
                    applyFreqOut(*l);
            }
            return;
        }
        ctx.setStatus(tr("Pulse train on DIO 3"));
        while (ctx.sleepMs(50)) {
            if (const auto l = live->take()) {
                task->update(l->pulseFrequency, l->dutyCycle);
                publishActual();
                applyFreqOut(*l);
            }
        }
    };
}

InstrumentController::WorkerFunction CtrController::measureLoop(std::shared_ptr<std::atomic<bool>> reset)
{
    const CounterInputConfig base = inputConfig();
    const bool counting = !ctr::isIntervalMeasurement(base.measurement);
    const bool freqOut = m_freqOutEnabled;
    const int intervalMs = int(std::lround(m_updateInterval * 1000));
    const int64_t singleCount = m_bufferedSamples;
    const QString name = measurementNames().value(m_measurement);
    const QString lines = lineList(ctr::inputLines(base));
    // Buffered: one value per measured interval; size the buffer for ~2 s at the fastest rate.
    double fastest = base.measurement == CounterMeasurement::Frequency ? base.maxValue
                                                                      : 1.0 / std::max(base.minValue, 1e-9);
    const int64_t bufferHint = int64_t(std::clamp(fastest * 2.0, 10000.0, 2.0e6));

    return [this, base, counting, freqOut, intervalMs, singleCount, name, lines, bufferHint, reset,
            live = m_live](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        auto serviceLive = [&] {
            if (const auto l = live->take(); l && freqOut) {
                const double actual = device.frequencyOutputStart(l->freqOut);
                ctx.post([this, actual] {
                    m_freqOutActual = actual;
                    emit outputChanged();
                });
            }
        };

        const bool single = ctx.once();
        CounterInputConfig config = base;
        if (config.buffered) {
            config.mode = single ? mydaq::SampleMode::Finite : mydaq::SampleMode::Continuous;
            config.samplesPerChannel = single ? singleCount : bufferHint;
        }
        auto task = device.createCounterInputTask(config);
        task->start();
        ctx.setStatus(tr("%1 on %2").arg(name, lines));

        ctr::RunningStats stats;
        ctr::RateEstimator rate;
        const bool edgeCount = base.measurement == CounterMeasurement::CountEdges;
        ctr::CountUnwrapper unwrap(double(base.initialCount));
        auto publish = [&](double value, double r) {
            ctx.publish([this, value, r, stats] {
                m_value = value;
                m_rate = r;
                m_stats = stats;
                emit resultsChanged();
            });
        };
        auto handleReset = [&] {
            if (!reset->exchange(false))
                return;
            stats.reset();
            rate.reset();
            unwrap.reset(double(base.initialCount));
            if (counting) {   // count again from the initial value
                task.reset();
                task = device.createCounterInputTask(config);
                task->start();
            }
        };

        if (!config.buffered) {
            while (!ctx.stopRequested()) {
                serviceLive();
                handleReset();
                double value = 0;
                try {
                    value = task->readScalar(150);
                    ctx.setTimedOut(false);
                } catch (const mydaq::DeviceError& error) {
                    if (error.code() != mydaq::DeviceError::Timeout)
                        throw;
                    ctx.setTimedOut(true);   // no complete measurement yet (no input edges?)
                    continue;
                }
                if (edgeCount)
                    value = unwrap.add(value);
                const double t = nowSeconds();
                stats.add(value);
                const double r = rate.add(t, value);
                m_trend.append(t, value);
                ctx.addSamples(1);
                publish(value, r);
                ctx.acquisitionDone();
                if (ctx.once())
                    break;
                // Wait for the next reading, still serving live FREQOUT changes.
                for (int waited = 0; waited < intervalMs && !ctx.stopRequested(); waited += 50) {
                    ctx.sleepMs(std::min(50, intervalMs - waited));
                    serviceLive();
                }
            }
            return;
        }

        // Buffered: every measured interval is one value.
        int64_t received = 0;
        double lastRead = nowSeconds();
        double lastValue = lastRead;
        while (ctx.sleepMs(100)) {
            serviceLive();
            handleReset();
            const std::vector<double> values = task->readBuffered(-1, 0);
            const double t = nowSeconds();
            if (values.empty()) {
                ctx.setTimedOut(t - lastValue > 2.0);
                lastRead = t;
                continue;
            }
            ctx.setTimedOut(false);
            for (double v : values)
                stats.add(v);
            // Trend: every value of slow signals, block means of fast ones (<= 500 points per read).
            const size_t n = values.size();
            const size_t points = std::min<size_t>(n, 500);
            for (size_t p = 0; p < points; ++p) {
                const size_t from = p * n / points;
                const size_t to = (p + 1) * n / points;
                double sum = 0;
                for (size_t i = from; i < to; ++i)
                    sum += values[i];
                m_trend.append(lastRead + (t - lastRead) * double(p + 1) / double(points), sum / double(to - from));
            }
            received += int64_t(n);
            lastRead = lastValue = t;
            ctx.addSamples(qint64(n));
            publish(values.back(), qQNaN());
            ctx.acquisitionDone();
            if (single && received >= singleCount)
                break;
        }
    };
}

void CtrController::onStarted()
{
    m_outputState.clear();
    emit outputChanged();
    updatePins();
}

void CtrController::onFinished()
{
    m_freqOutActual = 0;
    if (m_outputState != tr("Done"))
        m_outputState.clear();
    emit outputChanged();
    updateResources();
    updatePins();
}

DataTable CtrController::exportTable() const
{
    DataTable table;
    table.title = title();
    table.instrumentId = instrumentId();
    table.addMetadata(tr("Function"), displayName());
    if (m_counterFunction == Measure) {
        table.addMetadata(tr("Terminals"), lineList(ctr::inputLines(inputConfig())));
        table.addMetadata(tr("Sampling"), m_sampling == Buffered ? tr("Buffered (every interval)")
                                                                 : tr("On demand, every %1").arg(Units::format(m_updateInterval, u"s"_s)));
        if (intervalMeasurement())
            table.addMetadata(tr("Timebase"), Units::format(timebase(), u"Hz"_s, 3));
        table.addMetadata(tr("Count"), QString::number(m_stats.count()));
        table.addMetadata(tr("Minimum"), DataExport::formatNumber(m_stats.minimum()));
        table.addMetadata(tr("Maximum"), DataExport::formatNumber(m_stats.maximum()));
        table.addMetadata(tr("Mean"), DataExport::formatNumber(m_stats.mean()));
        table.addMetadata(tr("Std deviation"), DataExport::formatNumber(m_stats.standardDeviation()));
    }
    if (m_freqOutEnabled)
        table.addMetadata(tr("FREQOUT"), Units::format(coercedFreqOut(), u"Hz"_s, 6));
    const plot::TraceSnapshot s = m_trend.snapshot();
    if (s && !s->empty()) {
        std::vector<double> time(s->size());
        std::vector<double> values(s->size());
        const double t0 = s->xAt(0);
        table.addMetadata(tr("Start"), QDateTime::fromMSecsSinceEpoch(qint64(t0 * 1000)).toString(Qt::ISODateWithMs));
        for (size_t i = 0; i < s->size(); ++i) {
            time[i] = s->xAt(i) - t0;
            values[i] = s->yAt(i);
        }
        table.addColumn(tr("Time"), u"s"_s, std::move(time));
        table.addColumn(measurementNames().value(m_measurement), unit(), std::move(values));
    }
    return table;
}

void registerCtr(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"ctr"_s,
        .title = QObject::tr("Counter/Timer"),
        .shortTitle = QObject::tr("CTR"),
        .description = QObject::tr("Frequency, period, edges, encoders; pulse train, FREQOUT"),
        .module = u"MyBench.Instruments.Ctr"_s,
        .icon = u"instrument-ctr"_s,
        .order = 70,
        .createController = [] { return new CtrController; },
    });
}

}  // namespace mybench::instruments
