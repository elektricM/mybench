// End-to-end test of mybench-scopehal-bridge: starts the executable on free ports and talks to it
// the way scopehal's "digilent" driver does (control socket: one line per command, a reply only
// to queries; data socket: binary frames), parsing replies and frames independently of the
// bridge's code.
//
// Simulator (default): AI0 shows a 1 kHz, 1 V sine and AI1 a 200 Hz, +-0.5 V square, so the
// trigger position and the waveforms are checked. MYBENCH_BRIDGE_HW=1 runs against the real myDAQ
// (MYDAQ_SERIAL picks one): nothing is wired, so it checks the protocol, frame layout and value
// ranges with forced and auto-triggered frames.

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS   // std::sscanf: the test mirrors the driver's *IDN? parsing
#endif

#include <QDeadlineTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTest>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

constexpr double kFsPerSecond = 1e15;

bool hwMode() { return qEnvironmentVariableIntValue("MYBENCH_BRIDGE_HW") == 1; }

// Event-loop wait (QTcpSocket::waitFor* may fail spuriously on Windows).
bool waitUntil(const std::function<bool()>& done, int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
        if (deadline.hasExpired())
            return false;
        QTest::qWait(2);
    }
    return true;
}

class BridgeProcess {
public:
    ~BridgeProcess() { stop(); }

    bool start(const QStringList& extra, QString* error)
    {
        QStringList args = {u"--port"_s, u"0"_s, u"--data-port"_s, u"0"_s};
        if (!hwMode())
            args << u"--sim"_s;
        else if (!qEnvironmentVariable("MYDAQ_SERIAL").isEmpty())
            args << u"--serial"_s << qEnvironmentVariable("MYDAQ_SERIAL");
        args << extra;
        m_process.setProgram(QStringLiteral(MYBENCH_BRIDGE_EXECUTABLE));
        m_process.setArguments(args);
        m_process.setProcessChannelMode(QProcess::ForwardedErrorChannel);   // bridge log in the test log
        m_process.start();
        if (!m_process.waitForStarted(10000)) {
            *error = m_process.errorString();
            return false;
        }
        static const QRegularExpression re(uR"(listening on ([^\s:]+):(\d+):(\d+))"_s);
        QByteArray out;
        QRegularExpressionMatch m;
        waitUntil(
            [&] {
                out += m_process.readAllStandardOutput();
                m = re.match(QString::fromUtf8(out));
                return m.hasMatch() || m_process.state() == QProcess::NotRunning;
            },
            hwMode() ? 40000 : 15000);
        if (!m.hasMatch()) {
            *error = u"the bridge did not start listening; stdout: "_s + QString::fromUtf8(out);
            return false;
        }
        m_port = quint16(m.captured(2).toUInt());
        m_dataPort = quint16(m.captured(3).toUInt());
        return true;
    }

    // Returns the exit code, or -1 if it had to be killed.
    int stop()
    {
        if (m_process.state() == QProcess::NotRunning)
            return m_process.exitCode();
        m_process.terminate();   // SIGTERM: the bridge shuts down cleanly (no-op on Windows)
        if (m_process.waitForFinished(5000))
            return m_process.exitStatus() == QProcess::NormalExit ? m_process.exitCode() : -1;
        m_process.kill();
        m_process.waitForFinished(5000);
        return -1;
    }

    quint16 port() const { return m_port; }
    quint16 dataPort() const { return m_dataPort; }

private:
    QProcess m_process;
    quint16 m_port = 0;
    quint16 m_dataPort = 0;
};

struct Frame {
    struct Channel {
        quint64 index = 0;
        quint64 depth = 0;
        float phase = 0;   // fs
        std::vector<double> samples;
    };
    quint16 channelCount = 0;
    qint64 fsPerSample = 0;
    std::vector<Channel> channels;
};

class Client {
public:
    bool open(quint16 port, quint16 dataPort)
    {
        control.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
        if (!waitUntil([&] { return control.state() == QAbstractSocket::ConnectedState; }, 5000))
            return false;
        data.connectToHost(QHostAddress(QHostAddress::LocalHost), dataPort);
        return waitUntil([&] { return data.state() == QAbstractSocket::ConnectedState; }, 5000);
    }
    void close()
    {
        control.abort();
        data.abort();
    }

