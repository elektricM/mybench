#include "mybench/devicehub.h"

#include "mybench/eventlog.h"
#include "mybench/instrumentcontroller.h"
#include "mybench/instrumentregistry.h"
#include "mybench/notifications.h"

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QElapsedTimer>
#include <QJSEngine>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <thread>

using namespace Qt::StringLiterals;

namespace mybench {

namespace {

const QString kCurrentKey = u"device/current"_s;

}  // namespace

// --- DeviceListModel -----------------------------------------------------------------------

DeviceListModel::DeviceListModel(QObject* parent) : QAbstractListModel(parent) {}

void DeviceListModel::setEntries(const QList<Entry>& entries)
{
    const bool sameShape = [&] {
        if (entries.size() != m_entries.size())
            return false;
        for (qsizetype i = 0; i < entries.size(); ++i) {
            if (entries[i].info.id != m_entries[i].info.id)
                return false;
        }
        return true;
    }();
    if (sameShape) {
        m_entries = entries;
        if (!m_entries.isEmpty())
            emit dataChanged(index(0), index(int(m_entries.size()) - 1));
        return;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    emit countChanged();
}

int DeviceListModel::indexOf(const QString& deviceId) const
{
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].info.id == deviceId)
            return int(i);
    }
    return -1;
}

QString DeviceListModel::deviceIdAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries[row].info.id : QString();
}

int DeviceListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry& entry = m_entries[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return entry.connected ? entry.info.name : tr("%1 (disconnected)").arg(entry.info.name);
    case DeviceIdRole:
        return entry.info.id;
    case NameRole:
        return entry.info.name;
    case ProductTypeRole:
        return entry.info.productType;
    case SerialNumberRole:
        return entry.info.serialNumber;
    case SimulatedRole:
        return entry.info.simulated;
    case ConnectedRole:
        return entry.connected;
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        {DeviceIdRole, "deviceId"},
        {NameRole, "name"},
        {DisplayNameRole, "displayName"},
        {ProductTypeRole, "productType"},
        {SerialNumberRole, "serialNumber"},
        {SimulatedRole, "simulated"},
        {ConnectedRole, "connected"},
    };
}

// --- DeviceHub -----------------------------------------------------------------------------

DeviceHub::DeviceHub(QObject* parent) : QObject(parent), m_model(new DeviceListModel(this))
{
    mydaq::DeviceManager& manager = mydaq::DeviceManager::instance();
    m_simulator = std::dynamic_pointer_cast<mydaq::SimDevice>(manager.device(u"sim:0"_s));
    connect(&manager, &mydaq::DeviceManager::devicesChanged, this, &DeviceHub::refresh);

    for (const mydaq::DevicePtr& device : manager.devices())
        m_presentIds.append(device->info().id);

    const QString stored = QSettings().value(kCurrentKey).toString();
    QString id;
    if (!stored.isEmpty() && manager.device(stored))
        id = stored;
    else if (const mydaq::DevicePtr device = manager.defaultDevice())
        id = device->info().id;
    setCurrent(id, false);
    refresh();
}

DeviceHub* DeviceHub::instance()
{
    static DeviceHub* const hub = new DeviceHub;
    return hub;
}

DeviceHub* DeviceHub::create(QQmlEngine*, QJSEngine*)
{
    DeviceHub* hub = instance();
    QJSEngine::setObjectOwnership(hub, QJSEngine::CppOwnership);
    return hub;
}

mydaq::DevicePtr DeviceHub::device() const
{
    std::lock_guard lock(m_mutex);
    return m_device;
}

mydaq::DeviceInfo DeviceHub::info() const
{
    return m_currentInfo;
}

int DeviceHub::currentIndex() const
{
    return m_model->indexOf(m_currentId);
}

DeviceHub::ConnectionState DeviceHub::connectionState() const
{
    if (m_resetting)
        return Resetting;
    if (m_currentId.isEmpty())
        return NoDevice;
    const mydaq::DevicePtr current = device();
    return current && current->isConnected() ? Connected : Disconnected;
}

void DeviceHub::setCurrent(const QString& deviceId, bool persist)
{
    mydaq::DeviceManager& manager = mydaq::DeviceManager::instance();
    const mydaq::DevicePtr found = deviceId.isEmpty() ? nullptr : manager.device(deviceId);
    m_currentId = deviceId;
    if (found)
        m_currentInfo = found->info();
    else if (deviceId.isEmpty())
        m_currentInfo = {};
    {
        std::lock_guard lock(m_mutex);
        m_device = found;
    }
    if (persist)
        QSettings().setValue(kCurrentKey, deviceId);
    emit currentChanged();
    emit connectionChanged();
}

