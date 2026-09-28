#include "dmmcontroller.h"

#include "continuitytone.h"

#include <mybench/dataexport.h>
#include <mybench/devicehub.h>
#include <mybench/notifications.h>

#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

constexpr qsizetype kTrendCapacity = 1'000'000;   // ~2 days at 5 readings/s ("All" window)
constexpr size_t kHistoryCapacity = 1'000'000;

QChar separatorFor(const QString& path)
{
    return path.endsWith(u".csv"_s, Qt::CaseInsensitive) ? u',' : u'\t';
}

QStringList commentHeader(const QString& title, const QStringList& extra)
{
    DataTable table;
    table.title = title;
    table.instrumentId = u"dmm"_s;
    QStringList lines;
    for (const QString& line : DataExport::standardHeader(table))
        lines << u"# "_s + line;
    for (const QString& line : extra)
        lines << u"# "_s + line;
    return lines;
}

}  // namespace

DmmController::DmmController() : InstrumentController(u"dmm"_s)
{
    setRequiredResources(ResourceArbiter::Multimeter);
    m_trend.setMaximumCount(kTrendCapacity);
    m_tone = new ContinuityTone(this);
    refreshRanges();
    connect(DeviceHub::instance(), &DeviceHub::currentChanged, this, &DmmController::refreshRanges);
    connect(this, &InstrumentController::stateChanged, this, &DmmController::updateTone);
}

DmmController::~DmmController()
{
    shutdown();   // the worker lambda captures `this`
    closeLogFile();
}

// --- Settings -------------------------------------------------------------------------------

void DmmController::setSelectedFunction(MeterFunction function)
{
    if (function == m_function || function < DcVoltage || function > Continuity)
        return;
    m_function = function;
    refreshRanges();
    // ELVISmx: every function change goes back to the largest range and clears Null Offset.
    m_rangeIndex = std::max(0, int(m_ranges.size()) - 1);
    m_nullOffset = false;
    m_nullCaptured = false;
    clearDisplay();
    m_trend.clear();
    m_history.clear();
    m_statistics.reset();
    emit functionChanged();
    emit rangeIndexChanged();
    emit autoRangeChanged();   // effectiveAutoRange / rangeSelectable depend on the function
    emit nullOffsetChanged();
    emit statisticsChanged();
    updateTone();
    restart();   // no-op when idle
}

void DmmController::setAutoRange(bool autoRange)
{
    if (autoRange == m_autoRange)
        return;
    m_autoRange = autoRange;
    emit autoRangeChanged();
    restart();
}

void DmmController::setRangeIndex(int index)
{
    index = std::clamp(index, 0, std::max(0, int(m_ranges.size()) - 1));
    if (index == m_rangeIndex)
        return;
    m_rangeIndex = index;
    emit rangeIndexChanged();
    if (!effectiveAutoRange())
        restart();
}

void DmmController::setTrendWindow(int window)
{
    window = std::clamp(window, 0, 3);
    if (window == m_trendWindow)
        return;
    m_trendWindow = window;
    emit trendWindowChanged();
}

void DmmController::setContinuityBeep(bool enabled)
{
    if (enabled == m_continuityBeep)
        return;
    m_continuityBeep = enabled;
    emit continuityBeepChanged();
    updateTone();
}

void DmmController::setAutoStart(bool enabled)
{
    if (enabled == m_autoStart)
        return;
    m_autoStart = enabled;
    emit autoStartChanged();
}

// --- Transient controls ---------------------------------------------------------------------

void DmmController::setNullOffset(bool enabled)
{
    if (enabled == m_nullOffset)
        return;
    // On: arm a capture (the next non-overload reading becomes the null value). Off: raw values.
    m_nullOffset = enabled;
    m_nullCaptured = false;
    emit nullOffsetChanged();
}

void DmmController::setHold(bool hold)
{
    if (hold == m_hold)
        return;
    m_hold = hold;
    emit holdChanged();
}

void DmmController::setRecording(bool recording)
{
    if (recording == m_recording)
        return;
    m_recording = recording;
    if (recording)
        openLogFile();
    else
        closeLogFile();
    emit recordingChanged();
}

void DmmController::setLogFile(const QUrl& file)
{
    if (file == m_logFileUrl)
        return;
    closeLogFile();
    m_logFileUrl = file;
    if (m_recording)
        openLogFile();
    emit logFileChanged();
}

// --- Derived --------------------------------------------------------------------------------

QString DmmController::functionName() const
{
    return mydaq::toString(deviceFunction());
}

