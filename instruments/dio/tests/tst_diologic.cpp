// Digital I/O logic: presets, right-justified mapping, patterns, actions, value formats
// (docs/instruments/dio.md 2.2 and 3.2).
#include "diologic.h"

#include <QTest>

using namespace Qt::StringLiterals;
namespace dio = mybench::instruments::dio;

class TestDioLogic : public QObject
{
    Q_OBJECT

private slots:
    void presets()
    {
        QCOMPARE(dio::presetMask(dio::Lines0to3), uint8_t(0x0F));
        QCOMPARE(dio::presetMask(dio::Lines4to7), uint8_t(0xF0));
        QCOMPARE(dio::presetMask(dio::Lines0to7), uint8_t(0xFF));
        QCOMPARE(dio::presetOf(0xF0), dio::Lines4to7);
        QCOMPARE(dio::presetOf(0x00), dio::NoLines);
        QCOMPARE(dio::presetOf(0x52), dio::CustomLines);
        QCOMPARE(dio::linesLabel(0x0F), u"0 - 3"_s);
        QCOMPARE(dio::linesLabel(0x52), u"1, 4, 6"_s);
    }

    void rightJustified()
    {
        // 4 - 7: value bits 0..3 on lines 4..7 (ELVISmx subGetDIOdata / subSetDigWriterPatternValue).
        QCOMPARE(dio::scatter(0x5, 0xF0), uint8_t(0x50));
        QCOMPARE(dio::gather(0xA7, 0xF0), 0xA);
        QCOMPARE(dio::gather(0xA7, 0x0F), 0x7);
        QCOMPARE(dio::scatter(0x1F, 0x0F), uint8_t(0x0F));   // upper bits ignored
        // Custom set {1, 4, 6}: DIO 1 = bit 0, DIO 4 = bit 1, DIO 6 = bit 2; 5 -> DIO 1 and DIO 6.
        QCOMPARE(dio::scatter(5, 0x52), uint8_t(0x42));
        QCOMPARE(dio::gather(0x42, 0x52), 5);
        QCOMPARE(dio::bitOfLine(6, 0x52), 2);
        QCOMPARE(dio::bitOfLine(0, 0x52), -1);
    }

    void sequences()
    {
        QCOMPARE(dio::sequence(dio::Ramp, 4, dio::Left).size(), size_t(16));
        QCOMPARE(dio::sequence(dio::Ramp, 8, dio::Left).back(), 255);
        QCOMPARE(dio::sequence(dio::Alternating, 8, dio::Left), (std::vector<int>{0x55, 0xAA}));
        QCOMPARE(dio::sequence(dio::Alternating, 4, dio::Left), (std::vector<int>{0x5, 0xA}));
        QCOMPARE(dio::sequence(dio::WalkingOnes, 4, dio::Left), (std::vector<int>{1, 2, 4, 8}));
        QCOMPARE(dio::sequence(dio::WalkingOnes, 4, dio::Right), (std::vector<int>{8, 4, 2, 1}));
        QVERIFY(dio::sequence(dio::Manual, 4, dio::Left).empty());
        QVERIFY(dio::sequence(dio::Ramp, 0, dio::Left).empty());
    }

    // Spec 11: from 0x4 on 4 lines, Toggle -> B, Rotate Left -> 7, Shift Right -> 3.
    void actions()
    {
        int v = dio::applyAction(dio::Toggle, 0x4, 4, dio::Left);
        QCOMPARE(v, 0xB);
        v = dio::applyAction(dio::Rotate, v, 4, dio::Left);
        QCOMPARE(v, 0x7);
        v = dio::applyAction(dio::Shift, v, 4, dio::Right);
        QCOMPARE(v, 0x3);
        QCOMPARE(dio::applyAction(dio::Rotate, 0x1, 4, dio::Right), 0x8);
        QCOMPARE(dio::applyAction(dio::Shift, 0x8, 4, dio::Left), 0x0);
        QCOMPARE(dio::applyAction(dio::Rotate, 0x80, 8, dio::Left), 0x01);
        QCOMPARE(dio::applyAction(dio::Toggle, 0x55, 8, dio::Left), 0xAA);
    }

    void formats()
    {
        QCOMPARE(dio::formatValue(13, dio::Hex), u"D"_s);
        QCOMPARE(dio::formatValue(0, dio::Hex), u"0"_s);
        QCOMPARE(dio::formatValue(13, dio::Decimal), u"13"_s);
        QCOMPARE(dio::formatValue(13, dio::Octal), u"15"_s);
        QCOMPARE(dio::formatValue(13, dio::Binary, 4), u"1101"_s);
        QCOMPARE(dio::formatValue(5, dio::Binary, 8), u"00000101"_s);
        QCOMPARE(dio::formatValue(5, dio::Binary, 8, false), u"101"_s);
        QCOMPARE(dio::radixLetter(dio::Hex), u"x"_s);
    }
};

QTEST_APPLESS_MAIN(TestDioLogic)
#include "tst_diologic.moc"
