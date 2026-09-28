#include "logcontroller.h"

#include <mybench/dataexport.h>
#include <mybench/devicehub.h>
#include <mybench/notifications.h>
#include <mybench/units.h>

#include <QDir>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

using logger::LogFileWriter;
using logger::RunningStats;

double nowSeconds() { return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0; }

mydaq::DmmFunction deviceFunction(int index)
{
    static const mydaq::DmmFunction map[] = {mydaq::DmmFunction::DcVoltage, mydaq::DmmFunction::AcVoltage,
                                             mydaq::DmmFunction::DcCurrent, mydaq::DmmFunction::AcCurrent,
                                             mydaq::DmmFunction::Resistance};
    return map[std::clamp(index, 0, 4)];
}

QString durationText(double seconds)
{
    const auto total = qint64(seconds);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    if (hours >= 24)
        return QObject::tr("%1 d %2 h").arg(hours / 24).arg(hours % 24);
    if (hours > 0)
        return QObject::tr("%1 h %2 min").arg(hours).arg(minutes);
    if (minutes > 0)
        return QObject::tr("%1 min").arg(minutes);
    return QObject::tr("%1 s").arg(total);
}

// One channel's statistics as the QML list entry.
QVariantMap statsEntry(const QString& key, const QString& name, const QString& unit, const RunningStats& s,
                       bool overload)
{
    return {{u"key"_s, key},
            {u"name"_s, name},
            {u"unit"_s, unit},
            {u"last"_s, s.last},
            {u"minimum"_s, s.minimum},
            {u"maximum"_s, s.maximum},
            {u"mean"_s, s.count ? s.mean : qQNaN()},
            {u"deviation"_s, s.standardDeviation()},
            {u"count"_s, double(s.count)},
            {u"overload"_s, overload}};
}

}  // namespace

LogController::LogController() : InstrumentController(u"log"_s)
{
    setSupportsSingle(false);
    m_folder = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(u"MYBENCH Logs"_s);
    m_ai0.setMaximumCount(kChartSamples);
    m_ai1.setMaximumCount(kChartSamples);
    m_dmm.setMaximumCount(kChartSamples / 10);
    updateResources();
}

LogController::~LogController()
{
    shutdown();   // the worker lambda captures `this`
}

// --- Settings ---------------------------------------------------------------------------------

void LogController::updateResources()
{
    ResourceArbiter::Resources resources;
    if (m_ai0Enabled || m_ai1Enabled)
        resources |= ResourceArbiter::AnalogInput;
    if (m_dmmEnabled)
        resources |= ResourceArbiter::Multimeter;
    setRequiredResources(resources);
}

void LogController::restartIfRunning()
{
    if (!isRunning())
        updateResources();
    restart();
}

void LogController::setAi0Enabled(bool enabled)
{
    if (enabled == m_ai0Enabled)
        return;
    m_ai0Enabled = enabled;
    emit channelsChanged();
    restartIfRunning();
}

void LogController::setAi1Enabled(bool enabled)
{
    if (enabled == m_ai1Enabled)
        return;
    m_ai1Enabled = enabled;
    emit channelsChanged();
    restartIfRunning();
}

void LogController::setDmmEnabled(bool enabled)
{
    if (enabled == m_dmmEnabled)
        return;
    m_dmmEnabled = enabled;
    emit channelsChanged();
    restartIfRunning();
}

void LogController::setDmmFunction(int function)
{
    function = std::clamp(function, 0, int(logger::dmmFunctions().size()) - 1);
    if (function == m_dmmFunction)
        return;
    m_dmmFunction = function;
    m_dmmRange = 0;   // Auto after a function change
    emit dmmChanged();
    if (m_dmmEnabled)
        restartIfRunning();
}

void LogController::setDmmRange(int range)
{
    range = std::clamp(range, 0, int(logger::dmmFunctions()[size_t(m_dmmFunction)].ranges.size()));
    if (range == m_dmmRange)
        return;
    m_dmmRange = range;
    emit dmmChanged();
    if (m_dmmEnabled)
        restartIfRunning();
}

