// SessionManager: session files (.mybench) and the persisted workspace layout.
//
// A session is JSON (docs/instruments/app.md, "Sessions"):
//     { "format": "mybench-session", "version": 1,
//       "app": { "name": "MYBENCH", "version": "0.1.0" },
//       "savedAt": "2026-09-28T10:15:00+02:00",
//       "device": { "id": "usb:036CD1C6", "name": "myDAQ (036CD1C6)" },
//       "layout": { ...owned by the shell (panes, tabs, dashboard, help drawer)... },
//       "instruments": { "scope": { "settingsVersion": 1, "running": false,
//                                   "settings": { ...InstrumentController::saveSettings()... } } } }
//
// The shell keeps `workspace` (= "layout") up to date (it is also auto-saved in QSettings, so the
// layout comes back at the next start); load() stops every instrument, restores their settings,
// selects the device if present (toast otherwise), optionally restarts the instruments that were
// running (preference "preferences/resumeRunning", default off, never preempting anything) and
// emits workspaceRestored() for the shell to rebuild its layout.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class SessionManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QUrl currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentFileChanged)   // "Untitled" or file base name
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)              // since last save/load
    // [{ file: url, path: string, name: string, opened: Date }], most recent first (max 10).
    Q_PROPERTY(QVariantList recentSessions READ recentSessions NOTIFY recentSessionsChanged)
    // Layout of the shell (opaque to the framework). Persisted in QSettings when set.
    Q_PROPERTY(QVariantMap workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
    // View state of the panels ("view" in the session): collapsed inspector sections
    // ("collapsed/<instrument>/<section title>": true)... Persisted like the layout.
    Q_PROPERTY(QVariantMap viewState READ viewState NOTIFY viewStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString fileExtension READ fileExtension CONSTANT)   // "mybench"
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)

public:
    static SessionManager* instance();
    static SessionManager* create(QQmlEngine*, QJSEngine*);

    // Whole session as JSON (current device, workspace, every registered instrument).
    QJsonObject toJson() const;
    // Applies a session. Returns false (and sets lastError) for an invalid document.
    bool fromJson(const QJsonObject& session);

    Q_INVOKABLE bool save(const QUrl& file);
    Q_INVOKABLE bool saveCurrent();   // to currentFile (false if none: use save() with a dialog)
    Q_INVOKABLE bool load(const QUrl& file);
    // New, untitled session: every instrument back to its defaults, default layout.
    Q_INVOKABLE void newSession();
    // Every instrument back to its defaults (layout kept).
    Q_INVOKABLE void revertAllToDefaults();
    Q_INVOKABLE void clearRecentSessions();
    Q_INVOKABLE void removeRecentSession(const QUrl& file);
    Q_INVOKABLE QUrl suggestedFileUrl() const;   // Documents/<name>.mybench

    QUrl currentFile() const { return m_currentFile; }
    QString currentName() const;
    bool isModified() const { return m_modified; }
    QVariantList recentSessions() const;
    QVariantMap workspace() const { return m_workspace; }
    QVariantMap viewState() const { return m_viewState; }
    Q_INVOKABLE QVariant viewValue(const QString& key, const QVariant& fallback = QVariant()) const
    {
        return m_viewState.value(key, fallback);
    }
    // Sets one view-state value (marks the session modified; auto-saved).
    Q_INVOKABLE void setViewValue(const QString& key, const QVariant& value);
    void setWorkspace(const QVariantMap& workspace);
    QString lastError() const { return m_lastError; }
    QString fileExtension() const { return QStringLiteral("mybench"); }
    QStringList nameFilters() const;

signals:
    void currentFileChanged();
    void modifiedChanged();
    void recentSessionsChanged();
    void workspaceChanged();
    void viewStateChanged();
    void lastErrorChanged();
    // After load()/newSession(): the shell rebuilds its layout from `workspace` (may be empty =
    // default layout).
    void workspaceRestored(const QVariantMap& workspace);
    void sessionLoaded(const QUrl& file);
    void sessionSaved(const QUrl& file);

private:
    explicit SessionManager(QObject* parent = nullptr);
    void setCurrentFile(const QUrl& file);
    void setModified(bool modified);
    void setLastError(const QString& error);
    void addRecent(const QUrl& file);
    void watchControllers();

    QUrl m_currentFile;
    bool m_modified = false;
    bool m_applying = false;
    QVariantMap m_workspace;
    QVariantMap m_viewState;
    QString m_lastError;
    QTimer m_workspaceSaveTimer;
};

}  // namespace mybench
