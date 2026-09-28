#include "mybench/launcher.h"

#include "mybench/appinfo.h"
#include "mybench/dataexport.h"
#include "mybench/notifications.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QProcess>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

const QString kFavorites = u"launcher/favorites"_s;
const QString kTools = u"launcher/tools"_s;
const QString kPinnedFiles = u"launcher/pinnedFiles"_s;
const QString kRecentExports = u"launcher/recentExports"_s;

QStringList readList(const QSettings& settings, const QString& key)
{
    QStringList list = settings.value(key).toStringList();
    list.removeAll(QString());
    list.removeDuplicates();
    return list;
}

bool validMove(qsizetype size, int from, int to)
{
    return from >= 0 && to >= 0 && from < size && to < size && from != to;
}

}  // namespace

Launcher::Launcher(QObject* parent, bool followExports) : QObject(parent), m_notify(followExports)
{
    reload();
    if (followExports)
        connect(DataExport::instance(), &DataExport::exported, this, &Launcher::addRecentExport);
}

Launcher* Launcher::instance()
{
    static Launcher* const launcher = new Launcher;
    return launcher;
}

Launcher* Launcher::create(QQmlEngine*, QJSEngine*)
{
    Launcher* launcher = instance();
    QJSEngine::setObjectOwnership(launcher, QJSEngine::CppOwnership);
    return launcher;
}

void Launcher::reload()
{
    QSettings settings;
    m_favorites = readList(settings, kFavorites);
    m_pinnedFiles = readList(settings, kPinnedFiles).mid(0, kMaxPinnedFiles);
    m_recentExports = readList(settings, kRecentExports).mid(0, kMaxRecentExports);

    m_tools.clear();
    const int count = settings.beginReadArray(kTools);
    for (int i = 0; i < count && m_tools.size() < kMaxTools; ++i) {
        settings.setArrayIndex(i);
        Tool tool{settings.value(u"name"_s).toString(), settings.value(u"target"_s).toString(),
                  settings.value(u"arguments"_s).toString()};
        if (!tool.target.isEmpty())
            m_tools.append(tool);
    }
    settings.endArray();

    emit favoritesChanged();
    emit toolsChanged();
    emit pinnedFilesChanged();
    emit recentExportsChanged();
}

// --- Favorites ----------------------------------------------------------------------------------

void Launcher::setFavorite(const QString& id, bool favorite)
{
    if (id.isEmpty() || favorite == isFavorite(id))
        return;
    if (favorite)
        m_favorites.append(id);
    else
        m_favorites.removeAll(id);
    saveFavorites();
    emit favoritesChanged();
}

void Launcher::moveFavorite(int from, int to)
{
    if (!validMove(m_favorites.size(), from, to))
        return;
    m_favorites.move(from, to);
    saveFavorites();
    emit favoritesChanged();
}

void Launcher::saveFavorites() const
{
    QSettings().setValue(kFavorites, m_favorites);
}

// --- Tools --------------------------------------------------------------------------------------

QString Launcher::normalizedTarget(const QString& target)
{
    const QString text = target.trimmed();
    const QUrl url(text);
    if (url.isLocalFile())
        return QDir::toNativeSeparators(url.toLocalFile());
    return text;
}

QString Launcher::targetKind(const QString& target)
{
    const QString text = normalizedTarget(target);
    if (text.isEmpty())
        return u"missing"_s;
    // A scheme of two or more letters ("https:", "mailto:"); "C:\..." is a drive letter.
    const QUrl url(text, QUrl::StrictMode);
    if (url.isValid() && url.scheme().size() > 1 && !url.isLocalFile())
        return u"url"_s;
    const QFileInfo info(text);
    if (!info.exists())
        return u"missing"_s;
    if (info.isBundle())                     // macOS .app: opened by the system like a file
        return u"program"_s;
    if (info.isDir())
        return u"folder"_s;
#if defined(Q_OS_WIN)
    const QString suffix = info.suffix().toLower();
    if (suffix == u"exe"_s || suffix == u"bat"_s || suffix == u"cmd"_s || suffix == u"com"_s)
        return u"program"_s;
    return u"file"_s;
#else
    return info.isExecutable() ? u"program"_s : u"file"_s;
#endif
}

QVariantList Launcher::tools() const
{
    QVariantList list;
    for (const Tool& tool : m_tools) {
        const QString kind = targetKind(tool.target);
        list.append(QVariantMap{
            {u"name"_s, tool.name},
            {u"target"_s, tool.target},
            {u"arguments"_s, tool.arguments},
            {u"kind"_s, kind},
            {u"exists"_s, kind != u"missing"_s},
        });
    }
    return list;
}