void LogController::setSamplingRate(double rate)
{
    rate = std::clamp(std::round(rate * 100.0) / 100.0, 1.0, 1000.0);
    if (!std::isfinite(rate) || rate == m_sampleRate)
        return;
    m_sampleRate = rate;
    emit samplingRateChanged();
    if (m_ai0Enabled || m_ai1Enabled)
        restartIfRunning();
}

void LogController::setMaximum(double volts)
{
    volts = std::clamp(volts, -10.0, 10.0);
    if (!std::isfinite(volts) || volts == m_maximum)
        return;
    const double oldRange = hardwareRange();
    m_maximum = volts;
    emit limitsChanged();
    if (hardwareRange() != oldRange)
        restartIfRunning();
}

void LogController::setMinimum(double volts)
{
    volts = std::clamp(volts, -10.0, 10.0);
    if (!std::isfinite(volts) || volts == m_minimum)
        return;
    const double oldRange = hardwareRange();
    m_minimum = volts;
    emit limitsChanged();
    if (hardwareRange() != oldRange)
        restartIfRunning();
}

void LogController::setAutoScale(bool autoScale)
{
    if (autoScale == m_autoScale)
        return;
    m_autoScale = autoScale;
    emit autoScaleChanged();
}

void LogController::setFolder(const QString& folder)
{
    const QString cleaned = folder.trimmed().isEmpty() ? QString() : QDir::cleanPath(folder.trimmed());
    if (cleaned.isEmpty() || cleaned == m_folder)
        return;
    m_folder = cleaned;
    emit fileSettingsChanged();
}

