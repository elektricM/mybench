// DmmController: the Multimeter (docs/instruments/dmm.md; ELVISmx behaviour in
// docs/instruments/algorithms/dmm.md).
//
// Reads the isolated DMM continuously (Run) or once (Single) in a worker thread: one reading at a
// time with dmmRead(), software autoranging with the ELVISmx value -> range table (re-reading on
// the new range before a reading is displayed), at most one displayed reading per 200 ms.
// The UI thread turns raw readings into the display: 3½-digit text per range, +Over, % FS,
// null offset, diode GOOD/OPEN, continuity (tone + indicator), and the enhancements: hold,
// trend, statistics and the reading log (in memory, optionally streamed to a tab-separated file).
#pragma once

#include "dmmlogic.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace mybench::instruments {

class ContinuityTone;

class DmmController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings (persistent, in dependency order: selectedFunction before rangeIndex) --------
    Q_PROPERTY(MeterFunction selectedFunction READ selectedFunction WRITE setSelectedFunction NOTIFY functionChanged)
    // Mode: Auto (true) or Specify Range (false). One global setting, kept across functions.
    Q_PROPERTY(bool autoRange READ autoRange WRITE setAutoRange NOTIFY autoRangeChanged)
    // Selected range as an index into rangeLabels (smallest first, as ELVISmx lists them).
    Q_PROPERTY(int rangeIndex READ rangeIndex WRITE setRangeIndex NOTIFY rangeIndexChanged)
    // Trend chart window: 0 = 1 min, 1 = 10 min, 2 = 1 h, 3 = everything since the last clear.
    Q_PROPERTY(int trendWindow READ trendWindow WRITE setTrendWindow NOTIFY trendWindowChanged)
    // Continuity tone on/off (the visual indicator always works).
    Q_PROPERTY(bool continuityBeep READ continuityBeep WRITE setContinuityBeep NOTIFY continuityBeepChanged)
    // [MYBENCH] "-OVER" for negative DC overloads (ELVISmx shows "+Over" for both signs).
    Q_PROPERTY(bool signedOverload READ signedOverload WRITE setSignedOverload NOTIFY signedOverloadChanged)
    // [MYBENCH] start measuring when the panel or tile is first shown (default off).
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)

    // --- Transient controls (not settings) ---------------------------------------------------
    Q_PROPERTY(bool nullOffset READ nullOffset WRITE setNullOffset NOTIFY nullOffsetChanged STORED false)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY holdChanged STORED false)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged STORED false)
    // Optional file the log is streamed to while recording (tab-separated, or CSV by extension).
    Q_PROPERTY(QUrl logFile READ logFile WRITE setLogFile NOTIFY logFileChanged STORED false)

    // --- Derived from the settings -----------------------------------------------------------
    Q_PROPERTY(QString functionName READ functionName NOTIFY functionChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY functionChanged)                  // base unit: V, A, Ω
    Q_PROPERTY(QStringList rangeLabels READ rangeLabels NOTIFY rangesChanged) // "200mV" ... "60V"
    Q_PROPERTY(bool effectiveAutoRange READ effectiveAutoRange NOTIFY autoRangeChanged) // Continuity forces Auto
    Q_PROPERTY(bool rangeSelectable READ rangeSelectable NOTIFY autoRangeChanged)
    Q_PROPERTY(QString selectedRangeLabel READ selectedRangeLabel NOTIFY rangeIndexChanged)
    Q_PROPERTY(bool toneAvailable READ toneAvailable CONSTANT)

    // --- Results (UI thread, from the worker) ------------------------------------------------
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
    Q_PROPERTY(QString readingText READ readingText NOTIFY readingChanged)     // "1.235", "+Over", "OPEN", "---"
    Q_PROPERTY(QString readingUnit READ readingUnit NOTIFY readingChanged)     // "V", "mV", "kΩ" (empty for OPEN)
    Q_PROPERTY(double reading READ reading NOTIFY readingChanged)              // displayed value, base units (NaN: none/over)
    Q_PROPERTY(double rawReading READ rawReading NOTIFY readingChanged)
    Q_PROPERTY(bool overload READ overload NOTIFY readingChanged)
    Q_PROPERTY(double fullScaleFraction READ fullScaleFraction NOTIFY readingChanged)  // 0..1; -1 = empty bar
    Q_PROPERTY(double signedFullScaleFraction READ signedFullScaleFraction NOTIFY readingChanged)
    Q_PROPERTY(QString activeRangeLabel READ activeRangeLabel NOTIFY readingChanged)  // range of the last reading
    Q_PROPERTY(QString diodeStatus READ diodeStatus NOTIFY readingChanged)     // "GOOD" / "OPEN" / ""
    Q_PROPERTY(bool continuityClosed READ continuityClosed NOTIFY readingChanged)
    Q_PROPERTY(int readingSerial READ readingSerial NOTIFY readingChanged)     // increments per reading
    Q_PROPERTY(bool nullArmed READ nullArmed NOTIFY nullOffsetChanged)         // waiting for the reading to capture
    Q_PROPERTY(double nullValue READ nullValue NOTIFY nullOffsetChanged)       // base units
    Q_PROPERTY(QString nullText READ nullText NOTIFY nullOffsetChanged)        // "1.235 V"
    // Smallest y span of the trend chart (a steady reading does not fill the plot with noise).
    Q_PROPERTY(double trendMinimumSpan READ trendMinimumSpan NOTIFY readingChanged)
    Q_PROPERTY(double trendSeconds READ trendSeconds NOTIFY trendWindowChanged)   // 0 = all

    // Statistics of the displayed values since the last reset or function change.
    Q_PROPERTY(int statCount READ statCount NOTIFY statisticsChanged)
    Q_PROPERTY(int statOverloads READ statOverloads NOTIFY statisticsChanged)
    Q_PROPERTY(double statMinimum READ statMinimum NOTIFY statisticsChanged)
    Q_PROPERTY(double statMaximum READ statMaximum NOTIFY statisticsChanged)
    Q_PROPERTY(double statMean READ statMean NOTIFY statisticsChanged)
    Q_PROPERTY(double statStdDev READ statStdDev NOTIFY statisticsChanged)

    Q_PROPERTY(int logCount READ logCount NOTIFY logChanged)
    // Trend of the displayed readings (x = seconds since the epoch, y in the base unit; overloads
    // are NaN gaps).
    Q_PROPERTY(mybench::plot::TraceBuffer* trend READ trend CONSTANT)

