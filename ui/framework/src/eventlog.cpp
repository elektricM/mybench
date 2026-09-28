#include "mybench/eventlog.h"

#include <mydaq/device.h>

#include <QCoreApplication>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QThread>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcEvents, "mybench.events")

namespace mybench {

namespace {

constexpr size_t kMaxEntries = 1000;

QString levelName(EventLog::Level level)
{
    switch (level) {
    case EventLog::Debug:
        return u"debug"_s;
    case EventLog::Info:
        return u"info"_s;
    case EventLog::Warning:
        return u"warning"_s;
    case EventLog::Error:
        return u"error"_s;
    }
    return {};
}

}  // namespace

EventLog::EventLog(QObject* parent) : QAbstractListModel(parent) {}

EventLog* EventLog::instance()
{
    static EventLog* const log = new EventLog;
    return log;
}

EventLog* EventLog::create(QQmlEngine*, QJSEngine*)
{
    EventLog* log = instance();
    QJSEngine::setObjectOwnership(log, QJSEngine::CppOwnership);
    return log;
}

void EventLog::log(Level level, const QString& source, const QString& message, const QString& details)
{
    const QString text = details.isEmpty() ? message : message + u" ["_s + details + u']';
    switch (level) {
    case Debug:
        qCDebug(lcEvents).noquote() << source << text;
        return;   // debug entries only go to the Qt log
    case Info:
        qCInfo(lcEvents).noquote() << source << text;
        break;
    case Warning:
    case Error:
        qCWarning(lcEvents).noquote() << source << text;
        break;
    }
    EventLog* self = instance();
    if (QThread::currentThread() == self->thread()) {
        self->add(level, source, message, details);
        return;
    }
    QMetaObject::invokeMethod(self, [self, level, source, message, details] { self->add(level, source, message, details); },
                              Qt::QueuedConnection);
}

QString EventLog::errorCodeName(int code)
{
    using E = mydaq::DeviceError;
    switch (code) {
    case E::Generic:
        return u"Generic"_s;
    case E::NotConnected:
        return u"NotConnected"_s;
    case E::ResourceBusy:
        return u"ResourceBusy"_s;
    case E::InvalidConfig:
        return u"InvalidConfig"_s;
    case E::Timeout:
        return u"Timeout"_s;
    case E::Overflow:
        return u"Overflow"_s;
    case E::Underflow:
        return u"Underflow"_s;
    case E::UsbError:
        return u"UsbError"_s;
    case E::NotSupported:
        return u"NotSupported"_s;
    default:
        return {};
    }
}

QString EventLog::errorDetails(int code, const QString& backendText)
{
    const QString name = errorCodeName(code);
    const QString text = backendText.trimmed();
    if (name.isEmpty())
        return text;
    return text.isEmpty() ? name : name + u": "_s + text;
}

void EventLog::add(int level, const QString& source, const QString& message, const QString& details)
{
    const Level l = Level(std::clamp(level, int(Debug), int(Error)));
    beginInsertRows({}, 0, 0);
    m_entries.push_front({QDateTime::currentDateTime(), l, source, message, details});
    endInsertRows();
    if (l == Error) {
        ++m_errorCount;
        ++m_unreadErrors;
    } else if (l == Warning) {
        ++m_warningCount;
        ++m_unreadWarnings;
    }
    while (m_entries.size() > kMaxEntries) {
        const int last = int(m_entries.size()) - 1;
        beginRemoveRows({}, last, last);
        if (m_entries.back().level == Error)
            --m_errorCount;
        else if (m_entries.back().level == Warning)
            --m_warningCount;
        m_entries.pop_back();
        endRemoveRows();
    }
    emit countChanged();
    emit entryAdded(int(l), source, message);
}

void EventLog::clear()
{
    beginResetModel();
    m_entries.clear();
    m_errorCount = 0;
    m_unreadErrors = 0;
    m_warningCount = 0;
    m_unreadWarnings = 0;
    endResetModel();
    emit countChanged();
}

void EventLog::markRead()
{
    if (m_unreadErrors == 0 && m_unreadWarnings == 0)
        return;
    m_unreadErrors = 0;
    m_unreadWarnings = 0;
    emit countChanged();
}

QString EventLog::toText() const
{
    QStringList lines;
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        QString line = u"%1  %2  %3  %4"_s.arg(it->time.toString(Qt::ISODate), levelName(it->level).leftJustified(7),
                                                it->source.leftJustified(8), it->message);
        if (!it->details.isEmpty())
            line += u"  ["_s + it->details + u']';
        lines.append(line);
    }
    return lines.join(u'\n');
}

int EventLog::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant EventLog::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_entries.size()))
        return {};
    const Entry& entry = m_entries[size_t(index.row())];
    switch (role) {
    case TimeRole:
        return entry.time;
    case TimeTextRole:
        return entry.time.toString(u"HH:mm:ss"_s);
    case LevelRole:
        return int(entry.level);
    case SourceRole:
        return entry.source;
    case Qt::DisplayRole:
    case MessageRole:
        return entry.message;
    case DetailsRole:
        return entry.details;
    default:
        return {};
    }
}

QHash<int, QByteArray> EventLog::roleNames() const
{
    return {
        {TimeRole, "time"},
        {TimeTextRole, "timeText"},
        {LevelRole, "level"},
        {SourceRole, "source"},
        {MessageRole, "message"},
        {DetailsRole, "details"},
    };
}

}  // namespace mybench
