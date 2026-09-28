#include "diocontroller.h"

#include <mybench/devicehub.h>
#include <mybench/notifications.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <future>

using namespace Qt::StringLiterals;
using Clock = std::chrono::steady_clock;

namespace mybench::instruments {

namespace {

constexpr double kTimelineHistory = 600.0;    // s kept in the timeline (10 min)
constexpr size_t kMaximumSamples = 100'000;   // export buffer
constexpr int kMaximumSleepMs = 20;           // worker reaction time to live changes

QString directionName(bool output)
{
    return output ? QCoreApplication::translate("DioController", "Out") : QCoreApplication::translate("DioController", "In");
}

}  // namespace

void DioController::SampleLog::add(double t, uint8_t levels)
{
    std::lock_guard lock(mutex);
    samples.emplace_back(t, levels);
    while (samples.size() > kMaximumSamples || (!samples.empty() && samples.front().first < t - kTimelineHistory))
        samples.pop_front();
}

void DioController::SampleLog::clear()
{
    std::lock_guard lock(mutex);
    samples.clear();
}

DioController::DioController() : InstrumentController(u"dio"_s)
{
    m_timeline.setLaneCount(dio::kLines);
    m_timeline.setMaximumSpan(kTimelineHistory);
    updateResources();

    // Lines declared by other instruments (Counter/Timer terminals, FREQOUT).
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    connect(registry, &InstrumentRegistry::controllerCreated, this, [this](InstrumentController* controller) {
        watchController(controller);
        updateOwners();
    });
    for (InstrumentController* controller : registry->controllers())
        watchController(controller);
    connect(ResourceArbiter::instance(), &ResourceArbiter::holdersChanged, this, &DioController::updateOwners);
    updateOwners();

    // A pause (another instrument takes lines) must free the outputs before it starts: tell the
    // worker while it is stopping (see createWorker()).
    connect(this, &InstrumentController::stateChanged, this, [this] {
        if (state() == Stopping && !pauseReason().isEmpty())
            m_releaseOnExit->store(true);
        updateOwners();
    });
    // Shell hooks: Stop while idle, panel close and session reset release driven lines.
    connect(this, &DioController::drivenMaskChanged, this, [this] { setHoldsOutputs(m_drivenMask != 0); });
    // A device reset or unplugging turns every line into an input: the "still driven" state ends.
    DeviceHub* hub = DeviceHub::instance();
    connect(hub, &DeviceHub::connectionChanged, this, [this, hub] {
        if (!hub->isConnected())
            forgetDrivenLines();
    });
    connect(hub, &DeviceHub::resetFinished, this, [this] { forgetDrivenLines(); });
    connect(hub, &DeviceHub::deviceLost, this, [this] { forgetDrivenLines(); });
    // Quitting: every line we still drive goes back to input.
    if (QCoreApplication* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this] {
            shutdown();
            releaseLines(uint8_t(m_drivenMask), true);
        });
}

DioController::~DioController()
{
    shutdown();   // the worker lambda captures `this`
}

// --- Directions -----------------------------------------------------------------------------

void DioController::setInputMask(int mask)
{
    mask &= 0xFF;
    if (mask == m_inputMask)
        return;
    m_inputMask = mask;
    m_outputMask &= ~mask;   // a line is read or driven, never both
    directionsEdited();
}

void DioController::setOutputMask(int mask)
{
    mask &= 0xFF;
    if (mask == m_outputMask)
        return;
    m_outputMask = mask;
    m_inputMask &= ~mask;
    directionsEdited();
}

void DioController::applyDirections(uint8_t inputs, uint8_t outputs, const QString& movedMessage)
{
    inputs &= uint8_t(~outputs);
    if (inputs == m_inputMask && outputs == m_outputMask)
        return;
    m_inputMask = inputs;
    m_outputMask = outputs;
    directionsEdited();
    if (!movedMessage.isEmpty())
        Notifications::instance()->show(Notifications::Info, movedMessage, {}, {}, {}, instrumentId());
}

void DioController::directionsEdited()
{
    const int clamped = std::clamp(m_manualValue, 0, outputMax());
    if (clamped != m_manualValue) {
        m_manualValue = clamped;
        emit manualValueChanged();
    }
    emit directionsChanged();
    emit patternChanged();   // "Ramp (0-<max>)" follows the width
    if (isRunning()) {
        // New lines must be claimed and configured: a new run (the sequence restarts, the
        // timeline continues).
        m_restarting = true;
        restart();
    } else {
        updateResources();
        releaseLines(uint8_t(m_drivenMask & ~m_outputMask), false);
    }
    updateOwners();
    markSettingsChanged();
}

