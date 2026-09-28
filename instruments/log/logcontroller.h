// LogController: the Data Logger (docs/instruments/log.md, help docs/help/log.md), MYBENCH's
// version of the ELVISmx "Data Logger" featured application.
//
// Run acquires AI 0 / AI 1 continuously at 1..1000 S/s (and optionally the isolated DMM at its
// own pace) into a growing chart recording (TraceBuffer min/max pyramid: hours of data, any zoom
// in O(pixels)) with per-channel statistics. Record streams the same samples to a delimited text
// file while running (header with metadata, rows flushed after every block, synced every 5 s:
// crash-safe); Pause suspends writing; the file is closed by Stop recording, Stop or the
// optional duration limit.
#pragma once

#include "logfile.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>
#include <mybench/worker.h>

#include <QDateTime>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

namespace mybench::instruments {

class LogController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings ---------------------------------------------------------------------------
    Q_PROPERTY(bool ai0Enabled READ ai0Enabled WRITE setAi0Enabled NOTIFY channelsChanged)
    Q_PROPERTY(bool ai1Enabled READ ai1Enabled WRITE setAi1Enabled NOTIFY channelsChanged)
    Q_PROPERTY(bool dmmEnabled READ dmmEnabled WRITE setDmmEnabled NOTIFY channelsChanged)
    Q_PROPERTY(int dmmFunction READ dmmFunction WRITE setDmmFunction NOTIFY dmmChanged)
    Q_PROPERTY(int dmmRange READ dmmRange WRITE setDmmRange NOTIFY dmmChanged)   // 0 = Auto, n = nth range
    Q_PROPERTY(double samplingRate READ samplingRate WRITE setSamplingRate NOTIFY samplingRateChanged)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY limitsChanged)
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY limitsChanged)
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale NOTIFY autoScaleChanged)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY fileSettingsChanged)
    Q_PROPERTY(QString baseName READ baseName WRITE setBaseName NOTIFY fileSettingsChanged)
    Q_PROPERTY(bool appendTimestamp READ appendTimestamp WRITE setAppendTimestamp NOTIFY fileSettingsChanged)
    Q_PROPERTY(FileFormat fileFormat READ fileFormat WRITE setFileFormat NOTIFY fileSettingsChanged)
    // NativeLayout: MYBENCH's own file (log.md 4). ElvismxLayout: the ELVISmx Data Logger layout
    // (log.md 8.2): `Time`/`myDAQ1/ai0` header row, absolute date-time + E-notation cells,
    // TAB, CR LF, `.txt`; AI channels only. appendToExisting adds to the named file (ELVISmx does).
    Q_PROPERTY(FileLayout fileLayout READ fileLayout WRITE setFileLayout NOTIFY fileSettingsChanged)
    Q_PROPERTY(bool appendToExisting READ appendToExisting WRITE setAppendToExisting NOTIFY fileSettingsChanged)
    Q_PROPERTY(bool timestampColumn READ timestampColumn WRITE setTimestampColumn NOTIFY fileSettingsChanged)
    Q_PROPERTY(bool recordOnRun READ recordOnRun WRITE setRecordOnRun NOTIFY fileSettingsChanged)
    Q_PROPERTY(bool stopAfterEnabled READ stopAfterEnabled WRITE setStopAfterEnabled NOTIFY fileSettingsChanged)
    Q_PROPERTY(double stopAfterMinutes READ stopAfterMinutes WRITE setStopAfterMinutes NOTIFY fileSettingsChanged)

    // --- Derived ------------------------------------------------------------------------------
    Q_PROPERTY(QStringList dmmFunctionNames READ dmmFunctionNames CONSTANT)
    Q_PROPERTY(QStringList dmmRangeNames READ dmmRangeNames NOTIFY dmmChanged)   // "Auto", "60 V"...
    Q_PROPERTY(QString dmmUnit READ dmmUnit NOTIFY dmmChanged)
    Q_PROPERTY(double hardwareRange READ hardwareRange NOTIFY limitsChanged)     // ±2 or ±10 V
    Q_PROPERTY(double coercedRate READ coercedRate NOTIFY samplingRateChanged)
    Q_PROPERTY(QString chartCapacity READ chartCapacity NOTIFY samplingRateChanged)   // "5 h 33 min"
    Q_PROPERTY(QString nextFilePath READ nextFilePath NOTIFY fileSettingsChanged)
    Q_PROPERTY(QString fileExtension READ fileExtension NOTIFY fileSettingsChanged)

    // --- Recording state and results (not settings) ---------------------------------------
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool recordPaused READ recordPaused NOTIFY recordingChanged)
    Q_PROPERTY(bool recordPending READ recordPending NOTIFY recordingChanged)   // Record pressed, file not open yet
    Q_PROPERTY(QString recordFile READ recordFile NOTIFY recordingChanged)      // current (or last) file
    Q_PROPERTY(double recordBytes READ recordBytes NOTIFY recordProgressChanged)
    Q_PROPERTY(double recordRows READ recordRows NOTIFY recordProgressChanged)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY recordProgressChanged)   // recorded time
    Q_PROPERTY(double runSeconds READ runSeconds NOTIFY recordProgressChanged)         // acquired time
    // [{ key: "ai0", name: "AI 0", unit: "V", last, minimum, maximum, mean, deviation, count, overload }]
    Q_PROPERTY(QVariantList channelStats READ channelStats NOTIFY statsChanged)
    Q_PROPERTY(mybench::plot::TraceBuffer* ai0 READ ai0 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* ai1 READ ai1 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* dmm READ dmm CONSTANT)

