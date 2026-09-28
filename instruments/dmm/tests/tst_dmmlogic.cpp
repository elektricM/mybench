// Multimeter logic: ELVISmx range tables, autoranging, display formats, overload limits,
// statistics and the log format (docs/instruments/algorithms/dmm.md).
#include "dmmlogic.h"

#include <QTest>

using namespace Qt::StringLiterals;
using mydaq::DmmFunction;
namespace dmm = mybench::instruments::dmm;

class TestDmmLogic : public QObject
{
    Q_OBJECT

private slots:
    void rangeLabels()
    {
        QCOMPARE(dmm::rangeLabel(DmmFunction::DcVoltage, 60), u"60V"_s);
        QCOMPARE(dmm::rangeLabel(DmmFunction::DcVoltage, 0.2), u"200mV"_s);
        QCOMPARE(dmm::rangeLabel(DmmFunction::DcCurrent, 1), u"1A"_s);
        QCOMPARE(dmm::rangeLabel(DmmFunction::AcCurrent, 0.02), u"20mA"_s);
        QCOMPARE(dmm::rangeLabel(DmmFunction::Resistance, 20e6), u"20MΩ"_s);
        QCOMPARE(dmm::rangeLabel(DmmFunction::Resistance, 2e3), u"2kΩ"_s);
        QCOMPARE(dmm::rangeLabel(DmmFunction::Resistance, 200), u"200Ω"_s);
    }

    // The ELVISmx table of algorithms doc 4: (function, range) -> printf format and unit.
    void displayFormats_data()
    {
        QTest::addColumn<int>("function");
        QTest::addColumn<double>("range");
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("unit");
        auto row = [](const char* name, DmmFunction f, double range, double value, const char* text, const QString& unit) {
            QTest::newRow(name) << int(f) << range << value << QString::fromUtf8(text) << unit;
        };
        row("dcv 60", DmmFunction::DcVoltage, 60, 1.2345, "1.2", u"V"_s);
        row("dcv 60 zero", DmmFunction::DcVoltage, 60, 0.0, "0.0", u"V"_s);
        row("dcv 60 neg", DmmFunction::DcVoltage, 60, -5.0, "-5.0", u"V"_s);
        row("dcv 60 negzero", DmmFunction::DcVoltage, 60, -0.01, "0.0", u"V"_s);
        row("dcv 20", DmmFunction::DcVoltage, 20, 12.345, "12.35", u"V"_s);
        row("dcv 2", DmmFunction::DcVoltage, 2, 1.2345, "1.235", u"V"_s);
        row("dcv 200m", DmmFunction::DcVoltage, 0.2, 0.12345, "123.5", u"mV"_s);
        row("acv 2", DmmFunction::AcVoltage, 2, 0.7071, "0.707", u"V"_s);
        row("dca 1", DmmFunction::DcCurrent, 1, 0.5, "0.500", u"A"_s);
        row("dca 200m", DmmFunction::DcCurrent, 0.2, 0.01234, "12.3", u"mA"_s);
        row("dca 20m", DmmFunction::DcCurrent, 0.02, 0.01234, "12.34", u"mA"_s);
        row("ohm 20M", DmmFunction::Resistance, 20e6, 10e3, "0.01", u"MΩ"_s);
        row("ohm 2M", DmmFunction::Resistance, 2e6, 10.7e3, "0.011", u"MΩ"_s);
        row("ohm 200k", DmmFunction::Resistance, 200e3, 10.7e3, "10.7", u"kΩ"_s);
        row("ohm 20k", DmmFunction::Resistance, 20e3, 4.7e3, "4.70", u"kΩ"_s);
        row("ohm 2k", DmmFunction::Resistance, 2e3, 1.5e3, "1.500", u"kΩ"_s);
        row("ohm 200", DmmFunction::Resistance, 200, 47.25, "47.3", u"Ω"_s);
        row("diode", DmmFunction::Diode, 2, 0.6504, "0.650", u"V"_s);
        row("continuity", DmmFunction::Continuity, 200, 0.8, "0.8", u"Ω"_s);
    }
    void displayFormats()
    {
        QFETCH(int, function);
        QFETCH(double, range);
        QFETCH(double, value);
        QFETCH(QString, text);
        QFETCH(QString, unit);
        const dmm::DisplayFormat format = dmm::displayFormat(DmmFunction(function), range);
        QCOMPARE(dmm::formatNumber(value, format), text);
        QCOMPARE(format.unit, unit);
    }

