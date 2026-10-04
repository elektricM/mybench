// Unit tests of the bridge's protocol helpers: command parsing, list replies, frame encoding,
// range and trigger-delay rules.

#include "acquisition.h"
#include "protocol.h"

#include <QTest>
#include <QtEndian>

#include <cstring>

using namespace scopehal_bridge;

class TestScopehalProtocol : public QObject {
    Q_OBJECT

private slots:
    void parseCommands();
    void channelNames();
    void lists();
    void frameLayout();
    void rules();
};

void TestScopehalProtocol::parseCommands()
{
    Command c = parseCommand(":C1:RANGE 5.000000");
    QCOMPARE(c.subject, QByteArray("C1"));
    QCOMPARE(c.name, QByteArray("RANGE"));
    QCOMPARE(c.args, QList<QByteArray>{"5.000000"});
    QVERIFY(!c.query);

    c = parseCommand("TRIG:EDGE:DIR rising\r");
    QCOMPARE(c.subject, QByteArray("TRIG"));
    QCOMPARE(c.name, QByteArray("EDGE:DIR"));
    QCOMPARE(c.args, QList<QByteArray>{"RISING"});

    c = parseCommand("*IDN?");
    QCOMPARE(c.subject, QByteArray());
    QCOMPARE(c.name, QByteArray("*IDN"));
    QVERIFY(c.query);
    QVERIFY(c.args.isEmpty());

    c = parseCommand("  START  ");
    QCOMPARE(c.name, QByteArray("START"));
    QVERIFY(!c.query);

    c = parseCommand(":C2:OFFS -0.000000");
    QCOMPARE(c.subject, QByteArray("C2"));
    QCOMPARE(c.args, QList<QByteArray>{"-0.000000"});

    c = parseCommand("FOO 1, 2  3");
    QCOMPARE(c.args, (QList<QByteArray>{"1", "2", "3"}));
}

void TestScopehalProtocol::channelNames()
{
    QCOMPARE(channelIndex("C1"), 0);
    QCOMPARE(channelIndex("C2"), 1);
    QCOMPARE(channelIndex("C3"), -1);
    QCOMPARE(channelIndex("C0"), -1);
    QCOMPARE(channelIndex("TRIG"), -1);
    QCOMPARE(channelIndex(""), -1);
}

void TestScopehalProtocol::lists()
{
    QVERIFY(ratesReply().startsWith("5000000000,10000000000,"));
    QVERIFY(ratesReply().endsWith(','));
    QVERIFY(depthsReply().startsWith("1000,2000,5000,"));
    QVERIFY(depthsReply().endsWith(','));
    QCOMPARE(sampleRates().front(), uint64_t(200000));
    QCOMPARE(sampleDepths().front(), uint64_t(1000));
    QCOMPARE(nearestRate(199999), uint64_t(200000));
    QCOMPARE(nearestRate(1), uint64_t(10));
    QCOMPARE(nearestRate(30000), uint64_t(20000));
    QCOMPARE(nearestDepth(5000000), uint64_t(1000000));
}

void TestScopehalProtocol::frameLayout()
{
    const double a[3] = {1.0, -2.5, 0.125};
    const double b[3] = {0.5, 0.25, -1.0};
    const std::vector<FrameChannel> channels = {{0, 1234.5f, a, 1.0}, {1, -2.0f, b, 10.0}};
    const QByteArray f = encodeFrame(5'000'000'000, 3, channels);
    QCOMPARE(f.size(), qsizetype(2 + 8 + 2 * (8 + 8 + 4 + 3 * 8)));
    const char* p = f.constData();
    QCOMPARE(qFromLittleEndian<quint16>(p), quint16(2));
    QCOMPARE(qFromLittleEndian<qint64>(p + 2), qint64(5'000'000'000));
    p += 10;
    for (int ch = 0; ch < 2; ++ch) {
        QCOMPARE(qFromLittleEndian<quint64>(p), quint64(ch));
        QCOMPARE(qFromLittleEndian<quint64>(p + 8), quint64(3));
        const quint32 phaseBits = qFromLittleEndian<quint32>(p + 16);
        float phase = 0;
        std::memcpy(&phase, &phaseBits, 4);
        QCOMPARE(phase, ch == 0 ? 1234.5f : -2.0f);
        p += 20;
        for (int i = 0; i < 3; ++i) {
            const quint64 bits = qFromLittleEndian<quint64>(p);
            double v = 0;
            std::memcpy(&v, &bits, 8);
            QCOMPARE(v, ch == 0 ? a[i] : 10.0 * b[i]);   // attenuation applied
            p += 8;
        }
    }
}

void TestScopehalProtocol::rules()
{
    ChannelSettings ch;
    ch.span = 4.0;
    QCOMPARE(hardwareRange(ch), 2.0);   // -2..2 V fits
    ch.center = 0.1;
    QCOMPARE(hardwareRange(ch), 10.0);  // -1.9..2.1 V does not
    ch.span = 5.0;
    ch.center = 0;
    QCOMPARE(hardwareRange(ch), 10.0);
    QCOMPARE(triggerHysteresis(ch), 0.05);
    ch.span = 0.01;
    QCOMPARE(triggerHysteresis(ch), 1e-3);

    // 1000 samples at 200 kS/s = 5 ms = 5e12 fs: at most one record after, ten before.
    QCOMPARE(clampTriggerDelay(1'000'000'000'000, 200000, 1000), int64_t(1'000'000'000'000));
    QCOMPARE(clampTriggerDelay(9'000'000'000'000, 200000, 1000), int64_t(5'000'000'000'000));
    QCOMPARE(clampTriggerDelay(-80'000'000'000'000, 200000, 1000), int64_t(-50'000'000'000'000));
    // 1 M samples at 10 S/s overflows int64 fs: saturates instead of wrapping.
    QVERIFY(clampTriggerDelay(INT64_MAX, 10, 1000000) > 0);
}

QTEST_GUILESS_MAIN(TestScopehalProtocol)
#include "tst_scopehal_protocol.moc"
