#include "mybench/sessionmanager.h"

#include "mybench/appinfo.h"
#include "mybench/devicehub.h"
#include "mybench/instrumentcontroller.h"
#include "mybench/instrumentregistry.h"
#include "mybench/notifications.h"
#include "mybench/resourcearbiter.h"

#include <mydaq/device_manager.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

const QString kFormat = u"mybench-session"_s;
constexpr int kVersion = 1;
constexpr int kMaxRecent = 10;
const QString kWorkspaceKey = u"workspace/layout"_s;
const QString kViewKey = u"workspace/view"_s;
const QString kViewVersionKey = u"workspace/viewVersion"_s;   // QSettings
const QString kSessionViewVersion = u"viewVersion"_s;         // session JSON
const QString kRecentKey = u"session/recent"_s;
const QString kCurrentKey = u"session/current"_s;

QString localPath(const QUrl& file)
{
    return file.isLocalFile() ? file.toLocalFile() : file.toString();
}

}  // namespace

SessionManager::SessionManager(QObject* parent) : QObject(parent)
{
    QSettings settings;
    const QByteArray layout = settings.value(kWorkspaceKey).toByteArray();
    if (!layout.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(layout);
        if (document.isObject())
            m_workspace = document.object().toVariantMap();
    }
    const QJsonDocument view = QJsonDocument::fromJson(settings.value(kViewKey).toByteArray());
    if (view.isObject())
        m_viewState = view.object().toVariantMap();
    // Once: the v1 section keys used the translated title (UI v2 keys them by section id).
    if (settings.value(kViewVersionKey, 1).toInt() < kViewStateVersion) {
        if (pruneLegacyViewKeys(m_viewState) > 0)
            settings.setValue(kViewKey, QJsonDocument(QJsonObject::fromVariantMap(m_viewState)).toJson(QJsonDocument::Compact));
        settings.setValue(kViewVersionKey, kViewStateVersion);
    }
    const QString current = settings.value(kCurrentKey).toString();
    if (!current.isEmpty() && QFileInfo::exists(current))
        m_currentFile = QUrl::fromLocalFile(current);

    m_workspaceSaveTimer.setSingleShot(true);
    m_workspaceSaveTimer.setInterval(500);
    connect(&m_workspaceSaveTimer, &QTimer::timeout, this, [this] {
        QSettings().setValue(kWorkspaceKey, QJsonDocument(QJsonObject::fromVariantMap(m_workspace))
                                                .toJson(QJsonDocument::Compact));
        QSettings().setValue(kViewKey, QJsonDocument(QJsonObject::fromVariantMap(m_viewState))
                                           .toJson(QJsonDocument::Compact));
    });
    watchControllers();
}

SessionManager* SessionManager::instance()
{
    static SessionManager* const manager = new SessionManager;
    return manager;
}

SessionManager* SessionManager::create(QQmlEngine*, QJSEngine*)
{
    SessionManager* manager = instance();
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    return manager;
}

void SessionManager::watchControllers()
{
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    auto watch = [this](InstrumentController* controller) {
        connect(controller, &InstrumentController::settingsChanged, this, [this] {
            if (!m_applying)
                setModified(true);
        });
    };
    for (InstrumentController* controller : registry->controllers())
        watch(controller);
    connect(registry, &InstrumentRegistry::controllerCreated, this, watch);
}

QJsonObject SessionManager::toJson() const
{
    QJsonObject session;
    session.insert(u"format"_s, kFormat);
    session.insert(u"version"_s, kVersion);
    session.insert(u"app"_s, QJsonObject{{u"name"_s, AppInfo::instance()->name()},
                                         {u"version"_s, AppInfo::instance()->version()}});
    const QDateTime now = QDateTime::currentDateTime();
    session.insert(u"savedAt"_s, now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate));
    session.insert(u"device"_s, QJsonObject{{u"id"_s, DeviceHub::instance()->currentId()},
                                            {u"name"_s, DeviceHub::instance()->name()}});
    session.insert(u"layout"_s, QJsonObject::fromVariantMap(m_workspace));
    session.insert(u"view"_s, QJsonObject::fromVariantMap(m_viewState));
    session.insert(kSessionViewVersion, kViewStateVersion);
    QJsonObject instruments;
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    for (const InstrumentDescriptor& descriptor : registry->descriptors()) {
        if (InstrumentController* controller = registry->controller(descriptor.id)) {
            instruments.insert(descriptor.id, QJsonObject{{u"settingsVersion"_s, 1},
                                                          {u"running"_s, controller->isRunning()},
                                                          {u"settings"_s, controller->saveSettings()}});
        }
    }
    session.insert(u"instruments"_s, instruments);
    return session;
}

