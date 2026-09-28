#include "mybench/notifications.h"

#include "mybench/eventlog.h"

#include <QJSEngine>
#include <QTimerEvent>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

constexpr int kMaxVisible = 5;

int defaultTimeout(Notifications::Level level)
{
    switch (level) {
    case Notifications::Info:
    case Notifications::Success:
        return 4000;
    case Notifications::Warning:
        return 8000;
    case Notifications::Error:
        return 10000;
    }
    return 5000;
}

EventLog::Level logLevel(Notifications::Level level)
{
    switch (level) {
    case Notifications::Info:
    case Notifications::Success:
        return EventLog::Info;
    case Notifications::Warning:
        return EventLog::Warning;
    case Notifications::Error:
        return EventLog::Error;
    }
    return EventLog::Info;
}

}  // namespace

Notifications::Notifications(QObject* parent) : QAbstractListModel(parent) {}

Notifications* Notifications::instance()
{
    static Notifications* const notifications = new Notifications;
    return notifications;
}

Notifications* Notifications::create(QQmlEngine*, QJSEngine*)
{
    Notifications* notifications = instance();
    QJSEngine::setObjectOwnership(notifications, QJSEngine::CppOwnership);
    return notifications;
}

int Notifications::show(Level level, const QString& title, const QString& message, const QString& actionText,
                        std::function<void()> action, const QString& source, int timeoutMs)
{
    if (timeoutMs < 0)
        timeoutMs = defaultTimeout(level);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Instrument errors are logged by the controller already; everything else is logged here.
    const QString logSource = source.isEmpty() ? u"app"_s : source.section(u':', -1);
    if (!(level == Error && !source.isEmpty() && source != u"device"_s))
        EventLog::log(logLevel(level), logSource, message.isEmpty() ? title : title + u": "_s + message);

    // Merge with an identical visible toast.
    for (size_t i = 0; i < m_items.size(); ++i) {
        Item& item = m_items[i];
        if (item.level == level && item.title == title && item.message == message && item.source == source) {
            ++item.repeat;
            item.time = QDateTime::currentDateTime();
            item.expiresAt = timeoutMs > 0 ? now + timeoutMs : 0;
            item.action = std::move(action);
            item.actionText = actionText;
            const QModelIndex idx = index(int(i));
            emit dataChanged(idx, idx);
            scheduleTimer();
            return item.id;
        }
    }

    Item item;
    item.id = m_nextId++;
    item.level = level;
    item.title = title;
    item.message = message;
    item.actionText = actionText;
    item.action = std::move(action);
    item.source = source;
    item.time = QDateTime::currentDateTime();
    item.timeoutMs = timeoutMs;
    item.expiresAt = timeoutMs > 0 ? now + timeoutMs : 0;

    const int row = int(m_items.size());
    beginInsertRows({}, row, row);
    m_items.push_back(std::move(item));
    endInsertRows();

    // Keep the stack short: drop the oldest ones.
    while (m_items.size() > size_t(kMaxVisible)) {
        beginRemoveRows({}, 0, 0);
        m_items.erase(m_items.begin());
        endRemoveRows();
    }
    emit countChanged();
    scheduleTimer();
    return m_items.back().id;
}

int Notifications::notify(int level, const QString& title, const QString& message, const QString& actionText,
                          int timeoutMs)
{
    return show(Level(std::clamp(level, int(Info), int(Error))), title, message, actionText, {}, {}, timeoutMs);
}

int Notifications::rowOf(int toastId) const
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == toastId)
            return int(i);
    }
    return -1;
}

void Notifications::dismiss(int toastId)
{
    const int row = rowOf(toastId);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_items.erase(m_items.begin() + row);
    endRemoveRows();
    emit countChanged();
    scheduleTimer();
}

void Notifications::dismissSource(const QString& source)
{
    for (int row = int(m_items.size()) - 1; row >= 0; --row) {
        if (m_items[size_t(row)].source == source)
            dismiss(m_items[size_t(row)].id);
    }
}

void Notifications::trigger(int toastId)
{
    const int row = rowOf(toastId);
    if (row < 0)
        return;
    std::function<void()> action = m_items[size_t(row)].action;
    dismiss(toastId);
    emit actionTriggered(toastId);
    if (action)
        action();
}

void Notifications::hold(int toastId, bool held)
{
    const int row = rowOf(toastId);
    if (row < 0)
        return;
    Item& item = m_items[size_t(row)];
    item.held = held;
    if (!held && item.timeoutMs > 0)   // give the reader a moment after leaving the toast
        item.expiresAt = std::max(item.expiresAt, QDateTime::currentMSecsSinceEpoch() + 2000);
    scheduleTimer();
}

void Notifications::clear()
{
    if (m_items.empty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
    m_timer.stop();
}

void Notifications::scheduleTimer()
{
    qint64 next = 0;
    for (const Item& item : m_items) {
        if (item.expiresAt > 0 && !item.held && (next == 0 || item.expiresAt < next))
            next = item.expiresAt;
    }
    if (next == 0) {
        m_timer.stop();
        return;
    }
    const qint64 delay = std::max<qint64>(50, next - QDateTime::currentMSecsSinceEpoch());
    m_timer.start(int(delay), this);
}

void Notifications::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_timer.timerId()) {
        QAbstractListModel::timerEvent(event);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int row = int(m_items.size()) - 1; row >= 0; --row) {
        const Item& item = m_items[size_t(row)];
        if (item.expiresAt > 0 && !item.held && item.expiresAt <= now)
            dismiss(item.id);
    }
    scheduleTimer();
}

int Notifications::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant Notifications::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_items.size()))
        return {};
    const Item& item = m_items[size_t(index.row())];
    switch (role) {
    case ToastIdRole:
        return item.id;
    case LevelRole:
        return int(item.level);
    case Qt::DisplayRole:
    case TitleRole:
        return item.title;
    case MessageRole:
        return item.message;
    case ActionTextRole:
        return item.actionText;
    case SourceRole:
        return item.source;
    case RepeatRole:
        return item.repeat;
    case TimeRole:
        return item.time;
    default:
        return {};
    }
}

QHash<int, QByteArray> Notifications::roleNames() const
{
    return {
        {ToastIdRole, "toastId"},
        {LevelRole, "level"},
        {TitleRole, "title"},
        {MessageRole, "message"},
        {ActionTextRole, "actionText"},
        {SourceRole, "source"},
        {RepeatRole, "repeat"},
        {TimeRole, "time"},
    };
}

}  // namespace mybench
