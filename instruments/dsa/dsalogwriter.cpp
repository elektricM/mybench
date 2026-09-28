#include "dsalogwriter.h"

#include <QDateTime>
#include <QIODevice>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments::dsa {

namespace {

const DataColumn* column(const DataTable& table, const QString& name)
{
    for (const DataColumn& c : table.columns) {
        if (c.name == name)
            return &c;
    }
    return nullptr;
}

// `d.dddddd E+n` with an unpadded exponent (5.174413E+0, -1.220740E-2).
QString scientific(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("0.000000E+0");
    QString text = QString::asprintf("%.6E", value);
    const qsizetype e = text.indexOf(QLatin1Char('E'));
    QString exponent = text.mid(e + 2);
    exponent.remove(QRegularExpression(QStringLiteral("^0+(?=\\d)")));
    return text.left(e + 2) + exponent;
}

// `M/d/yyyy  HH:mm:ss.ffffff` (two spaces, six fractional digits) from epoch microseconds.
QString timestamp(qint64 epochMicroseconds)
{
    const qint64 ms = epochMicroseconds / 1000;
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    return t.toString(QStringLiteral("M/d/yyyy  HH:mm:ss.")) + QString::asprintf("%06d", int((epochMicroseconds % 1000000 + 1000000) % 1000000));
}

}  // namespace

QString LogWriter::name() const
{
    return QObject::tr("ELVISmx-style text log");
}

QStringList LogWriter::extensions() const
{
    return {u"txt"_s};
}

bool LogWriter::write(const DataTable& table, const QStringList&, QIODevice& out, QString* error) const
{
    const DataColumn* time = column(table, u"Time"_s);
    const DataColumn* amplitude = column(table, u"Amplitude"_s);
    if (!time || !amplitude || time->values.empty()) {
        if (error)
            *error = QObject::tr("There is no time record to write.");
        return false;
    }
    const QDateTime t0 = table.extra.value(QString::fromLatin1(kLogT0)).toDateTime();
    const double dt = table.extra.value(QString::fromLatin1(kLogDt)).toDouble();

    const qint64 base = t0.toMSecsSinceEpoch() * 1000;
    QTextStream s(&out);
    s.setEncoding(QStringConverter::Utf8);
    s << "waveform\tSource signal\r\n";
    s << "t0\t" << timestamp(base) << "\r\n";
    s << "delta t\t" << scientific(dt) << "\r\n";
    s << "\r\n";
    s << "time[0]\tSource signal\r\n";
    // Absolute time of each sample = t0 + k * dt, to the microsecond.
    for (size_t k = 0; k < amplitude->values.size(); ++k)
        s << timestamp(base + qint64(std::llround(double(k) * dt * 1e6))) << '\t' << scientific(amplitude->values[k]) << "\r\n";
    s << "\r\n";

    const DataColumn* frequency = column(table, u"Frequency"_s);
    const DataColumn* magnitude = column(table, u"Magnitude"_s);
    if (frequency && magnitude) {
        s << "FFT waveform\r\n";
        s << "Units: " << table.extra.value(QString::fromLatin1(kLogUnits)).toString() << "\r\n";
        size_t n = std::min(frequency->values.size(), magnitude->values.size());
        if (const int lines = table.extra.value(QString::fromLatin1(kLogLines)).toInt(); lines > 0)
            n = std::min(n, size_t(lines));
        for (size_t k = 0; k < n; ++k)
            s << QString::number(frequency->values[k], 'f', 2) << '\t' << scientific(magnitude->values[k]) << "\r\n";
    } else if (const DataColumn* centre = column(table, u"Band centre"_s)) {
        const DataColumn* level = column(table, u"Level"_s);
        s << "Octave bands\r\n";
        s << "Units: " << table.extra.value(QString::fromLatin1(kLogUnits)).toString() << "\r\n";
        for (size_t k = 0; level && k < std::min(centre->values.size(), level->values.size()); ++k)
            s << QString::number(centre->values[k], 'f', 2) << '\t' << QString::number(level->values[k], 'f', 2) << "\r\n";
    }
    s.flush();
    if (s.status() != QTextStream::Ok) {
        if (error)
            *error = out.errorString();
        return false;
    }
    return true;
}

}  // namespace mybench::instruments::dsa