void DeviceHub::select(const QString& deviceId)
{
    if (deviceId == m_currentId || m_resetting)
        return;
    if (!mydaq::DeviceManager::instance().device(deviceId))
        return;
    InstrumentRegistry::instance()->stopAll();
    setCurrent(deviceId, true);
    refresh();
    EventLog::log(EventLog::Info, u"device"_s, tr("Selected %1").arg(m_currentInfo.name));
}

void DeviceHub::selectIndex(int row)
{
    select(m_model->deviceIdAt(row));
}

void DeviceHub::rescan()
{
    mydaq::DeviceManager::instance().rescan();
}

void DeviceHub::refresh()
{
    mydaq::DeviceManager& manager = mydaq::DeviceManager::instance();
    const QList<mydaq::DevicePtr> devices = manager.devices();

    QList<DeviceListModel::Entry> entries;
    QStringList ids;
    QHash<QString, QString> names;
    for (const mydaq::DevicePtr& device : devices) {
        const mydaq::DeviceInfo info = device->info();
        ids.append(info.id);
        names.insert(info.id, info.name);
        entries.append({info, device->isConnected()});
    }

    // Hot-plug notifications.
    const QStringList previous = m_presentIds;
    m_presentIds = ids;
    for (const QString& id : ids) {
        if (previous.contains(id))
            continue;
        const QString name = names.value(id);
        EventLog::log(EventLog::Info, u"device"_s, tr("%1 connected").arg(name));
        emit deviceArrived(id, name);
        const mydaq::DevicePtr arrived = manager.device(id);
        const bool real = arrived && !arrived->info().simulated;
        QPointer<DeviceHub> self(this);
        if (id == m_currentId) {
            Notifications::instance()->show(Notifications::Success, tr("%1 reconnected").arg(name),
                                            tr("Press Resume or Run to continue."), {}, {}, u"device"_s);
        } else if (real && m_currentInfo.simulated && !previous.isEmpty()
                   && !InstrumentRegistry::instance()->anyRunning()) {
            // A real myDAQ appears while the simulator is used and nothing runs: use it.
            const QString before = m_currentId;
            QMetaObject::invokeMethod(this, [self, id, before, name] {
                if (!self)
                    return;
                self->select(id);
                Notifications::instance()->show(Notifications::Success, tr("%1 connected — now using it").arg(name), {},
                                                tr("Undo"), [self, before] { if (self) self->select(before); },
                                                u"device"_s);
            }, Qt::QueuedConnection);
        } else {
            Notifications::instance()->show(Notifications::Info, tr("%1 connected").arg(name), {},
                                            tr("Switch"), [self, id] { if (self) self->select(id); },
                                            u"device"_s);
        }
    }
    bool currentLost = false;
    for (const QString& id : previous) {
        if (ids.contains(id))
            continue;
        const bool isCurrent = id == m_currentId;
        currentLost = currentLost || isCurrent;
        const QString name = isCurrent ? m_currentInfo.name : id;
        EventLog::log(isCurrent ? EventLog::Warning : EventLog::Info, u"device"_s, tr("%1 disconnected").arg(name));
        emit deviceLost(id, name);
        if (isCurrent) {
            Notifications::instance()->show(Notifications::Error, tr("%1 disconnected").arg(name),
                                            tr("Reconnect it, or select another device."), {}, {}, u"device"_s);
        }
    }

    // The selection stays listed while unplugged.
    if (!m_currentId.isEmpty() && !ids.contains(m_currentId))
        entries.append({m_currentInfo, false});

    // First device ever (nothing was selected): select it.
    if (m_currentId.isEmpty() && !devices.isEmpty()) {
        m_model->setEntries(entries);
        if (const mydaq::DevicePtr fallback = manager.defaultDevice())
            setCurrent(fallback->info().id, false);
        return;
    }

    const ConnectionState before = connectionState();
    const mydaq::DevicePtr current = m_currentId.isEmpty() ? nullptr : manager.device(m_currentId);
    {
        std::lock_guard lock(m_mutex);
        m_device = current;
    }
    if (current)
        m_currentInfo = current->info();
    const int indexBefore = currentIndex();
    m_model->setEntries(entries);

    if (currentLost) {
        for (InstrumentController* controller : InstrumentRegistry::instance()->controllers())
            controller->deviceDisconnected();
    }
    if (connectionState() != before)
        emit connectionChanged();
    if (currentIndex() != indexBefore || currentLost)
        emit currentChanged();
}

