#include "mydaq/dsp/units.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace mydaq::dsp;

namespace {

const QString kMicro = QStringLiteral("µ");
const QString kOhm = QStringLiteral("Ω");

}  // namespace

class TestUnits : public QObject {
    Q_OBJECT

private slots:
    void format_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("unit");
        QTest::addColumn<int>("digits");
        QTest::addColumn<bool>("trim");
        QTest::addColumn<QString>("expected");
        QTest::newRow("kHz") << 1234.0 << "Hz" << 4 << true << "1.234 kHz";
        QTest::newRow("mV") << 0.5 << "V" << 4 << true << "500 mV";
        QTest::newRow("mV fixed") << 0.5 << "V" << 4 << false << "500.0 mV";
        QTest::newRow("uA") << -12.3e-6 << "A" << 3 << true << ("-12.3 " + kMicro + "A");
        QTest::newRow("kOhm") << 4700.0 << kOhm << 2 << true << ("4.7 k" + kOhm);
        QTest::newRow("carry") << 999.96 << "V" << 4 << false << "1.000 kV";
        QTest::newRow("carry trimmed") << 999.96 << "V" << 4 << true << "1 kV";
        QTest::newRow("milli boundary") << 1e-3 << "V" << 4 << true << "1 mV";
        QTest::newRow("zero") << 0.0 << "V" << 4 << true << "0 V";
        QTest::newRow("zero fixed") << 0.0 << "V" << 4 << false << "0.000 V";
        QTest::newRow("MHz") << 1e6 << "Hz" << 4 << true << "1 MHz";
        QTest::newRow("no unit") << 1.5 << "" << 4 << true << "1.5";
        QTest::newRow("no unit prefix") << 1500.0 << "" << 4 << true << "1.5 k";
        QTest::newRow("pF") << 2.2e-12 << "F" << 3 << false << "2.20 pF";
        QTest::newRow("rounding") << 1.23456 << "V" << 3 << true << "1.23 V";
        QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN() << "V" << 4 << true << "--- V";
    }
    void format()
    {
        QFETCH(double, value);
        QFETCH(QString, unit);
        QFETCH(int, digits);
        QFETCH(bool, trim);
        QFETCH(QString, expected);
        QCOMPARE(formatEng(value, unit, digits, trim), expected);
    }

    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("unit");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<double>("expected");
        QTest::newRow("k") << "1k" << "" << true << 1e3;
        QTest::newRow("K") << "1K" << "" << true << 1e3;
        QTest::newRow("m") << "2.5m" << "" << true << 2.5e-3;
        QTest::newRow("u") << "10u" << "" << true << 10e-6;
        QTest::newRow("MHz") << "1 MHz" << "" << true << 1e6;
        QTest::newRow("MHz unit") << "1 MHz" << "Hz" << true << 1e6;
        QTest::newRow("mHz unit") << "1 mhz" << "Hz" << true << 1e-3;
        QTest::newRow("mV") << "500 mV" << "V" << true << 0.5;
        QTest::newRow("plain unit") << "-3.3 V" << "" << true << -3.3;
        QTest::newRow("Hz no prefix") << "50Hz" << "" << true << 50.0;
        QTest::newRow("comma") << ("4,7 k" + kOhm) << kOhm << true << 4700.0;
        QTest::newRow("ohm word") << "100 ohm" << kOhm << true << 100.0;
        QTest::newRow("micro sign") << ("1 " + kMicro + "A") << "A" << true << 1e-6;
        QTest::newRow("greek mu") << QStringLiteral("1 μA") << "A" << true << 1e-6;
        QTest::newRow("exponent") << "1e-3" << "" << true << 1e-3;
        QTest::newRow("leading dot") << ".5" << "V" << true << 0.5;
        QTest::newRow("spaces") << "  2 k  " << "" << true << 2e3;
        QTest::newRow("garbage") << "abc" << "" << false << 0.0;
        QTest::newRow("empty") << "" << "" << false << 0.0;
        QTest::newRow("wrong unit") << "5 A" << "V" << false << 0.0;
        QTest::newRow("trailing digits") << "1 k 2" << "" << false << 0.0;
    }
    void parse()
    {
        QFETCH(QString, text);
        QFETCH(QString, unit);
        QFETCH(bool, valid);
        QFETCH(double, expected);
        const std::optional<double> v = parseEng(text, unit);
        QCOMPARE(v.has_value(), valid);
        if (valid)
            QVERIFY2(std::abs(*v - expected) <= 1e-12 * std::abs(expected), qPrintable(QString::number(*v)));
    }

    void roundTrip()
    {
        for (int e = -14; e <= 12; ++e) {
            for (double m : {1.0, 2.345678, 9.87654, -4.2}) {
                const double v = m * std::pow(10.0, e);
                const std::optional<double> back = parseEng(formatEng(v, QStringLiteral("V"), 6), QStringLiteral("V"));
                QVERIFY(back.has_value());
                QVERIFY2(std::abs(*back - v) <= 1e-5 * std::abs(v), qPrintable(formatEng(v, QStringLiteral("V"), 6)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestUnits)
#include "tst_units.moc"
