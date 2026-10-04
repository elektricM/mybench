#include "scopelogwriter.h"

#include <QDateTime>
#include <QIODevice>
#include <QObject>
#include <QTextStream>

#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments::scope {

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
    const double dt = table.extra.value(QString::fromLatin1(kLogInterval)).toDouble();
    const QVariantList channelList = table.extra.value(QString::fromLatin1(kLogChannels)).toList();
    if (!(dt > 0) || channelList.isEmpty()) {
        if (error)
            *error = QObject::tr("There is no record to log.");
        return false;
    }
    const double shift = table.extra.value(QString::fromLatin1(kLogShift)).toDouble();
    const QVariantList skewList = table.extra.value(QString::fromLatin1(kLogSkew)).toList();
    QDateTime t0 = table.extra.value(QString::fromLatin1(kLogStart)).toDateTime();
    if (!t0.isValid())
        t0 = QDateTime::currentDateTime();

    struct Channel {
        int index;
        double skew;
        const std::vector<double>* values;
    };
    std::vector<Channel> channels;
    for (const QVariant& entry : channelList) {
        const int ch = entry.toInt();
        const QString name = u"CH %1"_s.arg(ch);
        for (const DataColumn& column : table.columns) {
            if (column.name == name) {
                channels.push_back({ch, ch < skewList.size() ? skewList[ch].toDouble() : 0.0, &column.values});
                break;
            }
        }
    }
    if (channels.empty()) {
        if (error)
            *error = QObject::tr("There is no record to log.");
        return false;
    }

    // Absolute time stamps with microseconds, as LabVIEW writes them ("10/04/2026 09:15:02.123456 PM").
    auto stamp = [&t0](double offset) {
        const qint64 us = qint64(std::llround(offset * 1e6));
        const QDateTime t = t0.addMSecs(us / 1000);
        const int micro = int((t0.time().msec() * 1000 + us) % 1000000);
        return t.toString(u"MM/dd/yyyy hh:mm:ss"_s) + u'.' + QString::number(micro).rightJustified(6, u'0')
               + t.toString(u" AP"_s);
    };
    // %E with LabVIEW's exponent: no leading zeros ("5.000000E-6", spec §8).
    auto number = [](double v) {
        QString text = QString::asprintf("%.6E", v);
        const qsizetype e = text.indexOf(u'E');
        if (e >= 0 && e + 2 < text.size()) {
            qsizetype digits = e + 2;
            while (digits + 1 < text.size() && text[digits] == u'0')
                text.remove(digits, 1);
        }
        return text;
    };

    QStringList rows[3];
    rows[0] << u"waveform"_s;
    rows[1] << u"t0"_s;
    rows[2] << u"delta t"_s;
    QStringList columnHeader;
    size_t length = 0;
    for (size_t i = 0; i < channels.size(); ++i) {
        const Channel& c = channels[i];
        rows[0] << u"Ch%1"_s.arg(c.index) << QString();
        rows[1] << stamp(c.skew + shift) << QString();
        rows[2] << number(dt) << QString();
        columnHeader << u"time[%1]"_s.arg(i) << u"Ch%1"_s.arg(c.index);
        length = std::max(length, c.values->size());
    }
    QTextStream stream(&out);
    for (const QStringList& row : rows)
        stream << row.join(u'\t') << '\n';
    stream << '\n' << columnHeader.join(u'\t') << '\n';
    for (size_t i = 0; i < length; ++i) {
        QStringList cells;
        for (const Channel& c : channels) {
            cells << stamp(double(i) * dt + c.skew + shift);
            cells << (i < c.values->size() ? number((*c.values)[i]) : QString());
        }
        stream << cells.join(u'\t') << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        if (error)
            *error = out.errorString();
        return false;
    }
    return true;
}

}  // namespace mybench::instruments::scope