public:
    enum FileFormat { TabSeparated, CommaSeparated };
    Q_ENUM(FileFormat)
    enum FileLayout { NativeLayout, ElvismxLayout };
    Q_ENUM(FileLayout)

    LogController();
    ~LogController() override;

    static constexpr qsizetype kChartSamples = 20'000'000;   // per AI channel (160 MB)

    bool ai0Enabled() const { return m_ai0Enabled; }
    void setAi0Enabled(bool enabled);
    bool ai1Enabled() const { return m_ai1Enabled; }
    void setAi1Enabled(bool enabled);
    bool dmmEnabled() const { return m_dmmEnabled; }
    void setDmmEnabled(bool enabled);
    int dmmFunction() const { return m_dmmFunction; }
    void setDmmFunction(int function);
    int dmmRange() const { return m_dmmRange; }
    void setDmmRange(int range);
    double samplingRate() const { return m_sampleRate; }
    void setSamplingRate(double rate);
    double maximum() const { return m_maximum; }
    void setMaximum(double volts);
    double minimum() const { return m_minimum; }
    void setMinimum(double volts);
    bool autoScale() const { return m_autoScale; }
    void setAutoScale(bool autoScale);
    QString folder() const { return m_folder; }
    void setFolder(const QString& folder);
    QString baseName() const { return m_baseName; }
    void setBaseName(const QString& name);
    bool appendTimestamp() const { return m_appendTimestamp; }
    void setAppendTimestamp(bool append);
    FileFormat fileFormat() const { return m_fileFormat; }
    void setFileFormat(FileFormat format);
    FileLayout fileLayout() const { return m_fileLayout; }
    void setFileLayout(FileLayout layout);
    bool appendToExisting() const { return m_appendToExisting; }
    void setAppendToExisting(bool append);
    bool timestampColumn() const { return m_timestampColumn; }
    void setTimestampColumn(bool enabled);
    bool recordOnRun() const { return m_recordOnRun; }
    void setRecordOnRun(bool record);
    bool stopAfterEnabled() const { return m_stopAfterEnabled; }
    void setStopAfterEnabled(bool enabled);
    double stopAfterMinutes() const { return m_stopAfterMinutes; }
    void setStopAfterMinutes(double minutes);

    QStringList dmmFunctionNames() const;
    QStringList dmmRangeNames() const;
    QString dmmUnit() const;
    double hardwareRange() const;
    double coercedRate() const;
    QString chartCapacity() const;
    QString nextFilePath() const;
    QString fileExtension() const;

    bool recording() const { return m_recording; }
    bool recordPaused() const { return m_recordPaused; }
    bool recordPending() const { return m_recordPending; }
    QString recordFile() const { return m_recordFile; }
    double recordBytes() const { return m_recordBytes; }
    double recordRows() const { return m_recordRows; }
    double recordSeconds() const { return m_recordSeconds; }
    double runSeconds() const { return m_runSeconds; }
    QVariantList channelStats() const { return m_channelStats; }
    mybench::plot::TraceBuffer* ai0() { return &m_ai0; }
    mybench::plot::TraceBuffer* ai1() { return &m_ai1; }
    mybench::plot::TraceBuffer* dmm() { return &m_dmm; }

    // Record (the ELVISmx "Log" toggle): opens a new file and streams to it; starts the logger
    // when idle.
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void setRecordPaused(bool paused);
    // Empties the chart and the statistics (the file is not affected).
    Q_INVOKABLE void clearChart();
    // Local folder path of a QML folder URL.
    Q_INVOKABLE void setFolderUrl(const QUrl& url);
    Q_INVOKABLE QUrl folderUrl() const;

