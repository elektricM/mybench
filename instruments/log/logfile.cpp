#include "logfile.h"

#include <mybench/dataexport.h>

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#endif

namespace mybench::instruments::logger {

namespace {
constexpr qint64 kSyncIntervalMs = 5000;
}

QString LogFileWriter::uniquePath(const QString& folder, const QString& baseName, bool appendTimestamp,
                                  const QString& extension, const QDateTime& now)
{
    QString base = baseName.trimmed();
    // Keep file names portable.
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (QChar& c : base) {
        if (forbidden.contains(c) || c.unicode() < 32)
            c = QLatin1Char('_');
    }
    if (base.isEmpty())
        base = QStringLiteral("log");
    if (appendTimestamp)
        base += QLatin1Char('_') + now.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QDir dir(folder);
    QString candidate = dir.filePath(base + QLatin1Char('.') + extension);
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate = dir.filePath(QStringLiteral("%1_%2.%3").arg(base).arg(n).arg(extension));
    return candidate;
}

bool LogFileWriter::open(const QString& path, const QStringList& comments, const QStringList& columns,
                         char delimiter, QString* error)
{
    close();
    m_delimiter = delimiter;
    m_rows = 0;
    m_bytes = 0;
    m_buffer.clear();
    m_rowStarted = false;
    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QObject::tr("the folder %1 cannot be created").arg(info.absolutePath());
        return false;
    }
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text)) {
        if (error)
            *error = m_file.errorString();
        return false;
    }
    for (const QString& line : comments)
        m_buffer += "# " + line.toUtf8() + '\n';
    for (const QString& column : columns)
        addText(column);
    endRow();
    m_rows = 0;   // the header is not a data row
    m_lastSync = QDateTime::currentMSecsSinceEpoch();
    return flush(error);
}

void LogFileWriter::separate()
{
    if (m_rowStarted)
        m_buffer += m_delimiter;
    m_rowStarted = true;
}

void LogFileWriter::addNumber(double value)
{
    separate();
    if (std::isfinite(value))
        m_buffer += DataExport::formatNumber(value).toLatin1();
}

void LogFileWriter::addText(const QString& text)
{
    separate();
    QString cell = text;
    // Delimiters and line breaks never appear inside a cell.
    cell.replace(QLatin1Char(m_delimiter), QLatin1Char(' '));
    cell.replace(QLatin1Char('\n'), QLatin1Char(' '));
    m_buffer += cell.toUtf8();
}

void LogFileWriter::endRow()
{
    m_buffer += '\n';
    m_rowStarted = false;
    ++m_rows;
}

bool LogFileWriter::flush(QString* error)
{
    if (!m_file.isOpen())
        return false;
    if (!m_buffer.isEmpty()) {
        const qint64 written = m_file.write(m_buffer);
        if (written != m_buffer.size() || !m_file.flush()) {
            if (error)
                *error = m_file.errorString();
            m_buffer.clear();
            m_file.close();
            return false;
        }
        m_bytes += written;
        m_buffer.clear();
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastSync >= kSyncIntervalMs) {
        m_lastSync = now;
#if defined(Q_OS_UNIX)
        ::fsync(m_file.handle());
#elif defined(Q_OS_WIN)
        ::_commit(m_file.handle());
#endif
    }
    return true;
}

void LogFileWriter::close()
{
    if (!m_file.isOpen())
        return;
    flush();
#if defined(Q_OS_UNIX)
    ::fsync(m_file.handle());
#elif defined(Q_OS_WIN)
    ::_commit(m_file.handle());
#endif
    m_file.close();
}

void RunningStats::add(double value)
{
    if (!std::isfinite(value))
        return;
    last = value;
    if (count == 0) {
        minimum = maximum = value;
    } else {
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    ++count;
    const double delta = value - mean;
    mean += delta / double(count);
    m2 += delta * (value - mean);
}

double RunningStats::standardDeviation() const
{
    return count < 2 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(std::max(0.0, m2 / double(count - 1)));
}

const std::vector<DmmFunctionInfo>& dmmFunctions()
{
    static const std::vector<DmmFunctionInfo> functions{
        {"DC Voltage", "V", {{60, "60 V"}, {20, "20 V"}, {2, "2 V"}, {0.2, "200 mV"}}},
        {"AC Voltage", "V", {{20, "20 V"}, {2, "2 V"}, {0.2, "200 mV"}}},
        {"DC Current", "A", {{1, "1 A"}, {0.2, "200 mA"}, {0.02, "20 mA"}}},
        {"AC Current", "A", {{1, "1 A"}, {0.2, "200 mA"}, {0.02, "20 mA"}}},
        {"Resistance", "Ω", {{20e6, "20 MΩ"}, {2e6, "2 MΩ"}, {200e3, "200 kΩ"}, {20e3, "20 kΩ"}, {2e3, "2 kΩ"}, {200, "200 Ω"}}},
    };
    return functions;
}

int autoRange(const std::vector<DmmRangeInfo>& ranges, int current, double value, bool overload)
{
    const int last = int(ranges.size()) - 1;
    current = std::clamp(current, 0, last);
    if (overload)
        return std::max(0, current - 1);
    int next = current;
    while (next < last && std::isfinite(value) && std::abs(value) < 0.9 * ranges[size_t(next + 1)].max)
        ++next;
    return next;
}

}  // namespace mybench::instruments::logger