public:
    // Same order as mydaq::DmmFunction.
    enum MeterFunction { DcVoltage, AcVoltage, DcCurrent, AcCurrent, Resistance, Diode, Continuity };
    Q_ENUM(MeterFunction)

    DmmController();
    ~DmmController() override;

    MeterFunction selectedFunction() const { return m_function; }
    void setSelectedFunction(MeterFunction function);
    bool autoRange() const { return m_autoRange; }
    void setAutoRange(bool autoRange);
    int rangeIndex() const { return m_rangeIndex; }
    void setRangeIndex(int index);
    int trendWindow() const { return m_trendWindow; }
    void setTrendWindow(int window);
    bool continuityBeep() const { return m_continuityBeep; }
    void setContinuityBeep(bool enabled);
    bool signedOverload() const { return m_signedOverload; }
    void setSignedOverload(bool enabled);
    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool enabled);

    bool nullOffset() const { return m_nullOffset; }
    void setNullOffset(bool enabled);
    bool hold() const { return m_hold; }
    void setHold(bool hold);
    bool recording() const { return m_recording; }
    void setRecording(bool recording);
    QUrl logFile() const { return m_logFileUrl; }
    void setLogFile(const QUrl& file);

    QString functionName() const;
    QString unit() const;
    QStringList rangeLabels() const;
    bool effectiveAutoRange() const { return m_autoRange || m_function == Continuity; }
    bool rangeSelectable() const { return !effectiveAutoRange() && !m_ranges.empty(); }
    QString selectedRangeLabel() const;
    bool toneAvailable() const;

    bool hasReading() const { return m_display.valid; }
    QString readingText() const { return m_display.text; }
    QString readingUnit() const { return m_display.unit; }
    double reading() const { return m_display.value; }
    double rawReading() const { return m_display.raw; }
    bool overload() const { return m_display.overload; }
    double fullScaleFraction() const { return m_display.fullScale; }
    double signedFullScaleFraction() const { return m_display.signedFullScale; }
    QString activeRangeLabel() const { return m_display.rangeLabel; }
    QString diodeStatus() const { return m_display.diodeStatus; }
    bool continuityClosed() const { return m_continuityClosed; }
    int readingSerial() const { return m_readingSerial; }
    bool nullArmed() const { return m_nullOffset && !m_nullCaptured; }
    double nullValue() const { return m_nullCaptured ? m_nullValue : qQNaN(); }
    QString nullText() const;
    double trendMinimumSpan() const;
    double trendSeconds() const;

    int statCount() const { return int(m_statistics.count()); }
    int statOverloads() const { return int(m_statistics.overloads()); }
    double statMinimum() const { return m_statistics.minimum(); }
    double statMaximum() const { return m_statistics.maximum(); }
    double statMean() const { return m_statistics.mean(); }
    double statStdDev() const { return m_statistics.standardDeviation(); }

    int logCount() const { return int(m_log.size()); }
    mybench::plot::TraceBuffer* trend() { return &m_trend; }

    // A statistics value in the current range's format ("1.2346 V"); `extraDigit` adds one
    // decimal (averages, σ).
    Q_INVOKABLE QString formatValue(double value, bool extraDigit = false) const;
    Q_INVOKABLE void resetStatistics();
    Q_INVOKABLE void clearTrend();
    Q_INVOKABLE void clearLog();
    // Writes the in-memory log (tab-separated, or CSV for a .csv file). Toast on success/failure.
    Q_INVOKABLE bool saveLog(const QUrl& file);
    // Called by the panel/tile when shown: starts measuring if autoStart is on (once per launch).
    Q_INVOKABLE void autoStartIfEnabled();

    // Worker -> UI: one displayed reading (public for the tests).
    struct Reading {
        double raw = 0;          // base units
        bool overload = false;   // device flag or above the range's upper limit
        int rangeIndex = 0;      // index into the device's range list (largest first)
        qint64 timeMs = 0;       // ms since the epoch
    };
    void applyReading(const Reading& reading);