QString DmmController::unit() const
{
    return mydaq::unitOf(deviceFunction());
}

QStringList DmmController::rangeLabels() const
{
    if (m_function == Continuity)
        return {tr("No Range")};   // ELVISmx: Continuity has no selectable range
    QStringList labels;
    for (auto it = m_ranges.rbegin(); it != m_ranges.rend(); ++it)   // smallest first
        labels << dmm::rangeLabel(deviceFunction(), *it);
    return labels;
}

QString DmmController::selectedRangeLabel() const
{
    const QStringList labels = rangeLabels();
    return m_rangeIndex >= 0 && m_rangeIndex < labels.size() ? labels[m_rangeIndex] : QString();
}

bool DmmController::toneAvailable() const
{
    return m_tone->available();
}

int DmmController::deviceRangeIndex() const
{
    return std::clamp(int(m_ranges.size()) - 1 - m_rangeIndex, 0, std::max(0, int(m_ranges.size()) - 1));
}

dmm::DisplayFormat DmmController::currentFormat() const
{
    if (m_display.valid)
        return m_lastFormat;
    const double range = m_ranges.empty() ? 0.0 : m_ranges[size_t(deviceRangeIndex())];
    return dmm::displayFormat(deviceFunction(), range);
}

QString DmmController::nullText() const
{
    if (!m_nullOffset)
        return {};
    if (!m_nullCaptured)
        return tr("armed");
    const dmm::DisplayFormat format = currentFormat();
    return dmm::formatNumber(m_nullValue, format) + u' ' + format.unit;
}

double DmmController::trendMinimumSpan() const
{
    return 10.0 * currentFormat().resolution();
}

double DmmController::trendSeconds() const
{
    static constexpr double spans[] = {60.0, 600.0, 3600.0, 0.0};
    return spans[std::clamp(m_trendWindow, 0, 3)];
}

QString DmmController::formatValue(double value, bool extraDigit) const
{
    if (!std::isfinite(value))
        return u"—"_s;
    dmm::DisplayFormat format = currentFormat();
    if (extraDigit)
        ++format.decimals;
    return dmm::formatNumber(value, format) + u' ' + format.unit;
}

void DmmController::refreshRanges()
{
    std::vector<double> ranges;
    if (const mydaq::DevicePtr device = DeviceHub::instance()->device()) {
        try {
            for (const mydaq::DmmRange& range : device->dmmRanges(deviceFunction()))
                ranges.push_back(range.max);
        } catch (const mydaq::DeviceError&) {
            ranges.clear();
        }
    }
    if (ranges.empty())
        ranges = dmm::defaultRanges(deviceFunction());
    std::sort(ranges.begin(), ranges.end(), std::greater<>());
    if (ranges == m_ranges)
        return;
    m_ranges = std::move(ranges);
    const int clamped = std::clamp(m_rangeIndex, 0, std::max(0, int(m_ranges.size()) - 1));
    emit rangesChanged();
    if (clamped != m_rangeIndex) {
        m_rangeIndex = clamped;
        emit rangeIndexChanged();
    }
}

// --- Readings -------------------------------------------------------------------------------

void DmmController::clearDisplay()
{
    m_display = Display();
    m_continuityClosed = false;
    emit readingChanged();
}