void DioController::setReadPreset(int preset)
{
    if (preset < dio::Lines0to3 || preset > dio::NoLines)
        return;
    const uint8_t mask = dio::presetMask(dio::Preset(preset));
    const uint8_t moved = mask & uint8_t(m_outputMask);
    const QString message = moved ? tr("%1 changed from %2 to %3.")
                                        .arg(ResourceArbiter::dioLinesLabel(moved), directionName(true), directionName(false))
                                  : QString();
    applyDirections(mask, uint8_t(m_outputMask & ~mask), message);
}

void DioController::setWritePreset(int preset)
{
    if (preset < dio::Lines0to3 || preset > dio::NoLines)
        return;
    const uint8_t mask = dio::presetMask(dio::Preset(preset));
    const uint8_t moved = mask & uint8_t(m_inputMask);
    const QString message = moved ? tr("%1 changed from %2 to %3.")
                                        .arg(ResourceArbiter::dioLinesLabel(moved), directionName(false), directionName(true))
                                  : QString();
    applyDirections(uint8_t(m_inputMask & ~mask), mask, message);
}

void DioController::setLineDirection(int line, LineDirection direction)
{
    if (line < 0 || line >= dio::kLines)
        return;
    const uint8_t bit = uint8_t(1 << line);
    uint8_t inputs = uint8_t(m_inputMask & ~bit);
    uint8_t outputs = uint8_t(m_outputMask & ~bit);
    if (direction == In)
        inputs |= bit;
    else if (direction == Out)
        outputs |= bit;
    applyDirections(inputs, outputs, {});
}

DioController::LineDirection DioController::lineDirection(int line) const
{
    if (line < 0 || line >= dio::kLines)
        return Unused;
    if (m_outputMask & (1 << line))
        return Out;
    if (m_inputMask & (1 << line))
        return In;
    return Unused;
}

void DioController::updateResources()
{
    setRequiredResources(ResourceArbiter::DigitalLines, quint8(m_inputMask | m_outputMask));
}

// --- Writer settings ------------------------------------------------------------------------

DioController::Live DioController::liveSnapshot() const
{
    Live live;
    live.pattern = dio::Pattern(m_pattern);
    live.manualValue = m_manualValue;
    live.direction = dio::Direction(m_shiftDirection);
    live.stepMs = m_stepInterval;
    live.sampleMs = m_sampleInterval;
    return live;
}

void DioController::postLive()
{
    m_live->post(liveSnapshot());
}

void DioController::setInputRadix(Radix radix)
{
    if (radix == m_inputRadix)
        return;
    m_inputRadix = radix;
    emit inputRadixChanged();
}

void DioController::setOutputRadix(Radix radix)
{
    if (radix == m_outputRadix)
        return;
    m_outputRadix = radix;
    emit outputRadixChanged();
}

void DioController::setPattern(WriterPattern pattern)
{
    if (pattern == m_pattern || pattern < Manual || pattern > WalkingOnes)
        return;
    m_pattern = pattern;
    emit patternChanged();
    postLive();
}

void DioController::setManualValue(int value)
{
    value = std::clamp(value, 0, outputMax());
    if (value == m_manualValue)
        return;
    m_manualValue = value;
    emit manualValueChanged();
    postLive();
}

void DioController::setShiftDirection(ShiftDirection direction)
{
    if (direction == m_shiftDirection)
        return;
    m_shiftDirection = direction;
    emit shiftDirectionChanged();
    postLive();
}

void DioController::setStepInterval(int milliseconds)
{
    milliseconds = std::clamp(milliseconds, dio::kMinimumIntervalMs, dio::kMaximumStepIntervalMs);
    if (milliseconds == m_stepInterval)
        return;
    m_stepInterval = milliseconds;
    emit stepIntervalChanged();
    postLive();
}

void DioController::setSampleInterval(int milliseconds)
{
    milliseconds = std::clamp(milliseconds, dio::kMinimumIntervalMs, dio::kMaximumSampleIntervalMs);
    if (milliseconds == m_sampleInterval)
        return;
    m_sampleInterval = milliseconds;
    emit sampleIntervalChanged();
    postLive();
}

