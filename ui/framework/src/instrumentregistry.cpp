#include "mybench/instrumentregistry.h"

#include "mybench/instrumentcontroller.h"

#include <QJSEngine>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

QString lastUriPart(const QString& uri)
{
    return uri.section(u'.', -1);
}

QUrl moduleFileUrl(const QString& module, const QString& type)
{
    if (module.isEmpty() || type.isEmpty())
        return {};
    QString path = module;
    path.replace(u'.', u'/');
    return QUrl(u"qrc:/qt/qml/%1/%2.qml"_s.arg(path, type));
}

}  // namespace

QUrl InstrumentDescriptor::panelUrl() const
{
    return moduleFileUrl(module, panelType.isEmpty() ? lastUriPart(module) + u"Panel"_s : panelType);
}

QUrl InstrumentDescriptor::tileUrl() const
{
    return moduleFileUrl(module, tileType.isEmpty() ? lastUriPart(module) + u"Tile"_s : tileType);
}

InstrumentRegistry::InstrumentRegistry(QObject* parent) : QAbstractListModel(parent) {}

InstrumentRegistry* InstrumentRegistry::instance()
{
    static InstrumentRegistry* const registry = new InstrumentRegistry;
    return registry;
}

InstrumentRegistry* InstrumentRegistry::create(QQmlEngine*, QJSEngine*)
{
    InstrumentRegistry* registry = instance();
    QJSEngine::setObjectOwnership(registry, QJSEngine::CppOwnership);
    return registry;
}

void InstrumentRegistry::registerInstrument(InstrumentDescriptor descriptor)
{
    Q_ASSERT_X(!descriptor.id.isEmpty(), "InstrumentRegistry", "an instrument needs an id");
    Q_ASSERT_X(bool(descriptor.createController), "InstrumentRegistry", "an instrument needs a controller factory");
    if (descriptor.shortTitle.isEmpty())
        descriptor.shortTitle = descriptor.title;
    if (descriptor.icon.isEmpty())
        descriptor.icon = u"instrument-"_s + descriptor.id;
    if (descriptor.helpId.isEmpty())
        descriptor.helpId = descriptor.id;
    if (descriptor.panelType.isEmpty())
        descriptor.panelType = lastUriPart(descriptor.module) + u"Panel"_s;
    if (descriptor.tileType.isEmpty())
        descriptor.tileType = lastUriPart(descriptor.module) + u"Tile"_s;

    beginResetModel();
    const auto existing = std::find_if(m_descriptors.begin(), m_descriptors.end(),
                                       [&](const InstrumentDescriptor& d) { return d.id == descriptor.id; });
    if (existing != m_descriptors.end())
        m_descriptors.erase(existing);
    m_descriptors.push_back(std::move(descriptor));
    std::stable_sort(m_descriptors.begin(), m_descriptors.end(),
                     [](const InstrumentDescriptor& a, const InstrumentDescriptor& b) {
                         return a.order != b.order ? a.order < b.order : a.id < b.id;
                     });
    endResetModel();
    emit countChanged();
}

const InstrumentDescriptor* InstrumentRegistry::descriptor(const QString& id) const
{
    for (const InstrumentDescriptor& descriptor : m_descriptors) {
        if (descriptor.id == id)
            return &descriptor;
    }
    return nullptr;
}

InstrumentController* InstrumentRegistry::controller(const QString& id)
{
    if (InstrumentController* existing = m_controllers.value(id))
        return existing;
    const InstrumentDescriptor* d = descriptor(id);
    if (!d)
        return nullptr;
    InstrumentController* controller = d->createController();
    if (!controller)
        return nullptr;
    controller->setParent(this);   // C++ ownership (QML never garbage-collects it)
    m_controllers.insert(id, controller);
    controller->initialize();
    watch(controller);
    emit controllerCreated(controller);
    const int row = indexOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    return controller;
}

void InstrumentRegistry::watch(InstrumentController* controller)
{
    const QString id = controller->instrumentId();
    auto changed = [this, id] {
        const int row = indexOf(id);
        if (row >= 0)
            emit dataChanged(index(row), index(row), {RunningRole, PausedRole, StateRole, HasErrorRole});
    };
    connect(controller, &InstrumentController::stateChanged, this, [this, changed] {
        changed();
        emit runningChanged();
    });
    connect(controller, &InstrumentController::errorChanged, this, changed);
    connect(controller, &InstrumentController::holdsOutputsChanged, this, &InstrumentRegistry::runningChanged);
}

bool InstrumentRegistry::anyStoppable() const
{
    for (InstrumentController* controller : m_controllers) {
        if (controller->isRunning() || controller->isPaused() || controller->holdsOutputs())
            return true;
    }
    return false;
}