void DmmController::applyReading(const Reading& reading)
{
    const mydaq::DmmFunction function = deviceFunction();
    if (reading.rangeIndex < 0 || size_t(reading.rangeIndex) >= m_ranges.size())
        return;
    const double rangeMax = m_ranges[size_t(reading.rangeIndex)];
    const dmm::DisplayFormat format = dmm::displayFormat(function, rangeMax);
    const bool over = reading.overload;

    if (m_nullOffset && !m_nullCaptured && !over) {
        m_nullValue = reading.raw;
        m_nullCaptured = true;
        emit nullOffsetChanged();
    }
    const bool relative = m_nullOffset && m_nullCaptured;
    const double displayed = over ? qQNaN() : dmm::roundToResolution(relative ? reading.raw - m_nullValue : reading.raw, format);

    Display display;
    display.valid = true;
    display.raw = reading.raw;
    display.value = displayed;
    display.overload = over;
    display.rangeLabel = dmm::rangeLabel(function, rangeMax);
    if (over) {
        // ELVISmx: a diode on an open circuit shows OPEN in the readout itself; other functions
        // show +Over and no unit.
        display.text = function == mydaq::DmmFunction::Diode ? u"OPEN"_s : dmm::overloadText(function, reading.raw);
        display.unit.clear();
    } else {
        display.text = dmm::formatNumber(displayed, format);
        display.unit = format.unit;
    }
    // % FS always from the raw reading; full on overload.
    const double fraction = rangeMax > 0 ? reading.raw / rangeMax : 0.0;
    display.fullScale = over ? 1.0 : std::min(1.0, std::abs(fraction));
    display.signedFullScale = over ? (reading.raw < 0 && dmm::isBipolar(function) ? -1.0 : 1.0)
                                   : std::clamp(fraction, -1.0, 1.0);
    if (function == mydaq::DmmFunction::Diode)
        display.diodeStatus = over ? u"OPEN"_s : u"GOOD"_s;

    m_continuityClosed = function == mydaq::DmmFunction::Continuity && !over
                         && std::abs(reading.raw) < dmm::kContinuityThreshold;
    m_lastFormat = format;
    if (!m_hold)
        m_display = display;
    ++m_readingSerial;
    emit readingChanged();

    if (over)
        m_statistics.addOverload();
    else
        m_statistics.add(displayed);
    emit statisticsChanged();

    const double seconds = double(reading.timeMs) / 1000.0;
    m_trend.append(seconds, displayed);   // NaN = gap
    if (m_history.empty())
        m_historyStartMs = reading.timeMs;
    if (m_history.size() >= kHistoryCapacity)
        m_history.erase(m_history.begin(), m_history.begin() + qsizetype(kHistoryCapacity / 10));
    m_history.push_back({double(reading.timeMs - m_historyStartMs) / 1000.0, displayed, over});

    if (m_recording) {
        if (m_log.empty())
            m_logStartMs = reading.timeMs;
        dmm::LogRow row;
        row.time = QDateTime::fromMSecsSinceEpoch(reading.timeMs).toOffsetFromUtc(
                       QDateTime::currentDateTime().offsetFromUtc()).toString(Qt::ISODateWithMs);
        row.elapsed = double(reading.timeMs - m_logStartMs) / 1000.0;
        row.function = functionName();
        row.range = display.rangeLabel;
        row.value = displayed;
        row.unit = unit();
        row.overload = over;
        row.relative = relative;
        row.nullValue = m_nullValue;
        appendLog(row);
    }
    updateTone();
}

void DmmController::updateTone()
{
    const bool active = isRunning() && m_continuityBeep && m_function == Continuity && m_continuityClosed;
    m_tone->setActive(active);
}

void DmmController::resetStatistics()
{
    m_statistics.reset();
    emit statisticsChanged();
}

void DmmController::clearTrend()
{
    m_trend.clear();
}

// --- Log ------------------------------------------------------------------------------------

void DmmController::appendLog(const dmm::LogRow& row)
{
    m_log.push_back(row);
    if (m_logFile && m_logFile->isOpen()) {
        const QString path = m_logFile->fileName();
        m_logFile->write((dmm::logLine(row, separatorFor(path)) + u'\n').toUtf8());
        m_logFile->flush();   // a crash or unplug never loses recorded readings
    }
    emit logChanged();
}

void DmmController::openLogFile()
{
    closeLogFile();
    if (m_logFileUrl.isEmpty())
        return;
    const QString path = DataExport::localPath(m_logFileUrl);
    auto file = std::make_unique<QFile>(path);
    const bool fresh = !file->exists() || file->size() == 0;
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        Notifications::instance()->show(Notifications::Error, tr("Cannot write the log file"),
                                        path + u": "_s + file->errorString(), {}, {}, instrumentId());
        return;
    }
    if (fresh) {
        QString header = commentHeader(title(), {}).join(u'\n') + u'\n';
        header += dmm::logHeader(separatorFor(path)) + u'\n';
        file->write(header.toUtf8());
        file->flush();
    }
    m_logFile = std::move(file);
}

void DmmController::closeLogFile()
{
    if (m_logFile)
        m_logFile->close();
    m_logFile.reset();
}

void DmmController::clearLog()
{
    m_log.clear();
    emit logChanged();
}

bool DmmController::saveLog(const QUrl& file)
{
    const QString path = DataExport::localPath(file);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        Notifications::instance()->show(Notifications::Error, tr("Log not saved"), out.errorString(), {}, {},
                                        instrumentId());
        return false;
    }
    const QChar separator = separatorFor(path);
    QString text = commentHeader(title(), {tr("Readings: %1").arg(m_log.size())}).join(u'\n') + u'\n';
    text += dmm::logHeader(separator) + u'\n';
    for (const dmm::LogRow& row : m_log)
        text += dmm::logLine(row, separator) + u'\n';
    out.write(text.toUtf8());
    if (!out.commit()) {
        Notifications::instance()->show(Notifications::Error, tr("Log not saved"), out.errorString(), {}, {},
                                        instrumentId());
        return false;
    }
    Notifications::instance()->show(Notifications::Success, tr("Log saved"), path, {}, {}, instrumentId());
    return true;
}

