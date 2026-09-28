#include "logfile.h"

#include <mybench/dataexport.h>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

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
                                  const QString& extension, const QDateTime& now, bool allowExisting)
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
    for (int n = 2; !allowExisting && QFileInfo::exists(candidate); ++n)
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
    const QIODevice::OpenMode mode = m_append ? QIODevice::WriteOnly | QIODevice::Append
                                              : QIODevice::WriteOnly | QIODevice::NewOnly;
    // Explicit line endings (m_eol) instead of the platform's text mode.
    if (!m_file.open(mode)) {
        if (error)
            *error = m_file.errorString();
        return false;
    }
    for (const QString& line : comments)
        m_buffer += "# " + line.toUtf8() + m_eol;
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

void LogFileWriter::addRaw(const QByteArray& cell)
{
    separate();
    m_buffer += cell;
}

void LogFileWriter::endRow()
{
    m_buffer += m_eol;
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

QString elvismxTime(qint64 epochMicroseconds)
{
    const qint64 ms = epochMicroseconds >= 0 ? epochMicroseconds / 1000 : -((-epochMicroseconds + 999) / 1000);
    const qint64 micro = epochMicroseconds - ms * 1000;
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    return t.toString(QStringLiteral("M/d/yyyy  HH:mm:ss.")) + QStringLiteral("%1").arg(t.time().msec() * 1000 + micro, 6, 10, QLatin1Char('0'));
}

QString elvismxValue(double value)
{
    if (!std::isfinite(value))
        return {};
    QString text = QString::asprintf("%.6E", value);   // 5.174413E+00
    const qsizetype e = text.indexOf(QLatin1Char('E'));
    QString exponent = text.mid(e + 2);                 // digits after the sign
    exponent.remove(QRegularExpression(QStringLiteral("^0+(?=\\d)")));
    return text.left(e + 2) + exponent;
}

QStringList elvismxHeader(const QString& deviceName, const QStringList& channels)
{
    QStringList cells;
    for (const QString& channel : channels)
        cells << QStringLiteral("Time") << deviceName + QLatin1Char('/') + channel;
    return cells;
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
        {"DC Voltage", "V", {{0.2, "200 mV"}, {2, "2 V"}, {20, "20 V"}, {60, "60 V"}}},
        {"AC Voltage", "V", {{0.2, "200 mV"}, {2, "2 V"}, {20, "20 V"}}},
        {"DC Current", "A", {{0.02, "20 mA"}, {0.2, "200 mA"}, {1, "1 A"}}},
        {"AC Current", "A", {{0.02, "20 mA"}, {0.2, "200 mA"}, {1, "1 A"}}},
        {"Resistance", "Ω", {{200, "200 Ω"}, {2e3, "2 kΩ"}, {20e3, "20 kΩ"}, {200e3, "200 kΩ"}, {2e6, "2 MΩ"}, {20e6, "20 MΩ"}}},
    };
    return functions;
}

int autoRange(const std::vector<DmmRangeInfo>& ranges, int current, double value, bool overload)
{
    const int last = int(ranges.size()) - 1;
    current = std::clamp(current, 0, last);
    // Smallest range whose 2000-count limit the value stays under; anything else (or a value
    // that is not a number) needs the largest.
    int next = last;
    if (std::isfinite(value)) {
        for (int i = 0; i <= last; ++i) {
            if (std::abs(value) < 0.9995 * ranges[size_t(i)].max) {
                next = i;
                break;
            }
        }
    }
    if (overload && next <= current)
        next = std::min(last, current + 1);
    return next;
}

}  // namespace mybench::instruments::logger