void LogController::setFolderUrl(const QUrl& url)
{
    setFolder(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

QUrl LogController::folderUrl() const { return QUrl::fromLocalFile(m_folder); }

void LogController::setBaseName(const QString& name)
{
    if (name == m_baseName)
        return;
    m_baseName = name;
    emit fileSettingsChanged();
}

void LogController::setAppendTimestamp(bool append)
{
    if (append == m_appendTimestamp)
        return;
    m_appendTimestamp = append;
    emit fileSettingsChanged();
}

void LogController::setFileFormat(FileFormat format)
{
    if (format == m_fileFormat)
        return;
    m_fileFormat = format;
    emit fileSettingsChanged();
}

void LogController::setFileLayout(FileLayout layout)
{
    if (layout == m_fileLayout)
        return;
    m_fileLayout = layout;
    emit fileSettingsChanged();
}

void LogController::setAppendToExisting(bool append)
{
    if (append == m_appendToExisting)
        return;
    m_appendToExisting = append;
    emit fileSettingsChanged();
}

void LogController::setTimestampColumn(bool enabled)
{
    if (enabled == m_timestampColumn)
        return;
    m_timestampColumn = enabled;
    emit fileSettingsChanged();
}

void LogController::setRecordOnRun(bool record)
{
    if (record == m_recordOnRun)
        return;
    m_recordOnRun = record;
    emit fileSettingsChanged();
}

void LogController::setStopAfterEnabled(bool enabled)
{
    if (enabled == m_stopAfterEnabled)
        return;
    m_stopAfterEnabled = enabled;
    emit fileSettingsChanged();
}

void LogController::setStopAfterMinutes(double minutes)
{
    minutes = std::clamp(minutes, 0.01, 100000.0);
    if (!std::isfinite(minutes) || minutes == m_stopAfterMinutes)
        return;
    m_stopAfterMinutes = minutes;
    emit fileSettingsChanged();
}

// --- Derived ----------------------------------------------------------------------------------

QStringList LogController::dmmFunctionNames() const
{
    QStringList names;
    for (const auto& f : logger::dmmFunctions())
        names.append(tr(f.name));
    return names;
}

QStringList LogController::dmmRangeNames() const
{
    QStringList names{tr("Auto")};
    for (const auto& r : logger::dmmFunctions()[size_t(m_dmmFunction)].ranges)
        names.append(QString::fromUtf8(r.label));
    return names;
}

QString LogController::dmmUnit() const
{
    return QString::fromUtf8(logger::dmmFunctions()[size_t(m_dmmFunction)].unit);
}

double LogController::hardwareRange() const
{
    return std::max(std::abs(m_maximum), std::abs(m_minimum)) <= 2.0 ? 2.0 : 10.0;
}

double LogController::coercedRate() const
{
    if (const mydaq::DevicePtr device = DeviceHub::instance()->device())
        return device->coerceAiRate(m_sampleRate);
    return 100e6 / std::round(100e6 / m_sampleRate);
}

QString LogController::chartCapacity() const
{
    return durationText(double(kChartSamples) / m_sampleRate);
}

QString LogController::fileExtension() const
{
    if (m_fileLayout == ElvismxLayout)
        return u"txt"_s;   // not .lvm: the content is not LabVIEW Measurement format
    return m_fileFormat == CommaSeparated ? u"csv"_s : u"tsv"_s;
}

QString LogController::nextFilePath() const
{
    return LogFileWriter::uniquePath(m_folder, m_baseName, m_appendTimestamp, fileExtension(),
                                     QDateTime::currentDateTime(), m_fileLayout == ElvismxLayout && m_appendToExisting);
}

// --- Recording --------------------------------------------------------------------------------

void LogController::setRecordingState(bool recording, bool paused, bool pending)
{
    if (recording == m_recording && paused == m_recordPaused && pending == m_recordPending)
        return;
    m_recording = recording;
    m_recordPaused = paused;
    m_recordPending = pending;
    emit recordingChanged();
}

QStringList LogController::headerComments() const
{
    if (m_fileLayout == ElvismxLayout)
        return {};   // no `#` lines in the compatible layout
    DataTable table;
    table.title = title();
    table.instrumentId = instrumentId();
    QStringList lines = DataExport::standardHeader(table);
    QStringList channels;
    if (m_ai0Enabled)
        channels << u"AI 0"_s;
    if (m_ai1Enabled)
        channels << u"AI 1"_s;
    if (!channels.isEmpty()) {
        lines << tr("Sample rate: %1 S/s").arg(DataExport::formatNumber(coercedRate()));
        lines << tr("Input limits: %1 V to %2 V (range ±%3 V)")
                     .arg(DataExport::formatNumber(m_minimum), DataExport::formatNumber(m_maximum))
                     .arg(hardwareRange());
    }
    if (m_dmmEnabled) {
        channels << tr("DMM %1 (%2)").arg(dmmFunctionNames().value(m_dmmFunction), dmmRangeNames().value(m_dmmRange));
    }
    lines << tr("Channels: %1").arg(channels.join(u", "_s));
    lines << tr("Time: seconds since the start of the recording");
    return lines;
}

LogController::Command LogController::openCommand()
{
    return {Command::Open, nextFilePath(), headerComments()};
}

void LogController::startRecording()
{
    if (m_recording || m_recordPending)
        return;
    if (isRunning()) {
        m_commands->push(openCommand());
        setRecordingState(false, false, true);
        return;
    }
    m_armRecord = true;
    run();
    m_armRecord = false;
}

void LogController::stopRecording()
{
    if (!m_recording && !m_recordPending)
        return;
    m_commands->push({Command::Close, {}, {}});
}

void LogController::setRecordPaused(bool paused)
{
    if (!m_recording || paused == m_recordPaused)
        return;
    m_commands->push({paused ? Command::Pause : Command::Resume, {}, {}});
}

void LogController::clearChart()
{
    if (isRunning()) {
        m_commands->push({Command::Clear, {}, {}});
        return;
    }
    m_ai0.clear();
    m_ai1.clear();
    m_dmm.clear();
    m_channelStats.clear();
    emit statsChanged();
}

// --- Run --------------------------------------------------------------------------------------

InstrumentController::WorkerFunction LogController::createWorker()
{
    if (!m_ai0Enabled && !m_ai1Enabled && !m_dmmEnabled)
        throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig, tr("enable at least one channel to log"));
    if (!(m_minimum < m_maximum))
        throw InvalidInput(tr("Minimum must be below Maximum."));
    updateResources();

    struct Snapshot {
        std::vector<mydaq::AiChannel> channels;
        double range = 10;
        double rate = 10;
        bool dmm = false;
        mydaq::DmmFunction dmmFunction = mydaq::DmmFunction::DcVoltage;
        std::vector<logger::DmmRangeInfo> dmmRanges;
        int dmmRange = 0;   // 0 = auto
        QString dmmName;
        QString dmmUnit;
        bool timestampColumn = false;
        char delimiter = '\t';
        double stopAfter = 0;   // s, 0 = no limit
        bool elvismx = false;   // ELVISmx-compatible file layout
        bool append = false;
    } snap;
    if (m_ai0Enabled)
        snap.channels.push_back(mydaq::AiChannel::AI0);
    if (m_ai1Enabled)
        snap.channels.push_back(mydaq::AiChannel::AI1);
    snap.range = hardwareRange();
    snap.rate = m_sampleRate;
    snap.dmm = m_dmmEnabled;
    snap.dmmFunction = deviceFunction(m_dmmFunction);
    snap.dmmRanges = logger::dmmFunctions()[size_t(m_dmmFunction)].ranges;
    snap.dmmRange = m_dmmRange;
    snap.dmmName = dmmFunctionNames().value(m_dmmFunction);
    snap.dmmUnit = dmmUnit();
    snap.timestampColumn = m_timestampColumn;
    snap.delimiter = m_fileFormat == CommaSeparated ? ',' : '\t';
    snap.stopAfter = m_stopAfterEnabled ? m_stopAfterMinutes * 60.0 : 0.0;
    snap.elvismx = m_fileLayout == ElvismxLayout;
    snap.append = snap.elvismx && m_appendToExisting;
    if (snap.elvismx)
        snap.delimiter = '\t';

    m_commands->takeAll();
    if (m_armRecord || m_recordOnRun) {
        m_commands->push(openCommand());
        setRecordingState(false, false, true);
    }
    m_runSeconds = 0;
    m_recordSeconds = 0;
    emit recordProgressChanged();

    return [this, snap, commands = m_commands](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        const bool analog = !snap.channels.empty();
        const size_t nch = snap.channels.size();

        // Column header of the file.
        QStringList columns{tr("Time (s)")};
        if (snap.timestampColumn)
            columns << tr("Timestamp");
        for (mydaq::AiChannel ch : snap.channels)
            columns << mydaq::toString(ch) + u" (V)"_s;
        if (snap.dmm) {
            if (analog)
                columns << tr("DMM time (s)");
            columns << tr("DMM %1 (%2)").arg(snap.dmmName, snap.dmmUnit);
        }
        if (snap.elvismx) {   // Time / <dev>/ai<n> pairs per channel; the DMM is not written
            QStringList names;
            for (mydaq::AiChannel ch : snap.channels)
                names << (ch == mydaq::AiChannel::AI0 ? u"ai0"_s : u"ai1"_s);
            columns = logger::elvismxHeader(u"myDAQ1"_s, names);
        }

        std::unique_ptr<mydaq::AiTask> task;
        double rate = snap.rate;
        if (analog) {
            mydaq::AiConfig config;
            for (mydaq::AiChannel ch : snap.channels)
                config.channels.push_back({ch, snap.range});
            config.sampleRate = snap.rate;
            config.mode = mydaq::SampleMode::Continuous;
            config.samplesPerChannel = std::max<int64_t>(1000, int64_t(10 * snap.rate));   // 10 s buffer
            task = device.createAiTask(config);
            rate = task->sampleRate();
            ctx.setSampleRate(rate);
        }
        const double t0 = nowSeconds();
        const double dx = 1.0 / rate;
        mybench::plot::TraceBuffer* buffers[2] = {nullptr, nullptr};
        for (size_t c = 0; c < nch; ++c) {
            buffers[c] = snap.channels[c] == mydaq::AiChannel::AI0 ? &m_ai0 : &m_ai1;
            buffers[c]->startUniform(t0, dx);
        }
        for (mybench::plot::TraceBuffer* b : {&m_ai0, &m_ai1}) {
            if (b != buffers[0] && b != buffers[1])
                b->clear();
        }
        m_dmm.clear();
        if (task)
            task->start();

        std::vector<RunningStats> stats(nch);
        RunningStats dmmStats;
        bool dmmOverload = false;
        int dmmIndex = snap.dmmRange > 0 ? snap.dmmRange - 1 : int(snap.dmmRanges.size()) - 1;   // Auto starts on the largest

        LogFileWriter writer;
        bool paused = false;
        int64_t sampleIndex = 0;          // AI samples per channel acquired
        int64_t recordStartIndex = 0;
        double recordStartEpoch = 0;
        std::deque<std::pair<double, double>> pendingDmm;   // (time since record start, value)
        double lastPublish = 0;
        bool finishedByLimit = false;

        auto postState = [&](bool recording, bool isPaused, const QString& file) {
            ctx.post([this, recording, isPaused, file] {
                if (!file.isEmpty())
                    m_recordFile = file;
                setRecordingState(recording, isPaused, false);
            });
        };
        // A DMM reading that has no AI sample to share a row with gets a row of its own with
        // empty AI cells (its own time); DMM-only logs are made of such rows. Every queued
        // reading is written eventually, also when the AI rate is below the DMM rate.
        auto writeDmmRows = [&]() {
            while (!snap.elvismx && writer.isOpen() && !paused && !pendingDmm.empty()) {
                const auto [dt, value] = pendingDmm.front();
                pendingDmm.pop_front();
                writer.addNumber(dt);
                if (snap.timestampColumn)
                    writer.addText(QDateTime::fromMSecsSinceEpoch(qint64(std::llround((dt + recordStartEpoch) * 1000)))
                                       .toString(Qt::ISODateWithMs));
                for (size_t c = 0; c < nch; ++c)
                    writer.addEmpty();
                if (analog)
                    writer.addNumber(dt);
                writer.addNumber(value);
                writer.endRow();
            }
        };

        auto closeFile = [&](bool notify) {
            if (!writer.isOpen())
                return;
            writeDmmRows();
            const QString path = writer.path();
            const int64_t rows = writer.rows();
            writer.close();
            postState(false, false, path);
            if (notify)
                ctx.post([path, rows] {
                    Notifications::instance()->notify(Notifications::Success, tr("Recording saved"),
                                                      tr("%n row(s) in %1", nullptr, int(std::min<int64_t>(rows, INT_MAX)))
                                                          .arg(QDir::toNativeSeparators(path)));
                });
        };
        auto recordedSeconds = [&]() {
            if (!writer.isOpen())
                return 0.0;
            return analog ? double(sampleIndex - recordStartIndex) / rate : nowSeconds() - recordStartEpoch;
        };

        while (!ctx.stopRequested()) {
            // Commands from the UI thread, in order.
            for (Command& command : commands->takeAll()) {
                switch (command.kind) {
                case Command::Open: {
                    closeFile(true);
                    recordStartIndex = sampleIndex;
                    recordStartEpoch = analog ? t0 + double(sampleIndex) / rate : nowSeconds();
                    QStringList comments = command.comments;
                    if (!snap.elvismx)
                        comments << tr("Start: %1").arg(QDateTime::fromMSecsSinceEpoch(qint64(recordStartEpoch * 1000))
                                                        .toString(Qt::ISODateWithMs));
                    QString error;
                    if (snap.elvismx && !analog) {
                        ctx.post([this] {
                            setRecordingState(false, false, false);
                            reportWarning(tr("The ELVISmx-compatible layout logs AI channels only: enable AI 0 or AI 1."));
                        });
                        break;
                    }
                    writer.setLineEnding(snap.elvismx ? "\r\n" : "\n");
                    writer.setAppend(snap.append);
                    if (!writer.open(command.path, comments, columns, snap.delimiter, &error)) {
                        const QString path = command.path;
                        ctx.post([this, path, error] {
                            setRecordingState(false, false, false);
                            reportWarning(tr("Could not create %1 (%2): no data was saved.")
                                              .arg(QDir::toNativeSeparators(path), error));
                        });
                        break;
                    }
                    paused = false;
                    pendingDmm.clear();
                    postState(true, false, command.path);
                    break;
                }
                case Command::Close:
                    closeFile(true);
                    ctx.post([this] { setRecordingState(false, false, false); });
                    break;
                case Command::Pause:
                case Command::Resume:
                    if (writer.isOpen()) {
                        paused = command.kind == Command::Pause;
                        postState(true, paused, {});
                    }
                    break;
                case Command::Clear:
                    for (size_t c = 0; c < nch; ++c) {
                        buffers[c]->startUniform(t0 + double(sampleIndex) * dx, dx);
                        stats[c] = {};
                    }
                    m_dmm.clear();
                    dmmStats = {};
                    break;
                }
            }

            // DMM reading (blocks for the conversion time), else a short wait for AI samples.
            if (snap.dmm) {
                const mydaq::DmmReading reading = device.dmmRead(snap.dmmFunction, snap.dmmRanges[size_t(dmmIndex)].max);
                const double t = nowSeconds();
                // Auto: the next range comes straight from this reading (as the DMM instrument);
                // every reading is kept, also one taken on a range that is then left.
                if (snap.dmmRange == 0)
                    dmmIndex = logger::autoRange(snap.dmmRanges, dmmIndex, reading.value, reading.overload);
                {
                    dmmOverload = reading.overload;
                    const double value = reading.overload ? qQNaN() : reading.value;
                    if (!reading.overload) {
                        dmmStats.add(value);
                        m_dmm.append(t, value);
                    }
                    if (writer.isOpen() && !paused && !snap.elvismx)
                        pendingDmm.emplace_back(t - recordStartEpoch, value);
                }
            } else if (!ctx.sleepMs(50)) {
                break;
            }

            // Analog samples.
            if (task) {
                const std::vector<std::vector<double>> data = task->read(-1, 0);
                const size_t n = data.empty() ? 0 : data[0].size();
                if (n > 0) {
                    for (size_t c = 0; c < nch; ++c) {
                        buffers[c]->appendSamples(data[c]);
                        for (double v : data[c])
                            stats[c].add(v);
                    }
                    if (writer.isOpen() && !paused) {
                        for (size_t i = 0; i < n; ++i) {
                            const int64_t k = sampleIndex + int64_t(i);
                            if (snap.elvismx) {
                                // Per channel: absolute time of the sample, then its value.
                                const QByteArray time = logger::elvismxTime(qint64(std::llround((t0 + double(k) / rate) * 1e6))).toLatin1();
                                for (size_t c = 0; c < nch; ++c) {
                                    writer.addRaw(time);
                                    writer.addRaw(logger::elvismxValue(data[c][i]).toLatin1());
                                }
                                writer.endRow();
                                continue;
                            }
                            writer.addNumber(double(k - recordStartIndex) / rate);
                            if (snap.timestampColumn)
                                writer.addText(QDateTime::fromMSecsSinceEpoch(qint64(std::llround((t0 + double(k) / rate) * 1000)))
                                                   .toString(Qt::ISODateWithMs));
                            for (size_t c = 0; c < nch; ++c)
                                writer.addNumber(data[c][i]);
                            if (snap.dmm) {
                                if (!pendingDmm.empty()) {
                                    writer.addNumber(pendingDmm.front().first);
                                    writer.addNumber(pendingDmm.front().second);
                                    pendingDmm.pop_front();
                                } else {
                                    writer.addEmpty();
                                    writer.addEmpty();
                                }
                            }
                            writer.endRow();
                        }
                        writeDmmRows();   // readings beyond the rows just written (AI rate below the DMM rate)
                    }
                    sampleIndex += int64_t(n);
                    ctx.addSamples(qint64(n * nch));
                    ctx.acquisitionDone();
                } else {
                    writeDmmRows();   // no AI sample this pass: the readings get rows of their own
                }
            } else if (snap.dmm) {
                ctx.addSamples(1);
                ctx.acquisitionDone();
                // DMM only: one row per reading.
                writeDmmRows();
            }

            if (writer.isOpen()) {
                QString error;
                if (!writer.flush(&error)) {
                    const QString path = writer.path();
                    ctx.post([this, path, error] {
                        setRecordingState(false, false, false);
                        reportWarning(tr("Writing %1 failed (%2): recording stopped.").arg(QDir::toNativeSeparators(path), error));
                    });
                }
                if (snap.stopAfter > 0 && recordedSeconds() >= snap.stopAfter) {
                    closeFile(true);
                    finishedByLimit = true;
                    break;
                }
            }

            // Display (at most 10 times per second).
            const double now = nowSeconds();
            if (now - lastPublish >= 0.1) {
                lastPublish = now;
                QVariantList entries;
                for (size_t c = 0; c < nch; ++c) {
                    const bool ai0 = snap.channels[c] == mydaq::AiChannel::AI0;
                    entries.append(statsEntry(ai0 ? u"ai0"_s : u"ai1"_s, mydaq::toString(snap.channels[c]), u"V"_s,
                                              stats[c], std::abs(stats[c].last) >= snap.range * 1.049));
                }
                if (snap.dmm)
                    entries.append(statsEntry(u"dmm"_s, tr("DMM"), snap.dmmUnit, dmmStats, dmmOverload));
                const double bytes = double(writer.bytes());
                const double rows = double(writer.rows());
                const double recorded = recordedSeconds();
                const double acquired = analog ? double(sampleIndex) / rate : now - t0;
                ctx.publish([this, entries, bytes, rows, recorded, acquired] {
                    m_channelStats = entries;
                    emit statsChanged();
                    if (m_recording) {
                        m_recordBytes = bytes;
                        m_recordRows = rows;
                        m_recordSeconds = recorded;
                    }
                    m_runSeconds = acquired;
                    emit recordProgressChanged();
                });
                ctx.setStatus(writer.isOpen() ? (paused ? tr("Recording paused") : tr("Recording")) : tr("Monitoring"));
            }
        }
        closeFile(true);
        if (finishedByLimit)
            ctx.post([] {
                Notifications::instance()->notify(Notifications::Info, tr("Data Logger finished"),
                                                  tr("The recording reached its duration limit."));
            });
    };
}

void LogController::onFinished()
{
    m_armRecord = false;
    setRecordingState(false, false, false);
}

DataTable LogController::exportTable() const
{
    DataTable table;
    table.title = title();
    table.instrumentId = instrumentId();
    table.addMetadata(tr("Sampling rate"), Units::format(m_sampleRate, u"S/s"_s));
    table.addMetadata(tr("Input limits"), tr("%1 V to %2 V").arg(m_minimum).arg(m_maximum));
    double origin = qQNaN();
    for (const auto& [buffer, name] : {std::pair{&m_ai0, u"AI 0"_s}, std::pair{&m_ai1, u"AI 1"_s}}) {
        const plot::TraceSnapshot s = buffer->snapshot();
        if (!s || s->empty())
            continue;
        if (std::isnan(origin)) {
            origin = s->xAt(0);
            std::vector<double> time(s->size());
            for (size_t i = 0; i < s->size(); ++i)
                time[i] = s->xAt(i) - origin;
            table.addColumn(tr("Time"), u"s"_s, std::move(time));
        }
        std::vector<double> values(s->size());
        for (size_t i = 0; i < s->size(); ++i)
            values[i] = s->yAt(i);
        table.addColumn(name, u"V"_s, std::move(values));
    }
    const plot::TraceSnapshot d = m_dmm.snapshot();
    if (d && !d->empty()) {
        if (std::isnan(origin))
            origin = d->xAt(0);
        std::vector<double> time(d->size());
        std::vector<double> values(d->size());
        for (size_t i = 0; i < d->size(); ++i) {
            time[i] = d->xAt(i) - origin;
            values[i] = d->yAt(i);
        }
        table.addColumn(tr("DMM time"), u"s"_s, std::move(time));
        table.addColumn(tr("DMM %1").arg(dmmFunctionNames().value(m_dmmFunction)), dmmUnit(), std::move(values));
    }
    if (!std::isnan(origin))
        table.addMetadata(tr("Start"), QDateTime::fromMSecsSinceEpoch(qint64(origin * 1000)).toString(Qt::ISODateWithMs));
    return table;
}

void registerLog(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"log"_s,
        .title = QObject::tr("Data Logger"),
        .shortTitle = QObject::tr("Log"),
        .description = QObject::tr("Long AI 0/AI 1 (and DMM) recordings to a file, 1–1000 S/s"),
        .module = u"MyBench.Instruments.Log"_s,
        .icon = u"qrc:/qt/qml/MyBench/Instruments/Log/assets/instrument-log.svg"_s,
        .order = 80,
        .createController = [] { return new LogController; },
    });
}

}  // namespace mybench::instruments