    void overloadLimits()
    {
        QCOMPARE(dmm::overloadLimit(DmmFunction::DcVoltage, 60), 59.99);
        QCOMPARE(dmm::overloadLimit(DmmFunction::DcVoltage, 20), 19.99);
        QCOMPARE(dmm::overloadLimit(DmmFunction::DcVoltage, 2), 1.999);
        QVERIFY(qFuzzyCompare(dmm::overloadLimit(DmmFunction::DcVoltage, 0.2), 0.1999));
        QCOMPARE(dmm::overloadLimit(DmmFunction::DcCurrent, 1), 0.999);
        QVERIFY(qFuzzyCompare(dmm::overloadLimit(DmmFunction::DcCurrent, 0.02), 0.01999));
        QVERIFY(qFuzzyCompare(dmm::overloadLimit(DmmFunction::Resistance, 20e6), 19.99e6));
        QCOMPARE(dmm::overloadLimit(DmmFunction::Diode, 2), 1.999);
        QCOMPARE(dmm::overloadLimit(DmmFunction::Continuity, 200), 199.99);

        QVERIFY(dmm::isOverload(DmmFunction::Diode, 2, {1.9995, false}));
        QVERIFY(!dmm::isOverload(DmmFunction::Diode, 2, {1.998, false}));
        QVERIFY(dmm::isOverload(DmmFunction::DcVoltage, 2, {-2.5, false}));   // negative too
        QVERIFY(dmm::isOverload(DmmFunction::DcVoltage, 60, {1.0, true}));     // device flag
        QCOMPARE(dmm::overloadText(DmmFunction::DcVoltage, -3), u"+OVER"_s);   // ELVISmx: +Over for both signs
        QCOMPARE(dmm::overloadText(DmmFunction::DcVoltage, -3, true), u"-OVER"_s);
        QCOMPARE(dmm::overloadText(DmmFunction::AcVoltage, 3, true), u"+OVER"_s);
        QCOMPARE(dmm::overloadText(DmmFunction::Resistance, -1, true), u"+OVER"_s);
    }