int Launcher::addTool(const QString& name, const QString& target, const QString& arguments)
{
    const QString normalized = normalizedTarget(target);
    if (normalized.isEmpty() || m_tools.size() >= kMaxTools)
        return -1;
    QString title = name.trimmed();
    if (title.isEmpty()) {
        const QUrl url(normalized, QUrl::StrictMode);
        title = targetKind(normalized) == u"url"_s ? url.host() : QFileInfo(normalized).completeBaseName();
        if (title.isEmpty())
            title = normalized;
    }
    m_tools.append({title, normalized, arguments.trimmed()});
    saveTools();
    emit toolsChanged();
    return int(m_tools.size() - 1);
}

void Launcher::updateTool(int index, const QString& name, const QString& target, const QString& arguments)
{
    const QString normalized = normalizedTarget(target);
    if (index < 0 || index >= m_tools.size() || normalized.isEmpty())
        return;
    Tool& tool = m_tools[index];
    tool.name = name.trimmed().isEmpty() ? tool.name : name.trimmed();
    tool.target = normalized;
    tool.arguments = arguments.trimmed();
    saveTools();
    emit toolsChanged();
}

void Launcher::removeTool(int index)
{
    if (index < 0 || index >= m_tools.size())
        return;
    m_tools.removeAt(index);
    saveTools();
    emit toolsChanged();
}

void Launcher::moveTool(int from, int to)
{
    if (!validMove(m_tools.size(), from, to))
        return;
    m_tools.move(from, to);
    saveTools();
    emit toolsChanged();
}

bool Launcher::launchTool(int index)
{
    if (index < 0 || index >= m_tools.size())
        return false;
    const Tool tool = m_tools.at(index);
    const QString kind = targetKind(tool.target);
    if (kind == u"missing"_s) {
        fail(tr("%1: %2 was not found").arg(tool.name, tool.target));
        return false;
    }
    if (kind == u"url"_s)
        return openUrl(tool.target);
    if (kind == u"program"_s && !QFileInfo(tool.target).isBundle()) {
        const QStringList arguments = QProcess::splitCommand(tool.arguments);
        if (!QProcess::startDetached(tool.target, arguments, QFileInfo(tool.target).absolutePath())) {
            fail(tr("%1 could not be started").arg(tool.name));
            return false;
        }
        return true;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(tool.target))) {
        fail(tr("%1 could not be opened").arg(tool.name));
        return false;
    }
    return true;
}

QString Launcher::moreToolsUrl() const
{
    return AppInfo::instance()->homepage();
}

void Launcher::saveTools() const
{
    QSettings settings;
    settings.remove(kTools);
    settings.beginWriteArray(kTools, int(m_tools.size()));
    for (int i = 0; i < m_tools.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(u"name"_s, m_tools[i].name);
        settings.setValue(u"target"_s, m_tools[i].target);
        settings.setValue(u"arguments"_s, m_tools[i].arguments);
    }
    settings.endArray();
}

// --- Files --------------------------------------------------------------------------------------