    void send(const QByteArray& line)
    {
        control.write(line + '\n');
        control.flush();
    }
    std::optional<QByteArray> readLine(int timeoutMs = 5000)
    {
        if (!waitUntil([&] { return control.canReadLine(); }, timeoutMs))
            return std::nullopt;
        QByteArray line = control.readLine();
        line.chop(1);
        return line;
    }
    std::optional<QByteArray> query(const QByteArray& q)
    {
        send(q);
        return readLine();
    }

    bool readData(char* out, qint64 n, int timeoutMs)
    {
        if (!waitUntil([&] { return data.bytesAvailable() >= n; }, timeoutMs))
            return false;
        return data.read(out, n) == n;
    }

    // One frame, byte by byte as the driver reads it.
    std::optional<Frame> readFrame(int timeoutMs)
    {
        Frame f;
        char header[20];
        if (!readData(header, 10, timeoutMs))
            return std::nullopt;
        f.channelCount = qFromLittleEndian<quint16>(header);
        f.fsPerSample = qFromLittleEndian<qint64>(header + 2);
        for (int i = 0; i < f.channelCount; ++i) {
            if (!readData(header, 20, 10000))
                return std::nullopt;
            Frame::Channel c;
            c.index = qFromLittleEndian<quint64>(header);
            c.depth = qFromLittleEndian<quint64>(header + 8);
            const quint32 phaseBits = qFromLittleEndian<quint32>(header + 16);
            std::memcpy(&c.phase, &phaseBits, sizeof c.phase);
            if (c.depth == 0 || c.depth > 10'000'000)
                return std::nullopt;
            QByteArray raw(qsizetype(c.depth * 8), Qt::Uninitialized);
            if (!readData(raw.data(), raw.size(), 20000))
                return std::nullopt;
            c.samples.resize(size_t(c.depth));
            for (size_t j = 0; j < c.samples.size(); ++j) {
                const quint64 bits = qFromLittleEndian<quint64>(raw.constData() + 8 * j);
                std::memcpy(&c.samples[j], &bits, sizeof bits);
            }
            f.channels.push_back(std::move(c));
        }
        return f;
    }

    bool dataQuiet(int ms)
    {
        return !waitUntil([&] { return data.bytesAvailable() > 0; }, ms);
    }
    // Discards frames still on their way (after STOP) until the data socket stays quiet.
    void drain(int quietMs)
    {
        while (!dataQuiet(quietMs))
            data.readAll();
    }

    QTcpSocket control;
    QTcpSocket data;
};

// Rising crossings of `level` (armed below level - hysteresis), linear interpolation, as
// fractional sample indices.
std::vector<double> risingCrossings(const std::vector<double>& x, double level, double hysteresis)
{
    std::vector<double> out;
    bool armed = false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (!armed) {
            armed = x[i] < level - hysteresis;
            continue;
        }
        if (x[i] >= level) {
            out.push_back(double(i - 1) + (level - x[i - 1]) / (x[i] - x[i - 1]));
            armed = false;
        }
    }
    return out;
}

// Same parsing as the driver's GetSampleRatesNonInterleaved / GetSampleDepthsNonInterleaved:
// only blocks followed by a comma count.
std::vector<qint64> parseList(const QByteArray& reply)
{
    std::vector<qint64> out;
    const std::string s = reply.toStdString();
    size_t i = 0;
    while (true) {
        const size_t start = i;
        i = s.find(',', i + 1);
        if (i == std::string::npos)
            break;
        out.push_back(std::stoll(s.substr(start, i - start)));
        ++i;
    }
    return out;
}

double minOf(const std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); }
double maxOf(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); }

}  // namespace

class TestScopehalBridge : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void identify();
    void initialSettingsAndForce();
    void noReplyToSettings();
    void singleTriggeredFrame();
    void normalModeAndStop();
    void twoChannelsAndAttenuation();
    void autoTrigger();
    void clientAutoMode();
    void oneClientAndReconnect();

private:
    void configureC1(qint64 rate, qint64 depth, double level, qint64 delayFs);

    BridgeProcess m_bridge;
    Client m_client;
};

