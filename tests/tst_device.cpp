#include "mydaq/device.h"

#include <QTest>

using namespace mydaq;

class TestDevice : public QObject {
    Q_OBJECT

private slots:
    void channelNames()
    {
        QCOMPARE(toString(AiChannel::AI0), QStringLiteral("AI 0"));
        QCOMPARE(toString(AiChannel::AI1), QStringLiteral("AI 1"));
        QCOMPARE(toString(AiChannel::AudioInLeft), QStringLiteral("AudioInput Left"));
        QCOMPARE(toString(AiChannel::AudioInRight), QStringLiteral("AudioInput Right"));
        QCOMPARE(toString(AoChannel::AO0), QStringLiteral("AO 0"));
        QCOMPARE(toString(AoChannel::AO1), QStringLiteral("AO 1"));
        QCOMPARE(toString(AoChannel::AudioOutLeft), QStringLiteral("AudioOutput Left"));
        QCOMPARE(toString(AoChannel::AudioOutRight), QStringLiteral("AudioOutput Right"));
        QVERIFY(isAudio(AiChannel::AudioInLeft) && !isAudio(AiChannel::AI1));
        QVERIFY(isAudio(AoChannel::AudioOutRight) && !isAudio(AoChannel::AO0));
    }

    void dmmNames()
    {
        QCOMPARE(toString(DmmFunction::DcVoltage), QStringLiteral("DC Voltage"));
        QCOMPARE(toString(DmmFunction::AcVoltage), QStringLiteral("AC Voltage"));
        QCOMPARE(toString(DmmFunction::DcCurrent), QStringLiteral("DC Current"));
        QCOMPARE(toString(DmmFunction::AcCurrent), QStringLiteral("AC Current"));
        QCOMPARE(toString(DmmFunction::Resistance), QStringLiteral("Resistance"));
        QCOMPARE(toString(DmmFunction::Diode), QStringLiteral("Diode"));
        QCOMPARE(toString(DmmFunction::Continuity), QStringLiteral("Continuity"));
        QCOMPARE(unitOf(DmmFunction::AcVoltage), QStringLiteral("V"));
        QCOMPARE(unitOf(DmmFunction::Diode), QStringLiteral("V"));
        QCOMPARE(unitOf(DmmFunction::DcCurrent), QStringLiteral("A"));
        QCOMPARE(unitOf(DmmFunction::Resistance), QStringLiteral("Ω"));
        QCOMPARE(unitOf(DmmFunction::Continuity), QStringLiteral("Ω"));
    }

    void errorCarriesCode()
    {
        const DeviceError e(DeviceError::Timeout, QStringLiteral("late µs"));
        QCOMPARE(e.code(), DeviceError::Timeout);
        QCOMPARE(e.message(), QStringLiteral("late µs"));
    }
};

QTEST_GUILESS_MAIN(TestDevice)
#include "tst_device.moc"