bool SessionManager::fromJson(const QJsonObject& session)
{
    if (session.value(u"format"_s).toString() != kFormat) {
        setLastError(tr("This is not a %1 session file.").arg(AppInfo::instance()->name()));
        return false;
    }
    if (session.value(u"version"_s).toInt() > kVersion) {
        setLastError(tr("The session was saved by a newer version of %1.").arg(AppInfo::instance()->name()));
        return false;
    }
    m_applying = true;
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    registry->resetAllForSession();   // stop, outputs to their safe state (DIO to inputs...)
    const QJsonObject instruments = session.value(u"instruments"_s).toObject();
    QList<InstrumentController*> wereRunning;
    for (const InstrumentDescriptor& descriptor : registry->descriptors()) {
        InstrumentController* controller = registry->controller(descriptor.id);
        if (!controller)
            continue;
        const QJsonObject entry = instruments.value(descriptor.id).toObject();
        if (entry.isEmpty()) {
            controller->resetSettings();
            continue;
        }
        // {settingsVersion, running, settings} (also accepts a bare settings object).
        const bool wrapped = entry.contains(u"settings"_s) && entry.value(u"settings"_s).isObject();
        controller->restoreSettings(wrapped ? entry.value(u"settings"_s).toObject() : entry);
        if (wrapped && entry.value(u"running"_s).toBool())
            wereRunning.append(controller);
    }

    const QJsonValue deviceValue = session.value(u"device"_s);
    const QString device = deviceValue.isObject() ? deviceValue.toObject().value(u"id"_s).toString() : deviceValue.toString();
    const QString deviceName = deviceValue.isObject() ? deviceValue.toObject().value(u"name"_s).toString() : device;
    if (!device.isEmpty()) {
        if (DeviceHub::instance()->devices()->indexOf(device) >= 0 && mydaq::DeviceManager::instance().device(device))
            DeviceHub::instance()->select(device);
        else if (device != DeviceHub::instance()->currentId())
            Notifications::instance()->show(Notifications::Warning,
                                            tr("Session device %1 not found").arg(deviceName),
                                            tr("Using %1.").arg(DeviceHub::instance()->name()));
    }

    m_workspace = (session.contains(u"layout"_s) ? session.value(u"layout"_s) : session.value(u"workspace"_s))
                      .toObject().toVariantMap();
    m_viewState = session.value(u"view"_s).toObject().toVariantMap();
    if (session.value(kSessionViewVersion).toInt(1) < kViewStateVersion)
        pruneLegacyViewKeys(m_viewState);   // a session saved before UI v2
    m_workspaceSaveTimer.start();
    emit workspaceChanged();
    emit viewStateChanged();
    emit workspaceRestored(m_workspace);
    m_applying = false;
    setModified(false);

    // Optionally restart what was running, never preempting anything (app.md 5.2 rule 4).
    if (QSettings().value(u"preferences/resumeRunning"_s, false).toBool()) {
        for (InstrumentController* controller : std::as_const(wereRunning)) {
            if (ResourceArbiter::instance()->conflicts(controller).isEmpty())
                controller->run();
        }
    }
    return true;
}

void SessionManager::revertAllToDefaults()
{
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    registry->resetAllForSession();   // stop, outputs to their safe state (DIO to inputs...)
    for (InstrumentController* controller : registry->controllers())
        controller->resetSettings();
    Notifications::instance()->show(Notifications::Info, tr("Every instrument is back to its defaults"));
}

bool SessionManager::save(const QUrl& file)
{
    QString path = localPath(file);
    if (path.isEmpty()) {
        setLastError(tr("No file name given."));
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty())
        path += u'.' + fileExtension();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)
        || out.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)) < 0 || !out.commit()) {
        setLastError(tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), out.errorString()));
        Notifications::instance()->show(Notifications::Error, tr("Session not saved"), m_lastError);
        return false;
    }
    const QUrl saved = QUrl::fromLocalFile(path);
    setCurrentFile(saved);
    addRecent(saved);
    setModified(false);
    setLastError({});
    emit sessionSaved(saved);
    Notifications::instance()->show(Notifications::Success, tr("Session saved"), QFileInfo(path).fileName());
    return true;
}

bool SessionManager::saveCurrent()
{
    return m_currentFile.isValid() && save(m_currentFile);
}

bool SessionManager::load(const QUrl& file)
{
    const QString path = localPath(file);
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setLastError(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), in.errorString()));
        Notifications::instance()->show(Notifications::Error, tr("Session not loaded"), m_lastError);
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(in.readAll(), &error);
    if (!document.isObject()) {
        setLastError(tr("%1 is not valid JSON: %2").arg(QFileInfo(path).fileName(), error.errorString()));
        Notifications::instance()->show(Notifications::Error, tr("Session not loaded"), m_lastError);
        return false;
    }
    if (!fromJson(document.object())) {
        Notifications::instance()->show(Notifications::Error, tr("Session not loaded"), m_lastError);
        return false;
    }
    const QUrl loaded = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    setCurrentFile(loaded);
    addRecent(loaded);
    setLastError({});
    emit sessionLoaded(loaded);
    Notifications::instance()->show(Notifications::Success, tr("Session loaded"), QFileInfo(path).fileName());
    return true;
}