signals:
    void functionChanged();
    void autoRangeChanged();
    void rangeIndexChanged();
    void rangesChanged();
    void trendWindowChanged();
    void continuityBeepChanged();
    void signedOverloadChanged();
    void autoStartChanged();
    void nullOffsetChanged();
    void holdChanged();
    void recordingChanged();
    void logFileChanged();
    void readingChanged();
    void statisticsChanged();
    void logChanged();

protected:
    WorkerFunction createWorker() override;
    void onFinished() override;
    DataTable exportTable() const override;

private:
    struct Display {
        bool valid = false;
        QString text = QStringLiteral("---");
        QString unit;
        double value = qQNaN();
        double raw = qQNaN();
        bool overload = false;
        double fullScale = -1;
        double signedFullScale = 0;
        QString rangeLabel;
        QString diodeStatus;
    };

    mydaq::DmmFunction deviceFunction() const { return mydaq::DmmFunction(int(m_function)); }
    void refreshRanges();
    int deviceRangeIndex() const;   // m_rangeIndex converted to the device order
    dmm::DisplayFormat currentFormat() const;
    void clearDisplay();
    void updateTone();
    void appendLog(const dmm::LogRow& row);
    void closeLogFile();
    void openLogFile();

    // Settings
    MeterFunction m_function = DcVoltage;
    bool m_autoRange = false;   // ELVISmx starts in Specify Range (verified)
    int m_rangeIndex = 3;       // largest DC V range (60V)
    int m_trendWindow = 1;      // 10 min
    bool m_continuityBeep = true;
    bool m_signedOverload = false;
    bool m_autoStart = false;

    // Transient
    bool m_nullOffset = false;
    bool m_nullCaptured = false;
    double m_nullValue = 0;
    bool m_hold = false;
    bool m_recording = false;
    bool m_autoStartDone = false;
    QUrl m_logFileUrl;
    std::unique_ptr<QFile> m_logFile;

    std::vector<double> m_ranges;   // device order (largest first)
    Display m_display;
    dmm::DisplayFormat m_lastFormat;
    bool m_continuityClosed = false;
    int m_readingSerial = 0;
    dmm::Statistics m_statistics;
    std::vector<dmm::LogRow> m_log;
    qint64 m_logStartMs = 0;
    struct HistoryPoint {
        double elapsed;
        double value;
        bool overload;
    };
    std::vector<HistoryPoint> m_history;   // this function's readings (Export data)
    qint64 m_historyStartMs = 0;
    mybench::plot::TraceBuffer m_trend;
    ContinuityTone* m_tone = nullptr;
};

// Registration (called by the app and by mybench-dmm).
void registerDmm(InstrumentRegistry& registry);

}  // namespace mybench::instruments
