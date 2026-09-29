// Data Logger helpers without Qt objects of their own (unit-tested in tests/tst_log.cpp): the
// streaming log file writer, file naming, running statistics and the DMM range table with its
// software autoranging.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <limits>
#include <vector>

namespace mybench::instruments::logger {

// Streams a delimited text log to disk while it records. Layout:
//     # MYBENCH 0.1.0 - Data Logger           <- comment lines given to open()
//     # Sample rate: 10 S/s ...
//     Time (s)<TAB>AI 0 (V)<TAB>...           <- column header
//     0<TAB>0.0123<TAB>...                     <- rows ('.' decimal point, empty cell = no value)
// Rows are only written whole, and the file is flushed to the operating system at every
// flush() (the worker calls it after each block) and synced to the disk every few seconds, so
// a crash or power loss leaves a valid file with everything up to the last block.
class LogFileWriter
{
public:
    LogFileWriter() = default;
    ~LogFileWriter() { close(); }
    LogFileWriter(const LogFileWriter&) = delete;
    LogFileWriter& operator=(const LogFileWriter&) = delete;

    // A path in `folder` that does not exist yet: "<base>_<yyyyMMdd_HHmmss>.<ext>" (or "<base>.<ext>"
    // without the timestamp), with "_2", "_3"... appended when taken. Never overwrites.
    // With `allowExisting` the first candidate is returned even if it exists (append mode).
    static QString uniquePath(const QString& folder, const QString& baseName, bool appendTimestamp,
                              const QString& extension, const QDateTime& now, bool allowExisting = false);

    // Line ending (default "\n") and append mode (default: create a new file, never overwrite).
    // Set before open(). CR LF is written explicitly, so it is the same on every platform.
    void setLineEnding(const QByteArray& eol) { m_eol = eol; }
    void setAppend(bool append) { m_append = append; }

    // Creates the file (fails if it exists) and writes the comment lines ("# " is added) and the
    // column header. `delimiter` is '\t' or ','.
    bool open(const QString& path, const QStringList& comments, const QStringList& columns, char delimiter,
              QString* error = nullptr);
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }

    // Row building: cells are separated by the delimiter; NaN/infinite numbers are empty cells.
    void addNumber(double value);
    void addText(const QString& text);
    void addRaw(const QByteArray& cell);   // pre-formatted cell (no escaping)
    void addEmpty() { addText({}); }
    void endRow();

    // Writes the buffered rows. Returns false (and closes the file) on a write error.
    bool flush(QString* error = nullptr);
    void close();

    int64_t rows() const { return m_rows; }
    int64_t bytes() const { return m_bytes + m_buffer.size(); }

private:
    void separate();

    QFile m_file;
    QByteArray m_buffer;
    char m_delimiter = '\t';
    QByteArray m_eol = "\n";
    bool m_append = false;
    bool m_rowStarted = false;
    int64_t m_rows = 0;
    int64_t m_bytes = 0;
    qint64 m_lastSync = 0;   // ms since epoch
};

// ELVISmx-compatible cell formats (log.md 8.2): the time of a sample as `M/d/yyyy  HH:mm:ss.ffffff`
// (two spaces; local time from the epoch microseconds) and a value as `d.dddddd E+n` with an
// unpadded exponent (`5.174413E+0`, `-1.220740E-2`).
QString elvismxTime(qint64 epochMicroseconds);
QString elvismxValue(double value);
// Header row: `Time<TAB>myDAQ1/ai0` per channel, in the order given ("ai0", "ai1").
QStringList elvismxHeader(const QString& deviceName, const QStringList& channels);

// Welford running statistics over finite values.
struct RunningStats {
    int64_t count = 0;
    double mean = 0;
    double m2 = 0;
    double minimum = std::numeric_limits<double>::quiet_NaN();
    double maximum = std::numeric_limits<double>::quiet_NaN();
    double last = std::numeric_limits<double>::quiet_NaN();

    void add(double value);
    double standardDeviation() const;   // n - 1; NaN below two values
};

// DMM functions the logger offers and their ranges (smallest first, as the DMM instrument lists them).
struct DmmRangeInfo {
    double max;
    const char* label;
};
struct DmmFunctionInfo {
    const char* name;
    const char* unit;
    std::vector<DmmRangeInfo> ranges;
};
const std::vector<DmmFunctionInfo>& dmmFunctions();   // DC V, AC V, DC A, AC A, Resistance

// Software autoranging as the DMM instrument (dmm.md 4.2): the first reading of a run is taken on
// the largest range (`ranges.size() - 1`), then every next range is chosen directly from
// |value|: the smallest range whose 2000-count limit (0.9995 x its maximum) the value does not
// reach. An overload that would keep the same range moves one range up.
int autoRange(const std::vector<DmmRangeInfo>& ranges, int current, double value, bool overload);

// autoRange with memory (the DMM instrument's AutoRanger): a floating input reads ~0 on a large
// range but overloads a small one, so the plain rule hunts, and every range change costs the real
// meter ~2.6 s. After an overload pushed the ranger up from a range, that range and all smaller
// ones are barred for kBarFirst readings; the bar doubles on each repetition up to kBarMaximum and
// is forgotten once the range has been held.
class AutoRanger
{
public:
    static constexpr int kBarFirst = 12;
    static constexpr int kBarMaximum = 192;
    int next(const std::vector<DmmRangeInfo>& ranges, int current, double value, bool overload);

private:
    int m_barredUpTo = -1;   // ranges 0..m_barredUpTo (smaller ones) are barred while m_barLeft > 0
    int m_barLeft = 0;
    int m_nextBar = kBarFirst;
    int m_held = 0;
};

}  // namespace mybench::instruments::logger