void DioController::setTimelineWindow(int window)
{
    window = std::clamp(window, 0, 3);
    if (window == m_timelineWindow)
        return;
    m_timelineWindow = window;
    emit timelineWindowChanged();
}

void DioController::setTimelinePaused(bool paused)
{
    if (paused == m_timelinePaused)
        return;
    m_timelinePaused = paused;
    emit timelinePausedChanged();
}

double DioController::timelineSeconds() const
{
    static constexpr double spans[] = {5.0, 10.0, 30.0, 60.0};
    return spans[std::clamp(m_timelineWindow, 0, 3)];
}

QStringList DioController::patternNames() const
{
    const int width = outputWidth();
    return {tr("Manual"), width > 0 ? tr("Ramp (0-%1)").arg(dio::maxValue(width)) : tr("Ramp (0-255)"),
            tr("Alternating 1/0's"), tr("Walking 1's")};
}

void DioController::toggleLine(int line)
{
    const int bit = dio::bitOfLine(line, uint8_t(m_outputMask));
    if (bit < 0 || m_pattern != Manual)
        return;
    setManualValue(m_manualValue ^ (1 << bit));
}

void DioController::applyAction(WriterAction action)
{
    if (outputWidth() == 0)
        return;
    setManualValue(dio::applyAction(dio::Action(action), m_manualValue, outputWidth(), dio::Direction(m_shiftDirection)));
}

void DioController::setLineLabel(int line, const QString& label)
{
    if (line < 0 || line >= dio::kLines || m_lineLabels[line] == label.trimmed())
        return;
    m_lineLabels[line] = label.trimmed();
    emit lineLabelsChanged();
    markSettingsChanged();
}

QString DioController::lineName(int line) const
{
    const QString name = u"DIO %1"_s.arg(line);
    const QString label = line >= 0 && line < m_lineLabels.size() ? m_lineLabels[line] : QString();
    return label.isEmpty() ? name : name + u" · "_s + label;
}

QString DioController::formatValue(int value, Radix radix, int width) const
{
    return dio::formatValue(value, dio::Radix(radix), width);
}

QString DioController::numericValueText(bool output) const
{
    const Radix radix = output ? m_outputRadix : m_inputRadix;
    const int value = output ? writtenValue() : (m_hasReading ? inputValue() : 0);
    return dio::formatValue(value, dio::Radix(radix), 0, false);
}

int DioController::parseEntry(const QString& text, Radix radix) const
{
    return dio::parseEntry(text, dio::Radix(radix), outputMax());
}

QString DioController::valuesAtTime(double t, Radix radix) const
{
    uint8_t levels = 0;
    for (int line = 0; line < dio::kLines; ++line) {
        if (m_timeline.levelAt(line, t))
            levels |= uint8_t(1 << line);
    }
    QStringList parts;
    if (m_inputMask)
        parts << tr("In %1 %2").arg(dio::radixLetter(dio::Radix(radix)),
                                    dio::formatValue(dio::gather(levels, uint8_t(m_inputMask)), dio::Radix(radix), 0, false));
    if (m_outputMask)
        parts << tr("Out %1 %2").arg(dio::radixLetter(dio::Radix(radix)),
                                     dio::formatValue(dio::gather(levels, uint8_t(m_outputMask)), dio::Radix(radix), 0, false));
    return parts.join(u" · "_s);
}

QString DioController::radixLetter(Radix radix)
{
    return dio::radixLetter(dio::Radix(radix));
}

void DioController::clearTimeline()
{
    m_timeline.clear();
    m_samples->clear();
    m_origin = Clock::now();
    m_timelineEnd = 0;
    emit timelineAdvanced();
}

// --- Other instruments' lines -------------------------------------------------------------

void DioController::watchController(InstrumentController* controller)
{
    if (!controller || controller == this || m_watched.contains(controller))
        return;
    m_watched.insert(controller);
    connect(controller, &InstrumentController::resourcesChanged, this, &DioController::updateOwners);
    connect(controller, &InstrumentController::stateChanged, this, &DioController::updateOwners);
    connect(controller, &QObject::destroyed, this, [this, controller] { m_watched.remove(controller); });
}

