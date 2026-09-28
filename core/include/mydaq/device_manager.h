// Device discovery: one list of devices from all backends (simulator, native USB, ...).
#pragma once

#include "mydaq/device.h"

#include <QObject>
#include <QList>

#include <memory>

namespace mydaq {

// A backend that can find devices. scan() is called on rescans and must be cheap for devices that
// are already open (return the same DevicePtr for a device that is still present).
class DeviceProvider {
public:
    virtual ~DeviceProvider() = default;
    virtual QString name() const = 0;
    virtual QList<DevicePtr> scan() = 0;
};

class DeviceManager : public QObject {
    Q_OBJECT
public:
    static DeviceManager& instance();   // registers the built-in providers on first use

    void addProvider(std::unique_ptr<DeviceProvider> provider);

    QList<DevicePtr> devices() const;
    DevicePtr device(const QString& id) const;   // nullptr if not present
    DevicePtr defaultDevice() const;             // first real device, else the simulated one

public slots:
    void rescan();   // also runs periodically (hot-plug)

signals:
    void devicesChanged();

private:
    DeviceManager();
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace mydaq