void SessionManager::newSession()
{
    m_applying = true;
    InstrumentRegistry* registry = InstrumentRegistry::instance();
    registry->resetAllForSession();   // stop, outputs to their safe state (DIO to inputs...)
    for (InstrumentController* controller : registry->controllers())
        controller->resetSettings();
    m_workspace.clear();
    m_viewState.clear();
    emit viewStateChanged();
    m_workspaceSaveTimer.start();
    emit workspaceChanged();
    emit workspaceRestored(m_workspace);
    m_applying = false;
    setCurrentFile({});
    setModified(false);
}

QString SessionManager::currentName() const
{
    return m_currentFile.isValid() ? QFileInfo(localPath(m_currentFile)).completeBaseName() : tr("Untitled");
}

QVariantList SessionManager::recentSessions() const
{
    QVariantList list;
    const QStringList paths = QSettings().value(kRecentKey).toStringList();
    for (const QString& path : paths) {
        const QFileInfo info(path);
        QVariantMap entry;
        entry.insert(u"file"_s, QUrl::fromLocalFile(path));
        entry.insert(u"path"_s, QDir::toNativeSeparators(path));
        entry.insert(u"name"_s, info.completeBaseName());
        entry.insert(u"exists"_s, info.exists());
        entry.insert(u"modified"_s, info.lastModified());
        list.append(entry);
    }
    return list;
}

void SessionManager::addRecent(const QUrl& file)
{
    const QString path = QFileInfo(localPath(file)).absoluteFilePath();
    QSettings settings;
    QStringList paths = settings.value(kRecentKey).toStringList();
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > kMaxRecent)
        paths.removeLast();
    settings.setValue(kRecentKey, paths);
    emit recentSessionsChanged();
}

void SessionManager::removeRecentSession(const QUrl& file)
{
    QSettings settings;
    QStringList paths = settings.value(kRecentKey).toStringList();
    if (paths.removeAll(QFileInfo(localPath(file)).absoluteFilePath()) > 0) {
        settings.setValue(kRecentKey, paths);
        emit recentSessionsChanged();
    }
}

void SessionManager::clearRecentSessions()
{
    QSettings().remove(kRecentKey);
    emit recentSessionsChanged();
}

QUrl SessionManager::suggestedFileUrl() const
{
    QString folder = m_currentFile.isValid() ? QFileInfo(localPath(m_currentFile)).absolutePath()
                                             : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString name = m_currentFile.isValid() ? currentName()
                                                 : u"session_%1"_s.arg(QDateTime::currentDateTime().toString(u"yyyyMMdd"_s));
    return QUrl::fromLocalFile(QDir(folder).filePath(name + u'.' + fileExtension()));
}

void SessionManager::setWorkspace(const QVariantMap& workspace)
{
    if (workspace == m_workspace)
        return;
    m_workspace = workspace;
    emit workspaceChanged();
    if (!m_applying)
        setModified(true);
    m_workspaceSaveTimer.start();
}

int SessionManager::pruneLegacyViewKeys(QVariantMap& viewState)
{
    int removed = 0;
    for (auto it = viewState.begin(); it != viewState.end();) {
        if (it.key().startsWith(u"collapsed/"_s)) {
            it = viewState.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void SessionManager::setViewValue(const QString& key, const QVariant& value)
{
    if (m_viewState.value(key) == value)
        return;
    if (value.isValid())
        m_viewState.insert(key, value);
    else
        m_viewState.remove(key);
    emit viewStateChanged();
    if (!m_applying)
        setModified(true);
    m_workspaceSaveTimer.start();
}

QStringList SessionManager::nameFilters() const
{
    return {tr("%1 sessions (*.%2)").arg(AppInfo::instance()->name(), fileExtension()), tr("All files (*)")};
}

void SessionManager::setCurrentFile(const QUrl& file)
{
    if (file == m_currentFile)
        return;
    m_currentFile = file;
    QSettings().setValue(kCurrentKey, file.isValid() ? localPath(file) : QString());
    emit currentFileChanged();
}

void SessionManager::setModified(bool modified)
{
    if (modified == m_modified)
        return;
    m_modified = modified;
    emit modifiedChanged();
}

void SessionManager::setLastError(const QString& error)
{
    if (error == m_lastError)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

}  // namespace mybench
