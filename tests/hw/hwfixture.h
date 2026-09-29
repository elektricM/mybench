// Shared helpers of the app-level hardware tests (tests/hw): they drive the real instrument
// controllers headless on the real myDAQ through the USB backend.
//
// Device: MYBENCH_HW_DEVICE (e.g. "usb:036CD1C6"; "sim:0" to develop a test against the
// simulator), else the first USB myDAQ found. Jumpers: MYDAQ_JUMPERS=ao0-ai0,dio4-dio1 (AO 0 wired
// to AI 0+ with AI 0- on AGND; DIO 4 wired to DIO 1); tests that need one skip without it, and no
// test drives a jumpered input line as an output.
#pragma once

#include <mybench/devicehub.h>
#include <mybench/instrumentcontroller.h>
#include <mydaq/device.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTest>

#include <cstdio>
#include <functional>

namespace hw {

// Selects the device under test in DeviceHub and waits (up to 15 s) until it is connected and
// answers an on-demand AI read (right after the lock script took the device back from the NI VM
// it refuses the AI engine for a few seconds). Prints what it found. nullptr if none.
mydaq::DevicePtr selectDevice();

bool simulated();
bool jumper(const char* name);   // MYDAQ_JUMPERS contains `name`

// Common per-test setup: no saved settings, test-mode paths.
void initTestCase();

// Runs the event loop until pred() holds or timeoutMs elapses; returns pred().
bool waitFor(const std::function<bool()>& pred, int timeoutMs);

// Stops a controller and waits until it is idle; returns the milliseconds it took (-1: timeout).
qint64 stopAndWait(mybench::InstrumentController& c, int timeoutMs = 5000);

// One result line in the test log: "HW <name>: <text>" (collected into the report).
void report(const QString& name, const QString& text);

// AI0 on demand (±10 V), averaged; NaN on error.
double readAi0(int n = 5);

// UI-thread responsiveness: a 5 ms timer on the thread that runs the controllers' published
// updates; the largest gap between its ticks (beyond the 5 ms period) is how long the UI thread
// was blocked. Only meaningful while the event loop runs (waitFor / QTest::qWait).
class LatencyProbe : public QObject
{
public:
    LatencyProbe();
    void reset();
    double maxStallMs() const { return m_max; }
    int ticks() const { return m_ticks; }

protected:
    void timerEvent(QTimerEvent*) override;

private:
    QElapsedTimer m_clock;
    qint64 m_last = -1;
    double m_max = 0;
    int m_ticks = 0;
};

}  // namespace hw

#define HW_REQUIRE_JUMPER(name)                                                                    \
    do {                                                                                           \
        if (!hw::jumper(name))                                                                     \
            QSKIP("needs the jumper " name " (MYDAQ_JUMPERS=" name ")");                         \
    } while (0)

#define HW_REQUIRE_REAL_DEVICE()                                                                   \
    do {                                                                                           \
        if (hw::simulated())                                                                       \
            QSKIP("a physical fact of the real myDAQ (not modelled by the simulator)");            \
    } while (0)

// The controller has no error; prints it otherwise.
#define HW_VERIFY_NO_ERROR(c) QVERIFY2(!(c).hasError(), qPrintable((c).errorText()))