QString Launcher::localPath(const QVariant& file)
{
    QString path;
    if (file.metaType().id() == QMetaType::QUrl) {
        const QUrl url = file.toUrl();
        if (!url.isLocalFile())
            return {};
        path = url.toLocalFile();
    } else {
        const QString text = file.toString().trimmed();
        const QUrl url(text);
        path = url.isLocalFile() ? url.toLocalFile() : text;
    }
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Folder for display: the home directory shown as "~" (macOS/Linux), native separators.
static QString displayFolder(const QString& folder)
{
    QString text = QDir::cleanPath(folder);
#if !defined(Q_OS_WIN)
    const QString home = QDir::homePath();
    if (text == home)
        return u"~"_s;
    if (text.startsWith(home + u'/'))
        text = u"~"_s + text.mid(home.size());
#endif
    return QDir::toNativeSeparators(text);
}

QVariantMap Launcher::fileEntry(const QString& path)
{
    const QFileInfo info(path);
    return {
        {u"path"_s, QDir::toNativeSeparators(path)},
        {u"name"_s, info.fileName()},
        {u"folder"_s, displayFolder(info.absolutePath())},
        {u"url"_s, QUrl::fromLocalFile(path)},
        {u"exists"_s, info.exists()},
        {u"suffix"_s, info.suffix().toLower()},
        {u"modified"_s, info.exists() ? QVariant(info.lastModified()) : QVariant()},
    };
}

QVariantList Launcher::pinnedFiles() const
{
    QVariantList list;
    for (const QString& path : m_pinnedFiles)
        list.append(fileEntry(path));
    return list;
}

bool Launcher::pinFile(const QVariant& file)
{
    const QString path = localPath(file);
    if (path.isEmpty())
        return false;
    if (m_pinnedFiles.contains(path))
        return true;
    if (m_pinnedFiles.size() >= kMaxPinnedFiles)
        return false;
    m_pinnedFiles.append(path);
    saveFiles();
    emit pinnedFilesChanged();
    return true;
}

void Launcher::unpinFile(const QString& path)
{
    if (m_pinnedFiles.removeAll(localPath(path)) == 0)
        return;
    saveFiles();
    emit pinnedFilesChanged();
}

bool Launcher::isPinned(const QString& path) const
{
    return m_pinnedFiles.contains(localPath(path));
}

QVariantList Launcher::recentExports() const
{
    QVariantList list;
    for (const QString& path : m_recentExports)
        list.append(fileEntry(path));
    return list;
}

void Launcher::addRecentExport(const QString& file)
{
    const QString path = localPath(file);
    if (path.isEmpty())
        return;
    m_recentExports.removeAll(path);
    m_recentExports.prepend(path);
    while (m_recentExports.size() > kMaxRecentExports)
        m_recentExports.removeLast();
    saveFiles();
    emit recentExportsChanged();
}

void Launcher::removeRecentExport(const QString& file)
{
    if (m_recentExports.removeAll(localPath(file)) == 0)
        return;
    saveFiles();
    emit recentExportsChanged();
}

void Launcher::clearRecentExports()
{
    if (m_recentExports.isEmpty())
        return;
    m_recentExports.clear();
    saveFiles();
    emit recentExportsChanged();
}

bool Launcher::openFile(const QString& file)
{
    const QString path = localPath(file);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        fail(tr("%1 no longer exists").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        fail(tr("No application opens %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    return true;
}

bool Launcher::showInFolder(const QString& file)
{
    const QString path = localPath(file);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.dir().exists()) {
        fail(tr("The folder of %1 no longer exists").arg(QDir::toNativeSeparators(path)));
        return false;
    }
#if defined(Q_OS_MACOS)
    if (info.exists())
        return QProcess::startDetached(u"/usr/bin/open"_s, {u"-R"_s, path});
#elif defined(Q_OS_WIN)
    if (info.exists())
        return QProcess::startDetached(u"explorer.exe"_s, {u"/select,"_s + QDir::toNativeSeparators(path)});
#endif
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void Launcher::saveFiles() const
{
    QSettings settings;
    settings.setValue(kPinnedFiles, m_pinnedFiles);
    settings.setValue(kRecentExports, m_recentExports);
}

// --- Resources ----------------------------------------------------------------------------------

QVariantList Launcher::resources() const
{
    const QString homepage = AppInfo::instance()->homepage();
    auto entry = [](const QString& title, const QString& subtitle, const QString& url, const QString& icon) {
        return QVariantMap{{u"title"_s, title}, {u"subtitle"_s, subtitle}, {u"url"_s, url}, {u"icon"_s, icon}};
    };
    return {
        entry(tr("NI myDAQ User Guide"), tr("Connections, signals and circuits · PDF"),
              u"https://www.ni.com/docs/en-US/bundle/mydaq-features/resource/373060g.pdf"_s, u"file"_s),
        entry(tr("NI myDAQ Specifications"), tr("Ranges, accuracy and limits"),
              u"https://www.ni.com/docs/en-US/bundle/mydaq-specs/page/specs.html"_s, u"ruler"_s),
        entry(tr("NI myDAQ Getting Started"), tr("Setting up the myDAQ · PDF"),
              u"https://www.ni.com/docs/en-US/bundle/mydaq-getting-started/resource/373059c.pdf"_s, u"info"_s),
        entry(tr("NI Community forum"), tr("Questions about the hardware"),
              u"https://forums.ni.com/"_s, u"help-circle"_s),
        entry(tr("%1 project").arg(AppInfo::instance()->name()), tr("Source, releases, issue tracker"),
              homepage, u"external"_s),
    };
}

bool Launcher::openUrl(const QString& text)
{
    const QUrl url = QUrl::fromUserInput(text.trimmed());
    if (!url.isValid() || !QDesktopServices::openUrl(url)) {
        fail(tr("Could not open %1").arg(text));
        return false;
    }
    return true;
}

void Launcher::fail(const QString& message)
{
    m_lastError = message;
    emit launchFailed(message);
    if (m_notify)
        Notifications::instance()->show(Notifications::Warning, message);
}

}  // namespace mybench