void TestScopehalBridge::initTestCase()
{
    QString error;
    // The real myDAQ has nothing wired: normal mode needs the auto trigger there.
    const QStringList extra = hwMode() ? QStringList{u"--auto-trigger-ms"_s, u"200"_s} : QStringList{};
    QVERIFY2(m_bridge.start(extra, &error), qPrintable(error));
    QVERIFY(m_client.open(m_bridge.port(), m_bridge.dataPort()));
}

void TestScopehalBridge::cleanupTestCase()
{
    m_client.close();
    const int rc = m_bridge.stop();
#ifndef Q_OS_WIN
    QCOMPARE(rc, 0);   // clean shutdown on SIGTERM
#else
    Q_UNUSED(rc);
#endif
}

void TestScopehalBridge::identify()
{
    const auto idn = m_client.query("*IDN?");
    QVERIFY(idn);
    // SCPIDevice: "%127[^,],%127[^,],%127[^,],%127s" must yield 4 fields.
    char vendor[128] = "", model[128] = "", serial[128] = "", version[128] = "";
    QCOMPARE(std::sscanf(idn->constData(), "%127[^,],%127[^,],%127[^,],%127s", vendor, model, serial, version), 4);
    QCOMPARE(QByteArray(vendor), "National Instruments");
    QVERIFY(QByteArray(model).startsWith("NI myDAQ"));
    QVERIFY(!idn->contains(';'));
    QCOMPARE(idn->split(',').value(3), QByteArray(version));   // no blank in the version
    if (hwMode())
        QVERIFY2(QByteArray(serial) != "SIM0" && QByteArray(serial) != "UNKNOWN", serial);

    QCOMPARE(m_client.query("CHANS?").value_or("?"), QByteArray("2"));

    const QByteArray rates = m_client.query("RATES?").value_or({});
    QVERIFY(rates.endsWith(','));
    const std::vector<qint64> periods = parseList(rates);
    QVERIFY(periods.size() >= 10);
    QCOMPARE(periods.front(), qint64(5'000'000'000));   // 200 kS/s first: the initial rate
    for (qint64 fs : periods) {
        QVERIFY(fs > 0);
        QCOMPARE(qint64(kFsPerSecond) % fs, qint64(0));           // the client's 1e15 / fs is exact
        const qint64 hz = qint64(kFsPerSecond) / fs;
        QCOMPARE(qint64(100'000'000) % hz, qint64(0));            // the 100 MHz clock divides to it
        QVERIFY(hz <= 200000);
    }

    const QByteArray depths = m_client.query("DEPTHS?").value_or({});
    QVERIFY(depths.endsWith(','));
    const std::vector<qint64> d = parseList(depths);
    QVERIFY(d.size() >= 5);
    QCOMPARE(d.front(), qint64(1000));
    QVERIFY(std::is_sorted(d.begin(), d.end()));
    QVERIFY(d.back() < (qint64(1) << 31));   // the driver parses with stol (32 bits on Windows)

    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));
}

void TestScopehalBridge::initialSettingsAndForce()
{
    // Fresh session: 200 kS/s, 1000 samples. 15 V is beyond the input range (samples clip at
    // about +-10.5 V): no trigger can happen, whatever a floating input does.
    m_client.send(":C1:ON");
    m_client.send("TRIG:SOU C1");
    m_client.send("TRIG:LEV 15.000000");
    m_client.send("SINGLE");
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("1"));
    QVERIFY(m_client.dataQuiet(500));

    m_client.send("FORCE");
    const auto frame = m_client.readFrame(5000);
    QVERIFY(frame);
    QCOMPARE(frame->channelCount, quint16(1));
    QCOMPARE(frame->fsPerSample, qint64(5'000'000'000));
    QCOMPARE(frame->channels.size(), size_t(1));
    const Frame::Channel& c = frame->channels[0];
    QCOMPARE(c.index, quint64(0));
    QCOMPARE(c.depth, quint64(1000));
    QCOMPARE(c.phase, 0.0f);
    QVERIFY(minOf(c.samples) >= -10.6 && maxOf(c.samples) <= 10.6);
    if (!hwMode())
        QVERIFY(maxOf(c.samples) > 0.9 && minOf(c.samples) < -0.9);   // the sine

    QVERIFY(m_client.dataQuiet(400));   // one frame only
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));

    if (hwMode()) {
        // A window inside +-2 V selects the +-2 V range.
        m_client.send(":C1:RANGE 2.000000");
        m_client.send(":C1:OFFS 0.000000");
        m_client.send("FORCE");
        const auto f2 = m_client.readFrame(5000);
        QVERIFY(f2);
        QVERIFY(minOf(f2->channels[0].samples) >= -2.2 && maxOf(f2->channels[0].samples) <= 2.2);
        QVERIFY(m_client.dataQuiet(400));
        m_client.send(":C1:RANGE 5.000000");
    }
}

