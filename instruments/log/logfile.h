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
    static QString uniquePath(const QString& folder, const QString& baseName, bool appendTimestamp,
                              const QString& extension, const QDateTime& now);

    // Creates the file (fails if it exists) and writes the comment lines ("# " is added) and the
    // column header. `delimiter` is '\t' or ','.
    bool open(const QString& path, const QStringList& comments, const QStringList& columns, char delimiter,
              QString* error = nullptr);
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }

    // Row building: cells are separated by the delimiter; NaN/infinite numbers are empty cells.
    void addNumber(double value);
    void addText(const QString& text);
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
    bool m_rowStarted = false;
    int64_t m_rows = 0;
    int64_t m_bytes = 0;
    qint64 m_lastSync = 0;   // ms since epoch
};

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

// DMM functions the logger offers and their ranges (largest first, as the device lists them).
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

// Software autoranging: the range index (into the ranges, largest first) for the next reading
// after `value` was read on `current`: up after an overload, down while the value would fit
// under 90 % of the next smaller range.
int autoRange(const std::vector<DmmRangeInfo>& ranges, int current, double value, bool overload);

}  // namespace mybench::instruments::logger
