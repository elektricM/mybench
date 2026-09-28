// Notifications: the toast/banner queue shown by the shell (ToastArea in MyBench.Controls).
//
// Non-blocking messages for device errors, preemption ("Scope paused — AI in use by Bode" with a
// Resume action), hot-plug, exports. Every notification is also written to the EventLog.
// Info/success toasts expire after a few seconds, warnings and errors stay longer; hovering a
// toast (hold()) keeps it. Identical notifications shown again while visible are merged (count).
// At most kMaxVisible (3) toasts are stacked (app.md 9.1): older ones are dropped (they stay in
// the EventLog). An error shown without an action gets a `Details` action that emits
// detailsRequested() (the shell opens the event log).
#pragma once

#include <QAbstractListModel>
#include <QBasicTimer>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <vector>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class Notifications : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Level { Info, Success, Warning, Error };
    Q_ENUM(Level)

    enum Role {
        ToastIdRole = Qt::UserRole + 1,
        LevelRole,
        TitleRole,
        MessageRole,
        ActionTextRole,
        SourceRole,       // instrument id or "" (app / device)
        RepeatRole,       // how many times it was shown (merged duplicates)
        TimeRole,         // QDateTime
    };
    Q_ENUM(Role)

    static Notifications* instance();
    static Notifications* create(QQmlEngine*, QJSEngine*);

    // Shows a toast; returns its id. `timeoutMs` < 0: default for the level; 0: until dismissed.
    // `action` runs (UI thread) when the user clicks the action button.
    int show(Level level, const QString& title, const QString& message = {},
             const QString& actionText = {}, std::function<void()> action = {},
             const QString& source = {}, int timeoutMs = -1);

    // QML variants (no callback: use the returned id and the `actionTriggered` signal).
    Q_INVOKABLE int notify(int level, const QString& title, const QString& message = {},
                           const QString& actionText = {}, int timeoutMs = -1);
    Q_INVOKABLE void dismiss(int toastId);
    Q_INVOKABLE void dismissSource(const QString& source);   // e.g. the pause banner of a resumed instrument
    Q_INVOKABLE void trigger(int toastId);                   // runs the action, then dismisses
    Q_INVOKABLE void hold(int toastId, bool held);            // pointer over the toast: do not expire
    Q_INVOKABLE void clear();

    int count() const { return int(m_items.size()); }
    static constexpr int kMaxVisible = 3;

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    void actionTriggered(int toastId);
    // The `Details` action of an error toast was clicked (`source` of that toast).
    void detailsRequested(const QString& source);

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    explicit Notifications(QObject* parent = nullptr);
    struct Item {
        int id = 0;
        Level level = Info;
        QString title;
        QString message;
        QString actionText;
        QString source;
        std::function<void()> action;
        QDateTime time;
        int repeat = 1;
        int timeoutMs = 0;
        qint64 expiresAt = 0;   // ms since epoch, 0 = never
        bool held = false;
    };
    int rowOf(int toastId) const;
    void scheduleTimer();

    std::vector<Item> m_items;   // newest last
    int m_nextId = 1;
    QBasicTimer m_timer;
};

}  // namespace mybench
