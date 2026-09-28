// End-to-end hardware check of the native backend through DeviceManager (ctest label "hw",
// run through tools/devlock.sh): the provider must list "myDAQ (<serial>)" within a few seconds,
// and the Device API must work on it. Leaves AO at 0 V and DIO as inputs.
#include "mydaq/device_manager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cmath>
#include <cstdio>

using namespace mydaq;

namespace {

int failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qputenv("MYBENCH_NO_SIM", "1");
    DeviceManager& manager = DeviceManager::instance();

    QElapsedTimer timer;
    timer.start();
    DevicePtr device;
    while (!device && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        manager.rescan();
        for (const DevicePtr& d : manager.devices())
            if (!d->info().simulated)
                device = d;
        if (!device)
            QThread::msleep(100);
    }
    if (!device) {
        std::printf("no myDAQ listed after %.1f s\n", timer.elapsed() / 1000.0);
        return 1;
    }
    const DeviceInfo info = device->info();
    std::printf("listed after %.1f s: \"%s\" (id %s, serial %s)\n", timer.elapsed() / 1000.0,
                qPrintable(info.name), qPrintable(info.id), qPrintable(info.serialNumber));
    expect(info.name.startsWith(QStringLiteral("myDAQ (")) && !info.simulated, "display name");

    try {
        const double v = device->aiReadStatic(AiChannel::AI0, 10.0);
        std::printf("aiReadStatic AI0: %.6f V\n", v);
        expect(std::isfinite(v) && std::abs(v) < 10.6, "static AI");

        AiConfig config;
        config.channels = {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 2.0}};
        config.sampleRate = 50000.0;
        config.samplesPerChannel = 5000;
        config.mode = SampleMode::Finite;
        {
            auto task = device->createAiTask(config);
            for (int k = 0; k < 3; ++k) {
                task->start();
                const auto data = task->read(config.samplesPerChannel, 2000);
                expect(data.size() == 2 && data[1].size() == 5000, "finite block");
            }
            std::printf("finite AI 2 ch x 5000 at %.0f S/s, skew %.1f us: 3 blocks OK\n", task->sampleRate(),
                        task->channelSkew() * 1e6);
        }
        config.mode = SampleMode::Continuous;
        config.samplesPerChannel = 10000;
        {
            auto task = device->createAiTask(config);
            task->start();
            int64_t total = 0;
            for (int k = 0; k < 10; ++k)
                total += int64_t(task->read(10000, 2000)[0].size());
            task->stop();
            std::printf("continuous AI 2 ch at 50 kS/s: %lld samples/channel without loss\n", (long long)total);
            expect(total == 100000, "continuous");
        }
        device->aoWriteStatic(AoChannel::AO0, 0.25);
        device->aoWriteStatic(AoChannel::AO0, 0.0);
        device->dioSetDirection(0xff, false);
        std::printf("DIO levels 0x%02x\n", device->dioRead());
        const DmmReading r = device->dmmRead(DmmFunction::DcVoltage, 20.0);
        std::printf("DMM DC V 20 V: %.9g V%s\n", r.value, r.overload ? " (overload)" : "");
        try {
            device->dmmRead(DmmFunction::Resistance, 2e3);
            expect(false, "resistance should not be supported yet");
        } catch (const DeviceError& e) {
            expect(e.code() == DeviceError::NotSupported, "NotSupported for undecoded DMM functions");
        }
    } catch (const DeviceError& e) {
        std::printf("DeviceError %d: %s\n", int(e.code()), e.what());
        ++failures;
    }
    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