void DioController::updateOwners()
{
    std::array<QStringList, dio::kLines> names;
    std::array<QStringList, dio::kLines> ids;
    std::array<bool, dio::kLines> active{};
    int foreign = 0;
    int conflict = 0;
    const int mine = m_inputMask | m_outputMask;
    for (InstrumentController* controller : InstrumentRegistry::instance()->controllers()) {
        if (controller == this || !controller->resources().testFlag(ResourceArbiter::DigitalLines))
            continue;
        const int lines = controller->dioLines();
        const bool on = controller->isRunning() || controller->isPaused();
        foreign |= lines;
        if (on)
            conflict |= lines & mine;
        for (int line = 0; line < dio::kLines; ++line) {
            if (!(lines & (1 << line)))
                continue;
            names[size_t(line)] << controller->shortTitle();
            ids[size_t(line)] << controller->instrumentId();
            active[size_t(line)] = active[size_t(line)] || on;
        }
    }
    QVariantList owners;
    for (int line = 0; line < dio::kLines; ++line) {
        QVariantMap entry;
        const QString who = names[size_t(line)].join(u", "_s);
        entry.insert(u"owner"_s, who);
        entry.insert(u"ownerId"_s, ids[size_t(line)].join(u","_s));
        entry.insert(u"active"_s, active[size_t(line)]);
        entry.insert(u"text"_s, who.isEmpty() ? QString()
                                : active[size_t(line)] ? tr("DIO %1 is in use by %2.").arg(line).arg(who)
                                                       : tr("DIO %1 is a terminal of %2 (used when it runs).").arg(line).arg(who));
        owners << entry;
    }
    if (owners == m_lineOwners && foreign == m_foreignMask && conflict == m_conflictMask)
        return;
    m_lineOwners = owners;
    m_foreignMask = foreign;
    m_conflictMask = conflict;
    emit ownersChanged();
}

// --- Outputs back to inputs -----------------------------------------------------------------

void DioController::releaseLines(uint8_t mask, bool synchronous)
{
    if (!mask)
        return;
    m_drivenMask &= ~mask;
    emit drivenMaskChanged();
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    if (!device)
        return;
    auto job = [device, mask, dioMutex = m_dioMutex] {
        std::lock_guard lock(*dioMutex);
        try {
            if (device->isConnected())
                device->dioSetDirection(mask, false);
        } catch (const mydaq::DeviceError&) {
            // Unplugged or lines reserved meanwhile: nothing left to release.
        }
    };
    if (synchronous)
        job();
    else
        QThreadPool::globalInstance()->start(job);   // never block the UI on the device
}

void DioController::forgetDrivenLines()
{
    if (m_drivenMask == 0)
        return;
    m_drivenMask = 0;
    emit drivenMaskChanged();
}

void DioController::applySafeState()
{
    releaseOutputs();
}

void DioController::releaseOutputs()
{
    if (isRunning())
        return;
    releaseLines(uint8_t(m_drivenMask), false);
}

// --- Run ------------------------------------------------------------------------------------

