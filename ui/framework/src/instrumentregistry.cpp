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

QUrl ViewDescriptor::panelUrl(const QString& module) const
{
    return moduleFileUrl(module, panelType);
}

const ViewDescriptor* InstrumentDescriptor::view(const QString& viewId) const
{
    for (const ViewDescriptor& v : views) {
        if (v.id == viewId)
            return &v;
    }
    return nullptr;
}

QUrl InstrumentDescriptor::viewUrl(const QString& viewId) const
{
    const ViewDescriptor* v = view(viewId);
    return v ? v->panelUrl(module) : QUrl();
}

namespace {

QVariantMap viewMap(const InstrumentDescriptor& d, const ViewDescriptor& v)
{
    return {{u"id"_s, v.id},
            {u"title"_s, v.title},
            {u"icon"_s, v.icon},
            {u"panelType"_s, v.panelType},
            {u"panelUrl"_s, v.panelUrl(d.module)},
            {u"instrumentId"_s, d.id}};
}

QVariantList viewList(const InstrumentDescriptor& d)
{
    QVariantList list;
    for (const ViewDescriptor& v : d.views)
        list.append(viewMap(d, v));
    return list;
}

}  // namespace

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
    static const QStringList groups = {u"measure"_s, u"generate"_s, u"digital"_s, u"apps"_s};
    if (!groups.contains(descriptor.group)) {
        if (!descriptor.group.isEmpty())
            qWarning("InstrumentRegistry: unknown group \"%s\" for %s (measure, generate, digital or apps)",
                     qPrintable(descriptor.group), qPrintable(descriptor.id));
        descriptor.group = u"apps"_s;
    }
    for (ViewDescriptor& view : descriptor.views) {
        if (view.icon.isEmpty())
            view.icon = descriptor.icon;
    }

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
    connect(controller, &InstrumentController::keyValueChanged, this, [this, id] {
        const int row = indexOf(id);
        if (row >= 0)
            emit dataChanged(index(row), index(row), {KeyValueRole});
    });
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
    map.insert(u"group"_s, d->group);
    map.insert(u"views"_s, viewList(*d));
    return map;
}

QVariantMap InstrumentRegistry::viewInfo(const QString& viewId) const
{
    const InstrumentDescriptor* d = descriptor(viewId.section(u'/', 0, 0));
    const ViewDescriptor* v = d ? d->view(viewId) : nullptr;
    return v ? viewMap(*d, *v) : QVariantMap();
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
    case GroupRole:
        return d.group;
    case ViewsRole:
        return viewList(d);
    case KeyValueRole:
        return controller ? controller->keyValue() : QString();
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
        {GroupRole, "group"},
        {ViewsRole, "views"},
        {KeyValueRole, "keyValue"},
    };
}

}  // namespace mybench