void TestScopehalBridge::noReplyToSettings()
{
    // The driver's constructor sequence, then commands the bridge does not know: none of them
    // may produce a reply, or every later query would read the wrong line.
    const QList<QByteArray> sets = {
        ":C1:COUP DC1M",   ":C1:OFFS -0.000000", ":C1:RANGE 5.000000", ":C1:ATTEN 1.000000",
        ":C2:COUP DC1M",   ":C2:OFFS -0.000000", ":C2:RANGE 5.000000", "RATE 200000",
        "DEPTH 1000",      "TRIG:DELAY 0",       "TRIG:SOU C1",        "TRIG:LEV 0.000000",
        "TRIG:EDGE:DIR RISING", "TRIG:MODE EDGE", ":C1:COUP AC1M", "FOO 1", ":C1:BWLIM 20", ":C9:ON",
        "TRIG:SOU C7",     "RATE abc",
    };
    for (const QByteArray& s : sets)
        m_client.send(s);
    QCOMPARE(m_client.query("CHANS?").value_or("?"), QByteArray("2"));
    // An unknown query still gets exactly one (empty) line.
    QCOMPARE(m_client.query("BOGUS?").value_or("?"), QByteArray(""));
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));
    QVERIFY(m_client.control.bytesAvailable() == 0);
}

void TestScopehalBridge::configureC1(qint64 rate, qint64 depth, double level, qint64 delayFs)
{
    m_client.send("RATE " + QByteArray::number(rate));
    m_client.send("DEPTH " + QByteArray::number(depth));
    m_client.send(":C1:ON");
    m_client.send(":C2:OFF");
    m_client.send(":C1:RANGE 5.000000");
    m_client.send(":C1:OFFS -0.000000");
    m_client.send(":C1:ATTEN 1.000000");
    m_client.send("TRIG:SOU C1");
    m_client.send("TRIG:LEV " + QByteArray::number(level, 'f', 6));
    m_client.send("TRIG:EDGE:DIR RISING");
    m_client.send("TRIG:DELAY " + QByteArray::number(delayFs));
}

void TestScopehalBridge::singleTriggeredFrame()
{
    if (hwMode())
        QSKIP("needs the simulator's test signals");
    const qint64 rate = 100000, depth = 2000;
    const qint64 period = qint64(kFsPerSecond) / rate;   // 1e10 fs
    const qint64 delay = 500 * period + period / 3;      // not on a sample: the phase carries it
    const double level = 0.25;
    configureC1(rate, depth, level, delay);
    m_client.send("SINGLE");

    const auto frame = m_client.readFrame(5000);
    QVERIFY(frame);
    QCOMPARE(frame->channelCount, quint16(1));
    QCOMPARE(frame->fsPerSample, period);
    const Frame::Channel& c = frame->channels[0];
    QCOMPARE(c.index, quint64(0));
    QCOMPARE(c.depth, quint64(depth));
    QCOMPARE(c.samples.size(), size_t(depth));
    QVERIFY2(c.phase >= 0 && c.phase < float(period), qPrintable(QString::number(c.phase)));
    QVERIFY(m_client.dataQuiet(400));   // single: exactly one frame
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));

    // The 1 kHz, 1 V sine: amplitude and period (100 samples at 100 kS/s).
    QVERIFY(maxOf(c.samples) > 0.95 && maxOf(c.samples) < 1.06);
    QVERIFY(minOf(c.samples) < -0.95 && minOf(c.samples) > -1.06);
    const std::vector<double> zeros = risingCrossings(c.samples, 0.0, 0.05);
    QVERIFY(zeros.size() >= 18);
    const double samplesPerCycle = (zeros.back() - zeros.front()) / double(zeros.size() - 1);
    QVERIFY2(std::abs(samplesPerCycle - 100.0) < 0.5, qPrintable(QString::number(samplesPerCycle)));

    // The trigger: a rising crossing of the level shown exactly at the requested delay.
    const std::vector<double> crossings = risingCrossings(c.samples, level, 0.05);
    double best = 1e30;
    for (double x : crossings)
        best = std::min(best, std::abs(x * double(period) + double(c.phase) - double(delay)));
    QVERIFY2(best < 0.01 * double(period), qPrintable(QString::number(best / double(period))));
    const size_t k = size_t(double(delay) / double(period));
    QVERIFY(c.samples[k - 1] < level && c.samples[k + 1] >= level);
}