InstrumentController::WorkerFunction DioController::createWorker()
{
    const uint8_t inputs = uint8_t(m_inputMask);
    const uint8_t outputs = uint8_t(m_outputMask);
    const bool keepTimeline = m_restarting;
    m_restarting = false;
    if (!inputs && !outputs) {
        Notifications::instance()->show(Notifications::Warning, tr("Select lines to read or to write first."), {}, {}, {},
                                        instrumentId());
        return {};
    }
    updateResources();   // idle here: the arbiter claims exactly these lines next
    if (!keepTimeline)
        clearTimeline();
    m_readbackErrors = 0;
    m_releaseOnExit->store(false);
    m_live->clear();

    const uint8_t previouslyDriven = uint8_t(m_drivenMask);
    const Live first = liveSnapshot();
    const Clock::time_point origin = m_origin;
    QStringList summary;
    if (inputs)
        summary << tr("reading %1").arg(dio::linesLabel(inputs));
    if (outputs)
        summary << tr("writing %1 (%2)").arg(dio::linesLabel(outputs), patternNames().value(m_pattern));
    const QString status = summary.join(u" · "_s);
    setSummary(tr("DIO %1").arg(ResourceArbiter::dioLinesLabel(inputs | outputs).mid(4)));

    return [this, inputs, outputs, previouslyDriven, first, origin, status, live = m_live,
            release = m_releaseOnExit, dioMutex = m_dioMutex, samples = m_samples](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        ctx.setStatus(status);
        {
            // Lines no longer driven (or now read) become inputs; the OUT lines become outputs.
            std::lock_guard lock(*dioMutex);
            const uint8_t toInput = uint8_t((previouslyDriven | inputs) & ~outputs);
            if (toInput)
                device.dioSetDirection(toInput, false);
            if (outputs)
                device.dioSetDirection(outputs, true);
        }
        ctx.post([this, outputs] {
            m_drivenMask = outputs;
            emit drivenMaskChanged();
        });

        Live current = first;
        const int width = dio::lineCount(outputs);
        const int max = dio::maxValue(width);
        std::vector<int> cycle = dio::sequence(current.pattern, width, current.direction);
        int step = 0;                 // index in `cycle`; -1: the next step starts the cycle
        uint8_t written = 0;
        std::array<int, dio::kLines> mismatches{};
        auto seconds = [origin] { return std::chrono::duration<double>(Clock::now() - origin).count(); };

        auto write = [&](int value, int index) {
            const uint8_t levels = dio::scatter(value, outputs);
            device.dioWrite(levels, outputs);
            written = levels;
            ctx.post([this, levels, value, index] {
                m_writtenLevels = levels;
                m_writtenValue = value;
                m_sequenceStep = index;
                emit writtenChanged();
            });
        };
        auto sample = [&] {
            const uint8_t levels = device.dioRead();
            const double t = seconds();
            m_timeline.append(t, levels);   // LogicBuffer: thread-safe
            m_timeline.setEnd(t);
            samples->add(t, levels);
            ctx.addSamples(1);
            int errors = 0;
            for (int line = 0; line < dio::kLines; ++line) {
                const bool wrong = (outputs & (1 << line)) && ((levels ^ written) & (1 << line));
                mismatches[size_t(line)] = wrong ? mismatches[size_t(line)] + 1 : 0;
                if (mismatches[size_t(line)] >= 2)   // two consecutive samples (spec 2.4)
                    errors |= 1 << line;
            }
            ctx.publish([this, levels, errors, t] {
                const bool changed = levels != m_lineStates || errors != m_readbackErrors || !m_hasReading;
                m_lineStates = levels;
                m_readbackErrors = errors;
                m_hasReading = true;
                m_timelineEnd = t;
                if (changed)
                    emit lineStatesChanged();
                emit timelineAdvanced();
            });
        };
        auto manualValue = [&] { return current.manualValue & max; };

        if (outputs) {
            if (current.pattern == dio::Manual || cycle.empty())
                write(manualValue(), -1);
            else
                write(cycle[0], 0);
        }
        sample();
        ctx.acquisitionDone();

        if (ctx.once()) {
            // Run Once: Manual writes once; a sequence plays one full cycle, one interval per
            // value, then the lines are read once more.
            if (outputs && current.pattern != dio::Manual && !cycle.empty()) {
                for (size_t i = 1; i < cycle.size() && ctx.sleepMs(current.stepMs); ++i) {
                    write(cycle[i], int(i));
                    sample();
                }
                if (ctx.sleepMs(current.stepMs))
                    sample();
            }
        } else {
            Clock::time_point nextStep = Clock::now() + std::chrono::milliseconds(current.stepMs);
            Clock::time_point nextSample = Clock::now() + std::chrono::milliseconds(current.sampleMs);
            // Single pressed while running (ctx.once() turns true) ends the loop after this pass.
            while (!ctx.stopRequested() && !ctx.once()) {
                if (std::optional<Live> changed = live->take()) {
                    const bool patternChanged = changed->pattern != current.pattern
                                                || (changed->pattern == dio::WalkingOnes && changed->direction != current.direction);
                    const bool manualChanged = changed->manualValue != current.manualValue;
                    current = *changed;
                    if (patternChanged) {
                        cycle = dio::sequence(current.pattern, width, current.direction);
                        if (outputs && current.pattern == dio::Manual)
                            write(manualValue(), -1);   // Manual: at once
                        step = -1;                       // sequences: first value at the next step
                        nextStep = Clock::now() + std::chrono::milliseconds(current.stepMs);
                    } else if (manualChanged && outputs && current.pattern == dio::Manual) {
                        write(manualValue(), -1);
                        sample();
                    }
                }
                const Clock::time_point now = Clock::now();
                const bool stepping = outputs && current.pattern != dio::Manual && !cycle.empty();
                if (stepping && now >= nextStep) {
                    step = (step + 1) % int(cycle.size());
                    write(cycle[size_t(step)], step);
                    sample();   // the timeline shows every step
                    nextStep += std::chrono::milliseconds(current.stepMs);
                    if (nextStep < now)
                        nextStep = now + std::chrono::milliseconds(current.stepMs);
                }
                if (now >= nextSample) {
                    sample();
                    ctx.acquisitionDone();
                    nextSample += std::chrono::milliseconds(current.sampleMs);
                    if (nextSample < now)
                        nextSample = now + std::chrono::milliseconds(current.sampleMs);
                }
                const Clock::time_point wake = stepping ? std::min(nextStep, nextSample) : nextSample;
                const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(wake - Clock::now()).count();
                if (!ctx.sleepMs(int(std::clamp<qint64>(wait, 1, kMaximumSleepMs))))
                    break;
            }
        }

        // Stop keeps the outputs driving (ELVISmx). A pause frees them for the instrument that
        // takes the lines: ask the UI thread whether this stop is a pause (the flag is set in
        // the same UI-thread call that requested the stop, so it is known once this runs).
        if (outputs && ctx.stopRequested()) {
            auto answer = std::make_shared<std::promise<bool>>();
            std::future<bool> isPause = answer->get_future();
            ctx.post([answer, release] { answer->set_value(release->load()); });
            if (isPause.wait_for(std::chrono::milliseconds(300)) == std::future_status::ready && isPause.get()) {
                std::lock_guard lock(*dioMutex);
                device.dioSetDirection(outputs, false);
                ctx.post([this] {
                    m_drivenMask = 0;
                    emit drivenMaskChanged();
                });
            }
        }
    };
}

