// EventLog: the application's event/error log (status bar error counter, log popup).
//
// Everything the user was notified about, device events and instrument errors, newest first,
// bounded (1000 entries). Also mirrored to the Qt message log (qInfo/qWarning).
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <deque>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class EventLog : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY countChanged)          // errors in the log
    Q_PROPERTY(int unreadErrorCount READ unreadErrorCount NOTIFY countChanged)  // since markRead()

public:
    enum Level { Debug, Info, Warning, Error };
    Q_ENUM(Level)

    enum Role {
        TimeRole = Qt::UserRole + 1,   // QDateTime
        TimeTextRole,                  // "10:15:02"
        LevelRole,
        SourceRole,                    // instrument id, "device", "app"
        MessageRole,
    };
    Q_ENUM(Role)

    static EventLog* instance();
    static EventLog* create(QQmlEngine*, QJSEngine*);

    // Thread-safe (queued to the UI thread when called from a worker).
    static void log(Level level, const QString& source, const QString& message);

    Q_INVOKABLE void add(int level, const QString& source, const QString& message);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void markRead();
    // Whole log as text (for "Copy" in the log popup), oldest first.
    Q_INVOKABLE QString toText() const;

    int count() const { return int(m_entries.size()); }
    int errorCount() const { return m_errorCount; }
    int unreadErrorCount() const { return m_unreadErrors; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void entryAdded(int level, const QString& source, const QString& message);

private:
    explicit EventLog(QObject* parent = nullptr);
    struct Entry {
        QDateTime time;
        Level level;
        QString source;
        QString message;
    };
    std::deque<Entry> m_entries;   // newest first
    int m_errorCount = 0;
    int m_unreadErrors = 0;
};

}  // namespace mybench