signals:
    void channelsChanged();
    void dmmChanged();
    void samplingRateChanged();
    void limitsChanged();
    void autoScaleChanged();
    void fileSettingsChanged();
    void recordingChanged();
    void recordProgressChanged();
    void statsChanged();

protected:
    WorkerFunction createWorker() override;
    void onFinished() override;
    DataTable exportTable() const override;

private:
    // UI thread -> worker commands (in order).
    struct Command {
        enum Kind { Open, Close, Pause, Resume, Clear } kind;
        QString path;
        QStringList comments;
    };
    struct Commands {
        std::mutex mutex;
        std::deque<Command> queue;
        void push(Command c)
        {
            std::lock_guard lock(mutex);
            queue.push_back(std::move(c));
        }
        std::deque<Command> takeAll()
        {
            std::lock_guard lock(mutex);
            return std::exchange(queue, {});
        }
    };

    void updateResources();
    void restartIfRunning();
    Command openCommand();
    QStringList headerComments() const;
    void setRecordingState(bool recording, bool paused, bool pending);

    bool m_ai0Enabled = true;
    bool m_ai1Enabled = false;
    bool m_dmmEnabled = false;
    int m_dmmFunction = 0;
    int m_dmmRange = 0;
    double m_sampleRate = 1000;   // ELVISmx default (log.md 2, observed)
    double m_maximum = 10;
    double m_minimum = -10;
    bool m_autoScale = true;
    QString m_folder;
    QString m_baseName = QStringLiteral("log");
    bool m_appendTimestamp = true;
    FileFormat m_fileFormat = TabSeparated;
    FileLayout m_fileLayout = NativeLayout;
    bool m_appendToExisting = false;
    bool m_timestampColumn = false;
    bool m_recordOnRun = false;
    bool m_stopAfterEnabled = false;
    double m_stopAfterMinutes = 60;

    bool m_recording = false;
    bool m_recordPaused = false;
    bool m_recordPending = false;
    bool m_armRecord = false;
    QString m_recordFile;
    double m_recordBytes = 0;
    double m_recordRows = 0;
    double m_recordSeconds = 0;
    double m_runSeconds = 0;
    QVariantList m_channelStats;
    std::shared_ptr<Commands> m_commands = std::make_shared<Commands>();
    mybench::plot::TraceBuffer m_ai0;
    mybench::plot::TraceBuffer m_ai1;
    mybench::plot::TraceBuffer m_dmm;
};

void registerLog(InstrumentRegistry& registry);

}  // namespace mybench::instruments