void DioController::onFinished()
{
    m_releaseOnExit->store(false);
    setSummary({});
}

// --- Settings and export --------------------------------------------------------------------

void DioController::saveExtraSettings(QJsonObject& settings) const
{
    settings.insert(u"lineLabels"_s, QJsonArray::fromStringList(m_lineLabels));
}

void DioController::restoreExtraSettings(const QJsonObject& settings)
{
    QStringList labels(dio::kLines, QString());
    const QJsonArray array = settings.value(u"lineLabels"_s).toArray();
    for (int line = 0; line < dio::kLines && line < array.size(); ++line)
        labels[line] = array.at(line).toString();
    if (labels != m_lineLabels) {
        m_lineLabels = labels;
        emit lineLabelsChanged();
    }
}

DataTable DioController::exportTable() const
{
    DataTable table;
    std::vector<double> time;
    std::array<std::vector<double>, dio::kLines> lines;
    {
        std::lock_guard lock(m_samples->mutex);
        time.reserve(m_samples->samples.size());
        for (const auto& [t, levels] : m_samples->samples) {
            time.push_back(t);
            for (int line = 0; line < dio::kLines; ++line)
                lines[size_t(line)].push_back((levels >> line) & 1 ? 1.0 : 0.0);
        }
    }
    table.addColumn(tr("Time"), u"s"_s, std::move(time));
    for (int line = 0; line < dio::kLines; ++line)
        table.addColumn(u"DIO %1"_s.arg(line), {}, std::move(lines[size_t(line)]));
    QStringList directions;
    for (int line = 0; line < dio::kLines; ++line) {
        const LineDirection direction = lineDirection(line);
        directions << u"DIO %1 %2"_s.arg(line).arg(direction == Out ? tr("Out") : direction == In ? tr("In") : tr("unused"));
    }
    table.addMetadata(tr("Directions"), directions.join(u", "_s));
    table.addMetadata(tr("Sample interval"), tr("%1 ms").arg(m_sampleInterval));
    if (m_outputMask) {
        table.addMetadata(tr("Pattern"), patternNames().value(m_pattern));
        if (m_pattern != Manual)
            table.addMetadata(tr("Step interval"), tr("%1 ms").arg(m_stepInterval));
    }
    for (int line = 0; line < dio::kLines; ++line) {
        if (!m_lineLabels[line].isEmpty())
            table.addMetadata(u"DIO %1"_s.arg(line), m_lineLabels[line]);
    }
    return table;
}

void registerDio(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"dio"_s,
        .title = QObject::tr("Digital I/O"),
        .shortTitle = QObject::tr("DIO"),
        .description = QObject::tr("Digital Reader + Writer: DIO 0–7, patterns, timeline"),
        .module = u"MyBench.Instruments.Dio"_s,
        .icon = u"instrument-dio"_s,
        .order = 60,
        .createController = [] { return new DioController; },
    });
}

}  // namespace mybench::instruments