    // Algorithms doc 3: value -> range, no hysteresis, several ranges at once.
    void autoRange_data()
    {
        QTest::addColumn<int>("function");
        QTest::addColumn<int>("current");
        QTest::addColumn<double>("magnitude");
        QTest::addColumn<bool>("overload");
        QTest::addColumn<double>("expected");
        auto row = [](const char* name, DmmFunction f, int current, double m, bool over, double expected) {
            QTest::newRow(name) << int(f) << current << m << over << expected;
        };
        row("dcv 1.2345 from 60", DmmFunction::DcVoltage, 0, 1.2345, false, 2.0);
        row("dcv 19.99 -> 60", DmmFunction::DcVoltage, 3, 19.99, false, 60.0);
        row("dcv 19.98 -> 20", DmmFunction::DcVoltage, 0, 19.98, false, 20.0);
        row("dcv 1.999 -> 20", DmmFunction::DcVoltage, 0, 1.999, false, 20.0);
        row("dcv 0.1999 -> 2", DmmFunction::DcVoltage, 0, 0.1999, false, 2.0);
        row("dcv 0.1998 -> 200m", DmmFunction::DcVoltage, 0, 0.1998, false, 0.2);
        row("dcv overload on 200m climbs", DmmFunction::DcVoltage, 3, 0.15, true, 2.0);
        row("acv 0.7071 -> 2", DmmFunction::AcVoltage, 0, 0.7071, false, 2.0);
        row("acv 5 -> 20", DmmFunction::AcVoltage, 2, 5.0, false, 20.0);
        row("dca 12.34m -> 20m", DmmFunction::DcCurrent, 0, 0.01234, false, 0.02);
        row("dca 0.1999 -> 1", DmmFunction::DcCurrent, 2, 0.1999, false, 1.0);
        row("ohm 4.7k from 20M (shortcut)", DmmFunction::Resistance, 0, 4.7e3, false, 20e3);
        row("ohm 150 from 20M (shortcut)", DmmFunction::Resistance, 0, 150.0, false, 20e3);
        row("ohm 150 from 20k", DmmFunction::Resistance, 3, 150.0, false, 200.0);
        row("ohm 8.26k from 2k", DmmFunction::Resistance, 4, 8.26e3, false, 20e3);
        row("ohm 12k from 20M", DmmFunction::Resistance, 0, 12e3, false, 20e3);
        row("ohm 150k from 20M", DmmFunction::Resistance, 0, 150e3, false, 200e3);
        row("ohm open stays 20M", DmmFunction::Resistance, 0, 1e9, true, 20e6);
        row("diode fixed", DmmFunction::Diode, 0, 5, true, 2.0);
    }
    void autoRange()
    {
        QFETCH(int, function);
        QFETCH(int, current);
        QFETCH(double, magnitude);
        QFETCH(bool, overload);
        QFETCH(double, expected);
        const std::vector<double> ranges = dmm::defaultRanges(DmmFunction(function));
        const int index = dmm::autoRangeIndex(DmmFunction(function), ranges, current, magnitude, overload);
        QVERIFY(index >= 0 && size_t(index) < ranges.size());
        QCOMPARE(ranges[size_t(index)], expected);
    }

    void statistics()
    {
        dmm::Statistics s;
        QCOMPARE(s.count(), 0);
        QVERIFY(std::isnan(s.mean()));
        for (double v : {1.0, 2.0, 3.0, 4.0})
            s.add(v);
        s.addOverload();
        QCOMPARE(s.count(), 4);
        QCOMPARE(s.overloads(), 1);
        QCOMPARE(s.minimum(), 1.0);
        QCOMPARE(s.maximum(), 4.0);
        QCOMPARE(s.mean(), 2.5);
        QVERIFY(qFuzzyCompare(s.standardDeviation(), std::sqrt(5.0 / 3.0)));
        s.reset();
        QCOMPARE(s.count(), 0);
        QCOMPARE(s.overloads(), 0);
    }

    void logFormat()
    {
        QCOMPARE(dmm::logHeader(u'\t'),
                 u"Time (ISO 8601)\tElapsed (s)\tFunction\tRange\tValue\tUnit\tOverload\tRelative\tNull value"_s);
        dmm::LogRow row{u"2026-09-28T10:00:00.000+02:00"_s, 1.5, u"DC Voltage"_s, u"2V"_s, 1.235, u"V"_s, false, false, 0};
        QCOMPARE(dmm::logLine(row, u'\t'), u"2026-09-28T10:00:00.000+02:00\t1.500\tDC Voltage\t2V\t1.235\tV\t0\t0\t"_s);
        row.relative = true;
        row.nullValue = 1.2;
        row.value = 0.035;
        QCOMPARE(dmm::logLine(row, u','), u"2026-09-28T10:00:00.000+02:00,1.500,DC Voltage,2V,0.035,V,0,1,1.2"_s);
        row.overload = true;
        QVERIFY(dmm::logLine(row, u',').contains(u"2V,,V,1,1"_s));
    }
};

QTEST_APPLESS_MAIN(TestDmmLogic)
#include "tst_dmmlogic.moc"