QList<InstrumentController*> InstrumentRegistry::controllers() const
{
    QList<InstrumentController*> list;
    for (const InstrumentDescriptor& descriptor : m_descriptors) {
        if (InstrumentController* controller = m_controllers.value(descriptor.id))
            list.append(controller);
    }
    return list;
}

void InstrumentRegistry::createAll()
{
    for (const InstrumentDescriptor& descriptor : m_descriptors)
        controller(descriptor.id);
}

QVariantMap InstrumentRegistry::info(const QString& id) const
{
    QVariantMap map;
    const InstrumentDescriptor* d = descriptor(id);
    if (!d)
        return map;
    map.insert(u"instrumentId"_s, d->id);
    map.insert(u"title"_s, d->title);
    map.insert(u"shortTitle"_s, d->shortTitle);
    map.insert(u"description"_s, d->description);
    map.insert(u"icon"_s, d->icon);
    map.insert(u"module"_s, d->module);
    map.insert(u"panelUrl"_s, d->panelUrl());
    map.insert(u"tileUrl"_s, d->tileUrl());
    map.insert(u"helpId"_s, d->helpId);
    map.insert(u"order"_s, d->order);
    return map;
}

int InstrumentRegistry::indexOf(const QString& id) const
{
    for (size_t i = 0; i < m_descriptors.size(); ++i) {
        if (m_descriptors[i].id == id)
            return int(i);
    }
    return -1;
}

void InstrumentRegistry::stopAll()
{
    for (InstrumentController* controller : controllers())
        controller->stop();   // an idle controller holding outputs returns them to safe state
}

bool InstrumentRegistry::openInstrument(const QString& id, const QVariantMap& preset, bool beside)
{
    InstrumentController* target = controller(id);
    if (!target)
        return false;
    if (!preset.isEmpty())
        target->applyPreset(preset);
    emit openRequested(id, beside);
    return true;
}

void InstrumentRegistry::notifyPanelClosed(const QString& id)
{
    if (InstrumentController* existing = m_controllers.value(id))
        existing->closePanel();
}

QStringList InstrumentRegistry::unsavedDocuments() const
{
    QStringList list;
    for (const InstrumentDescriptor& descriptor : m_descriptors) {
        const InstrumentController* controller = m_controllers.value(descriptor.id);
        if (controller && !controller->unsavedDocument().isEmpty())
            list.append(descriptor.title + u": "_s + controller->unsavedDocument());
    }
    return list;
}

void InstrumentRegistry::resetAllForSession()
{
    for (InstrumentController* controller : controllers())
        controller->resetForSession();
}

void InstrumentRegistry::shutdown()
{
    // Workers are stopped and joined; the controllers stay alive (QML may still be bound to them
    // while the application exits; they are owned by the process-wide registry).
    for (InstrumentController* controller : controllers())
        controller->shutdown();
}

QStringList InstrumentRegistry::ids() const
{
    QStringList list;
    for (const InstrumentDescriptor& descriptor : m_descriptors)
        list.append(descriptor.id);
    return list;
}

int InstrumentRegistry::runningCount() const
{
    int count = 0;
    for (InstrumentController* controller : m_controllers) {
        if (controller->isRunning())
            ++count;
    }
    return count;
}

int InstrumentRegistry::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_descriptors.size());
}

QVariant InstrumentRegistry::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_descriptors.size()))
        return {};
    const InstrumentDescriptor& d = m_descriptors[size_t(index.row())];
    InstrumentController* controller = m_controllers.value(d.id);
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return d.title;
    case InstrumentIdRole:
        return d.id;
    case ShortTitleRole:
        return d.shortTitle;
    case DescriptionRole:
        return d.description;
    case IconRole:
        return d.icon;
    case PanelUrlRole:
        return d.panelUrl();
    case TileUrlRole:
        return d.tileUrl();
    case HelpIdRole:
        return d.helpId;
    case ControllerRole:   // null until created (the shells call createAll() at startup)
        return QVariant::fromValue(controller);
    case RunningRole:
        return controller && controller->isRunning();
    case PausedRole:
        return controller && controller->isPaused();
    case HasErrorRole:
        return controller && controller->hasError();
    case StateRole:
        return controller ? int(controller->state()) : int(InstrumentController::Idle);
    default:
        return {};
    }
}

QHash<int, QByteArray> InstrumentRegistry::roleNames() const
{
    return {
        {InstrumentIdRole, "instrumentId"},
        {TitleRole, "title"},
        {ShortTitleRole, "shortTitle"},
        {DescriptionRole, "description"},
        {IconRole, "icon"},
        {PanelUrlRole, "panelUrl"},
        {TileUrlRole, "tileUrl"},
        {HelpIdRole, "helpId"},
        {ControllerRole, "controller"},
        {RunningRole, "running"},
        {PausedRole, "paused"},
        {HasErrorRole, "hasError"},
        {StateRole, "state"},
    };
}

}  // namespace mybench