void TestScopehalBridge::normalModeAndStop()
{
    const qint64 rate = 200000, depth = 1000;
    const qint64 period = qint64(kFsPerSecond) / rate;
    const qint64 delay = 100 * period;
    const double level = hwMode() ? 15.0 : 0.0;   // hardware: no signal, frames from the auto trigger
    configureC1(rate, depth, level, delay);
    m_client.send("START");
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("1"));
    for (int i = 0; i < 5; ++i) {
        const auto frame = m_client.readFrame(5000);
        QVERIFY2(frame, qPrintable(u"frame %1"_s.arg(i)));
        QCOMPARE(frame->channelCount, quint16(1));
        QCOMPARE(frame->fsPerSample, period);
        const Frame::Channel& c = frame->channels[0];
        QCOMPARE(c.depth, quint64(depth));
        if (hwMode()) {
            QCOMPARE(c.phase, 0.0f);
            continue;
        }
        double best = 1e30;
        for (double x : risingCrossings(c.samples, level, 0.05))
            best = std::min(best, std::abs(x * double(period) + double(c.phase) - double(delay)));
        QVERIFY2(best < 0.01 * double(period), qPrintable(QString::number(best / double(period))));
    }
    m_client.send("STOP");
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));
    m_client.drain(500);
    QVERIFY(m_client.dataQuiet(700));
}

void TestScopehalBridge::twoChannelsAndAttenuation()
{
    const qint64 rate = 100000, depth = 5000;
    const qint64 period = qint64(kFsPerSecond) / rate;
    const qint64 delay = 1000 * period;
    configureC1(rate, depth, 0.0, delay);
    m_client.send(":C2:ON");
    m_client.send(":C2:RANGE 0.500000");   // +-0.25 V window at the input (5 V shown with x10)
    m_client.send(":C2:OFFS -0.000000");
    m_client.send(":C2:ATTEN 10.000000");
    m_client.send("TRIG:SOU C2");
    m_client.send(hwMode() ? "TRIG:LEV 15.000000" : "TRIG:LEV 0.000000");
    m_client.send(hwMode() ? "FORCE" : "SINGLE");

    const auto frame = m_client.readFrame(5000);
    QVERIFY(frame);
    QCOMPARE(frame->channelCount, quint16(2));
    QCOMPARE(frame->channels.size(), size_t(2));
    QCOMPARE(frame->channels[0].index, quint64(0));
    QCOMPARE(frame->channels[1].index, quint64(1));
    QCOMPARE(frame->channels[0].depth, quint64(depth));
    QCOMPARE(frame->channels[1].depth, quint64(depth));
    // AI1 is converted after AI0 within a scan: its samples are later by the skew.
    const double skew = double(frame->channels[1].phase) - double(frame->channels[0].phase);
    QVERIFY2(skew > 0 && skew < double(period), qPrintable(QString::number(skew)));
    QVERIFY(m_client.dataQuiet(400));
    m_client.send(":C2:OFF");
    m_client.send(":C2:ATTEN 1.000000");
    if (hwMode())
        return;

    // AI1: 200 Hz square, +-0.5 V, shown x10 at the probe tip.
    const Frame::Channel& c2 = frame->channels[1];
    QVERIFY(maxOf(c2.samples) > 4.8 && maxOf(c2.samples) < 5.2);
    QVERIFY(minOf(c2.samples) < -4.8 && minOf(c2.samples) > -5.2);
    // Its rising edge (level 0 at the input = 0 at the tip) is at the delay, skew included.
    double best = 1e30;
    for (double x : risingCrossings(c2.samples, 0.0, 0.5))
        best = std::min(best, std::abs(x * double(period) + double(c2.phase) - double(delay)));
    QVERIFY2(best < 0.01 * double(period), qPrintable(QString::number(best / double(period))));
    // AI0 is still the 1 V sine (not scaled).
    QVERIFY(maxOf(frame->channels[0].samples) < 1.1);
}

