#include "hwfixture.h"

#include <mydaq/device_manager.h>

#include <QCoreApplication>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hw {

namespace {
mydaq::DevicePtr g_device;
}

bool simulated() { return g_device && g_device->info().simulated; }

bool jumper(const char* name)
{
    return qEnvironmentVariable("MYDAQ_JUMPERS").contains(QLatin1String(name));
}

void initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    mybench::InstrumentController::setRestoreSavedSettings(false);
}

bool waitFor(const std::function<bool()>& pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() >= timeoutMs)
            return pred();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return true;
}

qint64 stopAndWait(mybench::InstrumentController& c, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    c.stop();
    if (!waitFor([&] { return c.isIdle(); }, timeoutMs))
        return -1;
    return t.elapsed();
}

void report(const QString& name, const QString& text)
{
    std::printf("HW %s: %s\n", qPrintable(name), qPrintable(text));
    std::fflush(stdout);
}

double readAi0(int n)
{
    if (!g_device)
        return std::numeric_limits<double>::quiet_NaN();
    double sum = 0;
    try {
        for (int i = 0; i < n; ++i)
            sum += g_device->aiReadStatic(mydaq::AiChannel::AI0, 10.0);
    } catch (const mydaq::DeviceError&) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / n;
}

LatencyProbe::LatencyProbe()
{
    m_clock.start();
    startTimer(5, Qt::PreciseTimer);
}

void LatencyProbe::reset()
{
    m_last = -1;
    m_max = 0;
    m_ticks = 0;
}

void LatencyProbe::timerEvent(QTimerEvent*)
{
    const qint64 now = m_clock.nsecsElapsed();
    if (m_last >= 0)
        m_max = std::max(m_max, (now - m_last) / 1e6 - 5.0);
    m_last = now;
    ++m_ticks;
}

mydaq::DevicePtr selectDevice()
{
    if (g_device)
        return g_device;
    auto& manager = mydaq::DeviceManager::instance();
    const QString wanted = qEnvironmentVariable("MYBENCH_HW_DEVICE");
    QElapsedTimer t;
    t.start();
    mydaq::DevicePtr device;
    while (!device && t.elapsed() < 15000) {
        manager.rescan();
        for (const mydaq::DevicePtr& d : manager.devices())
            if (wanted.isEmpty() ? !d->info().simulated : d->info().id == wanted)
                device = d;
        if (!device)
            waitFor([] { return false; }, 100);
    }
    if (!device) {
        report(QStringLiteral("device"), QStringLiteral("none found after %1 s").arg(t.elapsed() / 1000.0));
        return nullptr;
    }
    auto* hub = mybench::DeviceHub::instance();
    hub->select(device->info().id);
    if (!waitFor([hub] { return hub->isConnected(); }, 5000)) {
        report(QStringLiteral("device"), QStringLiteral("%1 not connected in DeviceHub").arg(device->info().id));
        return nullptr;
    }
    g_device = device;
    QElapsedTimer ready;
    ready.start();
    double v = std::numeric_limits<double>::quiet_NaN();
    while (std::isnan(v = readAi0(1)) && ready.elapsed() < 15000)
        waitFor([] { return false; }, 250);
    report(QStringLiteral("device"), QStringLiteral("%1 (%2) listed after %3 s, AI ready after %4 s, jumpers: %5")
                                         .arg(device->info().name, device->info().id)
                                         .arg(t.elapsed() / 1000.0 - ready.elapsed() / 1000.0, 0, 'f', 2)
                                         .arg(ready.elapsed() / 1000.0, 0, 'f', 2)
                                         .arg(qEnvironmentVariable("MYDAQ_JUMPERS", QStringLiteral("none"))));
    if (std::isnan(v))
        return nullptr;
    {   // the machine's own scheduling noise, for comparison with the stalls measured while running
        LatencyProbe probe;
        waitFor([] { return false; }, 2000);
        report(QStringLiteral("baseline"), QStringLiteral("UI stall max %1 ms with nothing running").arg(probe.maxStallMs(), 0, 'f', 1));
    }
    return device;
}

}  // namespace hw
