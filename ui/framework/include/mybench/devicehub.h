// DeviceHub: the one device shared by every instrument.
//
// Wraps mydaq::DeviceManager for the UI: the device list (hot-plug), the selected device
// (persisted in QSettings, never switched silently: an unplugged selection stays listed as
// "<name> (disconnected)"), its connection state and info, Device::reset(), and the simulator's
// controls. Instruments get the device with DeviceHub::instance()->device() (the framework
// passes it to their worker as WorkerContext::device()).
//
// QML: the `DeviceHub` singleton. UI thread only, except device() which is thread-safe.
#pragma once

#include <mydaq/device.h>

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <mutex>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mydaq {
class SimDevice;
}

namespace mybench {

// Devices known to DeviceManager, plus the selected device while it is unplugged.
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use DeviceHub.devices")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        DeviceIdRole = Qt::UserRole + 1,
        NameRole,          // DeviceInfo::name
        DisplayNameRole,   // name, or "<name> (disconnected)"
        ProductTypeRole,
        SerialNumberRole,
        SimulatedRole,
        ConnectedRole,
    };
    Q_ENUM(Role)

    struct Entry {
        mydaq::DeviceInfo info;
        bool connected = true;
    };

    explicit DeviceListModel(QObject* parent = nullptr);

    int count() const { return int(m_entries.size()); }
    const QList<Entry>& entries() const { return m_entries; }
    void setEntries(const QList<Entry>& entries);
    Q_INVOKABLE int indexOf(const QString& deviceId) const;
    Q_INVOKABLE QString deviceIdAt(int row) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<Entry> m_entries;
};

class DeviceHub : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(mybench::DeviceListModel* devices READ devices CONSTANT)
    Q_PROPERTY(QString currentId READ currentId WRITE select NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)   // selected, present, usable
    Q_PROPERTY(bool simulated READ isSimulated NOTIFY currentChanged)
    Q_PROPERTY(QString name READ name NOTIFY currentChanged)               // "Simulated myDAQ"
    Q_PROPERTY(QString productType READ productType NOTIFY currentChanged) // "NI myDAQ"
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY currentChanged)
    Q_PROPERTY(bool resetting READ isResetting NOTIFY connectionChanged)
    // Device info dialog.
    Q_PROPERTY(double maxAiRate READ maxAiRate NOTIFY currentChanged)   // S/s per channel, 0 if unknown
    Q_PROPERTY(double maxAoRate READ maxAoRate NOTIFY currentChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY currentChanged)      // "USB", "Simulator"

    // Simulator controls (SimDevice "sim:0"; only when the simulator is built in).
    Q_PROPERTY(bool simulatorAvailable READ simulatorAvailable CONSTANT)
    Q_PROPERTY(bool simulatorPlugged READ simulatorPlugged WRITE setSimulatorPlugged NOTIFY simulatorChanged)
    Q_PROPERTY(double simulatorNoiseRms READ simulatorNoiseRms WRITE setSimulatorNoiseRms NOTIFY simulatorChanged)
    Q_PROPERTY(double simulatorRcCutoff READ simulatorRcCutoff WRITE setSimulatorRcCutoff NOTIFY simulatorChanged)
    Q_PROPERTY(bool simulatorDmmOpenCircuit READ simulatorDmmOpenCircuit WRITE setSimulatorDmmOpenCircuit NOTIFY simulatorChanged)

public:
    enum ConnectionState {
        NoDevice,       // nothing selected (no device found at all)
        Connected,      // selected device present
        Disconnected,   // selected device unplugged
        Resetting,      // Device::reset() in progress
    };
    Q_ENUM(ConnectionState)

    static DeviceHub* instance();
    static DeviceHub* create(QQmlEngine*, QJSEngine*);

    // Selected device if it is present (nullptr otherwise). Thread-safe.
    mydaq::DevicePtr device() const;
    mydaq::DeviceInfo info() const;   // of the selection (also while unplugged)
    // The built-in simulator (also while "unplugged"), or nullptr.
    std::shared_ptr<mydaq::SimDevice> simulator() const { return m_simulator; }

    DeviceListModel* devices() const { return m_model; }
    QString currentId() const { return m_currentId; }
    int currentIndex() const;
    ConnectionState connectionState() const;
    bool isConnected() const { return connectionState() == Connected; }
    bool isSimulated() const { return m_currentInfo.simulated; }
    QString name() const { return m_currentInfo.name; }
    QString productType() const { return m_currentInfo.productType; }
    QString serialNumber() const { return m_currentInfo.serialNumber; }
    bool isResetting() const { return m_resetting; }
    double maxAiRate() const;
    double maxAoRate() const;
    QString backend() const;

    bool simulatorAvailable() const { return m_simulator != nullptr; }
    bool simulatorPlugged() const;
    void setSimulatorPlugged(bool plugged);
    double simulatorNoiseRms() const;
    void setSimulatorNoiseRms(double volts);
    double simulatorRcCutoff() const;
    void setSimulatorRcCutoff(double hertz);
    bool simulatorDmmOpenCircuit() const;
    void setSimulatorDmmOpenCircuit(bool open);

public slots:
    // Selects a device by id (stops running instruments first; persisted).
    void select(const QString& deviceId);
    void selectIndex(int row);
    void rescan();
    // Stops every instrument, then runs Device::reset() off the UI thread (outputs to 0 V, DIO
    // to inputs). resetFinished() reports the result.
    void reset();

signals:
    void currentChanged();
    void connectionChanged();
    void simulatorChanged();
    void deviceArrived(const QString& deviceId, const QString& name);
    void deviceLost(const QString& deviceId, const QString& name);
    void resetFinished(bool ok, const QString& error);

private:
    explicit DeviceHub(QObject* parent = nullptr);
    void refresh();
    void setCurrent(const QString& deviceId, bool persist);

    DeviceListModel* m_model;
    std::shared_ptr<mydaq::SimDevice> m_simulator;
    QString m_currentId;
    mydaq::DeviceInfo m_currentInfo;
    QStringList m_presentIds;
    bool m_resetting = false;
    mutable std::mutex m_mutex;          // guards m_device
    mydaq::DevicePtr m_device;           // selected and present
};

}  // namespace mybench