void TestScopehalBridge::autoTrigger()
{
    // Without --auto-trigger-ms a missing trigger means no frames (checked in
    // initialSettingsAndForce); with it, START keeps sending, SINGLE still waits.
    BridgeProcess autoBridge;
    Client autoClient;
    Client* client = &m_client;   // hardware: the main bridge runs with --auto-trigger-ms
    if (!hwMode()) {
        QString error;
        QVERIFY2(autoBridge.start({u"--auto-trigger-ms"_s, u"100"_s}, &error), qPrintable(error));
        QVERIFY(autoClient.open(autoBridge.port(), autoBridge.dataPort()));
        client = &autoClient;
    }
    client->send(":C1:ON");
    client->send("TRIG:LEV 15.000000");
    client->send("START");
    for (int i = 0; i < 3; ++i) {
        const auto frame = client->readFrame(5000);
        QVERIFY2(frame, qPrintable(u"auto frame %1"_s.arg(i)));
        QCOMPARE(frame->channels.at(0).phase, 0.0f);
    }
    client->send("STOP");
    client->drain(500);
    client->send("SINGLE");
    QVERIFY(client->dataQuiet(800));
    client->send("STOP");
    client->drain(300);
    autoClient.close();
}

void TestScopehalBridge::clientAutoMode()
{
    // ngscopeclient's own Auto button: START, FORCE when no frame came within 250 ms, START
    // again after the frame. The forced frame ends the run (the client treats FORCE as one-shot).
    if (hwMode())
        QSKIP("the hardware run uses --auto-trigger-ms, which sends frames on its own");
    configureC1(200000, 1000, 15.0, 0);   // beyond the input range: no trigger possible
    for (int i = 0; i < 2; ++i) {
        m_client.send("START");
        QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("1"));
        QVERIFY(m_client.dataQuiet(300));
        m_client.send("FORCE");
        const auto frame = m_client.readFrame(5000);
        QVERIFY(frame);
        QCOMPARE(frame->channels.at(0).phase, 0.0f);
        QCOMPARE(frame->channels.at(0).depth, quint64(1000));
        QVERIFY(m_client.dataQuiet(300));
        QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));
    }
}

void TestScopehalBridge::oneClientAndReconnect()
{
    // A second client is turned away while the first one is connected.
    QTcpSocket intruder;
    intruder.connectToHost(QHostAddress(QHostAddress::LocalHost), m_bridge.port());
    QVERIFY(waitUntil([&] { return intruder.state() == QAbstractSocket::UnconnectedState; }, 5000));
    QCOMPARE(m_client.query("CHANS?").value_or("?"), QByteArray("2"));

    // Leaving while armed stops the acquisition; a new connection starts a fresh session.
    m_client.send(":C1:ON");
    m_client.send("TRIG:LEV 0.000000");
    m_client.send("START");
    QVERIFY(m_client.readFrame(5000));
    m_client.close();
    QTest::qWait(200);

    QVERIFY(m_client.open(m_bridge.port(), m_bridge.dataPort()));
    const auto idn = m_client.query("*IDN?");
    QVERIFY(idn && idn->startsWith("National Instruments,"));
    QCOMPARE(m_client.query("ARMED?").value_or("?"), QByteArray("0"));
    QVERIFY(m_client.dataQuiet(300));   // nothing left over from the previous session
    m_client.send(":C1:ON");
    m_client.send("FORCE");
    const auto frame = m_client.readFrame(5000);
    QVERIFY(frame);
    QCOMPARE(frame->fsPerSample, qint64(5'000'000'000));   // defaults again
    QCOMPARE(frame->channels.at(0).depth, quint64(1000));
}

QTEST_GUILESS_MAIN(TestScopehalBridge)
#include "tst_scopehal_bridge.moc"
