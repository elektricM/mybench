#include "sim_provider.h"

#include "mydaq/sim_device.h"

namespace mydaq {

namespace {

class SimProvider final : public DeviceProvider {
public:
    QString name() const override { return QStringLiteral("Simulator"); }
    QList<DevicePtr> scan() override
    {
        if (!m_device->isConnected())
            return {};
        return {m_device};
    }

private:
    const std::shared_ptr<SimDevice> m_device = std::make_shared<SimDevice>();
};

}  // namespace

std::unique_ptr<DeviceProvider> makeSimProvider() { return std::make_unique<SimProvider>(); }

}  // namespace mydaq