void DeviceHub::reset()
{
    if (m_resetting)
        return;
    const mydaq::DevicePtr current = device();
    if (!current) {
        Notifications::instance()->show(Notifications::Warning, tr("No device to reset"),
                                        tr("Connect a myDAQ or select the simulated device."));
        return;
    }
    m_resetting = true;
    emit connectionChanged();

    InstrumentRegistry* registry = InstrumentRegistry::instance();
    registry->stopAll();

    // Reset once every instrument released the hardware (bounded wait), off the UI thread.
    auto* waiter = new QTimer(this);
    waiter->setInterval(20);
    auto started = std::make_shared<QElapsedTimer>();
    started->start();
    QPointer<DeviceHub> self(this);
    connect(waiter, &QTimer::timeout, this, [self, waiter, started, current, registry] {
        if (registry->anyRunning() && started->elapsed() < 3000)
            return;
        waiter->stop();
        waiter->deleteLater();
        DeviceHub* hub = self.data();   // the hub is a process-wide singleton, never destroyed
        std::thread([hub, current] {
            bool ok = true;
            QString error;
            try {
                current->reset();
            } catch (const mydaq::DeviceError& e) {
                ok = false;
                error = e.message();
            } catch (const std::exception& e) {
                ok = false;
                error = QString::fromUtf8(e.what());
            }
            QMetaObject::invokeMethod(hub, [hub, ok, error] {
                hub->m_resetting = false;
                emit hub->connectionChanged();
                emit hub->resetFinished(ok, error);
                if (ok) {
                    EventLog::log(EventLog::Info, u"device"_s, tr("Device reset"));
                    Notifications::instance()->show(Notifications::Success, tr("Device reset"),
                                                    tr("Outputs at 0 V, digital lines are inputs."), {}, {}, u"device"_s);
                } else {
                    EventLog::log(EventLog::Error, u"device"_s, tr("Reset failed: %1").arg(error));
                    Notifications::instance()->show(Notifications::Error, tr("Reset failed"), error, {}, {}, u"device"_s);
                }
            }, Qt::QueuedConnection);
        }).detach();
    });
    waiter->start();
}

double DeviceHub::maxAiRate() const
{
    const mydaq::DevicePtr current = device();
    return current ? current->maxAiRate() : 0.0;
}

double DeviceHub::maxAoRate() const
{
    const mydaq::DevicePtr current = device();
    return current ? current->maxAoRate() : 0.0;
}

QString DeviceHub::backend() const
{
    if (m_currentId.isEmpty())
        return {};
    return m_currentInfo.simulated ? tr("Simulator") : u"USB"_s;
}

// --- Simulator -----------------------------------------------------------------------------

bool DeviceHub::simulatorPlugged() const
{
    return m_simulator && m_simulator->isConnected();
}

void DeviceHub::setSimulatorPlugged(bool plugged)
{
    if (!m_simulator || plugged == simulatorPlugged())
        return;
    m_simulator->setConnected(plugged);
    emit simulatorChanged();
    rescan();
}

double DeviceHub::simulatorNoiseRms() const
{
    return m_simulator ? m_simulator->noiseRms() : 0.0;
}

void DeviceHub::setSimulatorNoiseRms(double volts)
{
    if (!m_simulator || volts == simulatorNoiseRms())
        return;
    m_simulator->setNoiseRms(volts);
    emit simulatorChanged();
}

double DeviceHub::simulatorRcCutoff() const
{
    return m_simulator ? m_simulator->rcCutoff() : 0.0;
}

void DeviceHub::setSimulatorRcCutoff(double hertz)
{
    if (!m_simulator || hertz == simulatorRcCutoff())
        return;
    m_simulator->setRcCutoff(hertz);
    emit simulatorChanged();
}

bool DeviceHub::simulatorDmmOpenCircuit() const
{
    return m_simulator && m_simulator->dmmOpenCircuit();
}

void DeviceHub::setSimulatorDmmOpenCircuit(bool open)
{
    if (!m_simulator || open == simulatorDmmOpenCircuit())
        return;
    m_simulator->setDmmOpenCircuit(open);
    emit simulatorChanged();
}

}  // namespace mybench
