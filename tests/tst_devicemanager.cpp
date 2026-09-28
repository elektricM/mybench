#include "mydaq/device_manager.h"

#include "mydaq/sim_device.h"

#include <QSignalSpy>
#include <QTest>

#include <atomic>

using namespace mydaq;

namespace {

// A "real" (non-simulated) device that does nothing: enough to test discovery.
class FakeDevice final : public Device {
public:
    DeviceInfo info() const override
    {
        return {QStringLiteral("fake:1"), QStringLiteral("Fake myDAQ"), QStringLiteral("NI myDAQ"),
                QStringLiteral("1"), false};
    }
    bool isConnected() const override { return true; }
    double maxAiRate() const override { return 200e3; }
    double maxAoRate() const override { return 200e3; }
    double coerceAiRate(double requested) const override { return requested; }
    double coerceAoRate(double requested) const override { return requested; }
    std::unique_ptr<AiTask> createAiTask(const AiConfig&) override { throw unsupported(); }
    std::unique_ptr<AoTask> createAoTask(const AoConfig&) override { throw unsupported(); }
    double aiReadStatic(AiChannel, double) override { throw unsupported(); }
    void aoWriteStatic(AoChannel, double) override { throw unsupported(); }
    std::vector<DmmRange> dmmRanges(DmmFunction) const override { return {}; }
    DmmReading dmmRead(DmmFunction, double) override { throw unsupported(); }
    void dioSetDirection(uint8_t, bool) override {}
    uint8_t dioDirection() const override { return 0; }
    void dioWrite(uint8_t, uint8_t) override {}
    uint8_t dioRead() override { return 0; }
    void reset() override {}

private:
    static DeviceError unsupported() { return DeviceError(DeviceError::NotSupported, QStringLiteral("fake")); }
};

class FakeProvider final : public DeviceProvider {
public:
    explicit FakeProvider(std::shared_ptr<std::atomic<bool>> present) : m_present(std::move(present)) {}
    QString name() const override { return QStringLiteral("Fake"); }
    QList<DevicePtr> scan() override
    {
        if (!*m_present)
            return {};
        return {m_device};
    }

private:
    std::shared_ptr<std::atomic<bool>> m_present;
    DevicePtr m_device = std::make_shared<FakeDevice>();
};

std::shared_ptr<SimDevice> simulator()
{
    return std::dynamic_pointer_cast<SimDevice>(DeviceManager::instance().device(QStringLiteral("sim:0")));
}

}  // namespace

class TestDeviceManager : public QObject {
    Q_OBJECT

private slots:
    void simulatorIsListed()
    {
        DeviceManager& manager = DeviceManager::instance();
        QCOMPARE(&manager, &DeviceManager::instance());
        const std::shared_ptr<SimDevice> sim = simulator();
        QVERIFY(sim);
        QVERIFY(sim->info().simulated);
        QVERIFY(manager.devices().contains(sim));
        QVERIFY(manager.defaultDevice());
        QVERIFY(!manager.device(QStringLiteral("nope")));
    }

    void hotPlug()
    {
        DeviceManager& manager = DeviceManager::instance();
        QSignalSpy spy(&manager, &DeviceManager::devicesChanged);
        manager.rescan();
        QCOMPARE(spy.count(), 0);   // nothing changed

        auto present = std::make_shared<std::atomic<bool>>(true);
        manager.addProvider(std::make_unique<FakeProvider>(present));
        QCOMPARE(spy.count(), 1);
        QVERIFY(manager.device(QStringLiteral("fake:1")));
        QVERIFY(!manager.defaultDevice()->info().simulated);   // real devices win
        manager.rescan();
        QCOMPARE(spy.count(), 1);

        *present = false;
        manager.rescan();
        QCOMPARE(spy.count(), 2);
        QVERIFY(!manager.device(QStringLiteral("fake:1")));

        const std::shared_ptr<SimDevice> sim = simulator();
        sim->setConnected(false);
        manager.rescan();
        QCOMPARE(spy.count(), 3);
        QVERIFY(!manager.device(QStringLiteral("sim:0")));
        sim->setConnected(true);
        manager.rescan();
        QCOMPARE(spy.count(), 4);
        QCOMPARE(manager.device(QStringLiteral("sim:0")), DevicePtr(sim));   // same object comes back
    }

    // The manager rescans by itself every 2 s while an application object exists.
    void periodicRescan()
    {
        DeviceManager& manager = DeviceManager::instance();
        QSignalSpy spy(&manager, &DeviceManager::devicesChanged);
        const std::shared_ptr<SimDevice> sim = simulator();
        sim->setConnected(false);
        QVERIFY(spy.wait(3000));
        QVERIFY(!manager.device(QStringLiteral("sim:0")));
        sim->setConnected(true);
        manager.rescan();
        QVERIFY(manager.device(QStringLiteral("sim:0")));
    }
};

QTEST_GUILESS_MAIN(TestDeviceManager)
#include "tst_devicemanager.moc"
