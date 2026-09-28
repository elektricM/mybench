// Launcher: the persistent state of the Home page that replaces the ELVISmx Instrument Launcher
// (docs/instruments/app.md 18.1): favorite instruments, user tools, pinned files and the files
// exported last. Everything is stored in QSettings under "launcher/".
//
//     Launcher.favorites                       ["scope", "dmm"] (in the order they were starred)
//     Launcher.toggleFavorite("scope")
//     Launcher.tools                           [{ name, target, arguments, kind, exists }]
//     Launcher.addTool("Octave", "/usr/local/bin/octave", "")   // program, file, folder or URL
//     Launcher.launchTool(0)                   // URL/file: system handler; program: detached
//     Launcher.pinnedFiles                     [{ path, name, folder, url, exists }]
//     Launcher.pinFile(url)                    // local file URL or path
//     Launcher.recentExports                   same shape, newest first, the last 10
//                                              (filled from DataExport::exported)
//     Launcher.openFile(path) / showInFolder(path)
//     Launcher.resources                       [{ title, subtitle, url, icon }] documentation links
//     Launcher.showAbout()                     emits aboutRequested (the About dialog listens)
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class Launcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList favorites READ favorites NOTIFY favoritesChanged)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(QVariantList pinnedFiles READ pinnedFiles NOTIFY pinnedFilesChanged)
    Q_PROPERTY(QVariantList recentExports READ recentExports NOTIFY recentExportsChanged)
    Q_PROPERTY(QVariantList resources READ resources CONSTANT)
    Q_PROPERTY(QString moreToolsUrl READ moreToolsUrl CONSTANT)

public:
    static constexpr int kMaxRecentExports = 10;
    static constexpr int kMaxPinnedFiles = 50;
    static constexpr int kMaxTools = 50;

    static Launcher* instance();
    static Launcher* create(QQmlEngine*, QJSEngine*);
    // Public for tests (a fresh instance reads what the previous one stored); the application
    // uses instance(). `followExports`: record DataExport::exported paths and show launch
    // failures as warning toasts.
    explicit Launcher(QObject* parent = nullptr, bool followExports = true);

    // --- Favorites (instrument ids) -------------------------------------------------------
    QStringList favorites() const { return m_favorites; }
    Q_INVOKABLE bool isFavorite(const QString& id) const { return m_favorites.contains(id); }
    Q_INVOKABLE void setFavorite(const QString& id, bool favorite);
    Q_INVOKABLE void toggleFavorite(const QString& id) { setFavorite(id, !isFavorite(id)); }
    Q_INVOKABLE void moveFavorite(int from, int to);

    // --- User tools ("Add Item") ---------------------------------------------------------
    QVariantList tools() const;
    // Adds a tool; `target` is a URL (https:, mailto:, ...), a file URL or a local path (program,
    // document or folder). An empty name is derived from the target. Returns the new index or -1.
    Q_INVOKABLE int addTool(const QString& name, const QString& target, const QString& arguments = {});
    Q_INVOKABLE void updateTool(int index, const QString& name, const QString& target, const QString& arguments);
    Q_INVOKABLE void removeTool(int index);
    Q_INVOKABLE void moveTool(int from, int to);
    // Starts tool `index`; false (and lastError set) if it could not be started.
    Q_INVOKABLE bool launchTool(int index);
    // "url", "program", "file", "folder" or "missing" for a tool target.
    Q_INVOKABLE static QString targetKind(const QString& target);
    // Normalized target: a file URL becomes a local path, text is trimmed.
    Q_INVOKABLE static QString normalizedTarget(const QString& target);
    QString moreToolsUrl() const;

    // --- Files -----------------------------------------------------------------------------
    QVariantList pinnedFiles() const;
    Q_INVOKABLE bool pinFile(const QVariant& file);      // QUrl or path; false if not a local file
    Q_INVOKABLE void unpinFile(const QString& path);
    Q_INVOKABLE bool isPinned(const QString& path) const;
    QVariantList recentExports() const;
    Q_INVOKABLE void addRecentExport(const QString& path);
    Q_INVOKABLE void removeRecentExport(const QString& path);
    Q_INVOKABLE void clearRecentExports();
    Q_INVOKABLE bool openFile(const QString& path);
    Q_INVOKABLE bool showInFolder(const QString& path);

    // --- Resources / about -----------------------------------------------------------------
    QVariantList resources() const;
    Q_INVOKABLE bool openUrl(const QString& url);
    Q_INVOKABLE void showAbout() { emit aboutRequested(); }
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    // Re-reads everything from QSettings.
    void reload();

signals:
    void favoritesChanged();
    void toolsChanged();
    void pinnedFilesChanged();
    void recentExportsChanged();
    void aboutRequested();
    void launchFailed(const QString& message);

private:
    struct Tool {
        QString name;
        QString target;
        QString arguments;
    };

    static QVariantMap fileEntry(const QString& path);
    static QString localPath(const QVariant& file);
    void saveFavorites() const;
    void saveTools() const;
    void saveFiles() const;
    void fail(const QString& message);

    QStringList m_favorites;
    QList<Tool> m_tools;
    QStringList m_pinnedFiles;
    QStringList m_recentExports;
    QString m_lastError;
    bool m_notify = true;   // failures as warning toasts (the application instance)
};

}  // namespace mybench