void DmmController::autoStartIfEnabled()
{
    if (!m_autoStart || m_autoStartDone || !isIdle() || isPaused() || !canRun())
        return;
    m_autoStartDone = true;
    run();
}

// --- Run ------------------------------------------------------------------------------------

InstrumentController::WorkerFunction DmmController::createWorker()
{
    refreshRanges();
    const mydaq::DmmFunction function = deviceFunction();
    const std::vector<double> ranges = m_ranges;
    if (ranges.empty())
        throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig, tr("no range for %1").arg(functionName()));
    const bool autoRange = effectiveAutoRange();
    const int fixedIndex = deviceRangeIndex();
    setSummary(autoRange ? tr("DMM · %1 · Auto").arg(functionName())
                         : tr("DMM · %1 · %2").arg(functionName(), selectedRangeLabel()));

    return [this, function, ranges, autoRange, fixedIndex](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        ctx.setStatus(tr("Measuring"));
        // Auto: the first reading of a run (and of a function) is taken on the largest range.
        int index = autoRange ? 0 : fixedIndex;
        QElapsedTimer pace;
        while (!ctx.stopRequested()) {
            pace.start();
            mydaq::DmmReading reading;
            bool overload = false;
            int measuredOn = index;
            // Auto: re-read at once on the range that fits, so only settled readings are shown.
            for (int attempt = 1;; ++attempt) {
                measuredOn = index;
                reading = device.dmmRead(function, ranges[size_t(index)]);   // blocking, never interrupted
                ctx.addSamples(1);
                overload = dmm::isOverload(function, ranges[size_t(index)], reading);
                if (!autoRange)
                    break;
                index = dmm::autoRangeIndex(function, ranges, index, std::abs(reading.value), overload);
                if (index == measuredOn || attempt >= dmm::kMaximumAutoRangeReadings || ctx.stopRequested())
                    break;
            }
            const Reading result{reading.value, overload, measuredOn, QDateTime::currentMSecsSinceEpoch()};
            ctx.post([this, result] { applyReading(result); });   // every reading: trend, statistics, log
            ctx.acquisitionDone();
            if (ctx.once())
                break;
            const qint64 remaining = dmm::kMinimumReadingPeriodMs - pace.elapsed();
            if (remaining > 0 && !ctx.sleepMs(int(remaining)))
                break;
        }
    };
}

void DmmController::onFinished()
{
    setSummary({});
    updateTone();
}

DataTable DmmController::exportTable() const
{
    DataTable table;
    std::vector<double> elapsed, values, overloads;
    elapsed.reserve(m_history.size());
    values.reserve(m_history.size());
    overloads.reserve(m_history.size());
    for (const HistoryPoint& point : m_history) {
        elapsed.push_back(point.elapsed);
        values.push_back(point.value);
        overloads.push_back(point.overload ? 1.0 : 0.0);
    }
    table.addColumn(tr("Elapsed"), u"s"_s, std::move(elapsed));
    table.addColumn(functionName(), unit(), std::move(values));
    table.addColumn(tr("Overload"), {}, std::move(overloads));
    table.addMetadata(tr("Function"), functionName());
    table.addMetadata(tr("Mode"), effectiveAutoRange() ? tr("Auto") : tr("Specify Range"));
    table.addMetadata(tr("Range"), effectiveAutoRange() ? m_display.rangeLabel : selectedRangeLabel());
    if (m_historyStartMs > 0)
        table.addMetadata(tr("Start"), QDateTime::fromMSecsSinceEpoch(m_historyStartMs).toString(Qt::ISODateWithMs));
    if (m_nullOffset && m_nullCaptured)
        table.addMetadata(tr("Null offset"), nullText());
    return table;
}

void registerDmm(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"dmm"_s,
        .title = QObject::tr("Multimeter"),
        .shortTitle = QObject::tr("DMM"),
        .description = QObject::tr("DC/AC volts and amps, ohms, diode, continuity"),
        .module = u"MyBench.Instruments.Dmm"_s,
        .icon = u"instrument-dmm"_s,
        .order = 10,
        .createController = [] { return new DmmController; },
    });
}

}  // namespace mybench::instruments
