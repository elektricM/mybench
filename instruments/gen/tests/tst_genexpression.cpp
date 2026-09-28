// Expression components of the Waveform Editor (docs/instruments/gen.md 4.3.5).
#include "genexpression.h"

#include <QTest>

#include <cmath>
#include <numbers>

using mybench::instruments::gen::Expression;

class TestGenExpression : public QObject
{
    Q_OBJECT

private slots:
    void values_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("expected");
        QTest::newRow("number") << "2.5" << 0.0 << 2.5;
        QTest::newRow("exponent") << "1e-3" << 0.0 << 1e-3;
        QTest::newRow("variable") << "x" << 3.0 << 3.0;
        QTest::newRow("precedence") << "1 + 2 * x" << 3.0 << 7.0;
        QTest::newRow("parentheses") << "(1 + 2) * x" << 3.0 << 9.0;
        QTest::newRow("power right assoc") << "2^3^2" << 0.0 << 512.0;
        QTest::newRow("unary minus below power") << "-x^2" << 3.0 << -9.0;
        QTest::newRow("negative exponent") << "2^-1" << 0.0 << 0.5;
        QTest::newRow("sine") << "sin(2*pi*x)" << 0.25 << 1.0;
        QTest::newRow("constants") << "e + PI" << 0.0 << std::numbers::e + std::numbers::pi;
        QTest::newRow("case insensitive") << "SIN(X)" << 0.5 << std::sin(0.5);
        QTest::newRow("log10") << "log(1000)" << 0.0 << 3.0;
        QTest::newRow("ln") << "ln(e)" << 0.0 << 1.0;
        QTest::newRow("max") << "max(x, 2)" << 1.0 << 2.0;
        QTest::newRow("min") << "min(x, 2)" << 1.0 << 1.0;
        QTest::newRow("mod floor") << "mod(-1, 3)" << 0.0 << 2.0;
        QTest::newRow("abs sqrt") << "sqrt(abs(-16))" << 0.0 << 4.0;
        QTest::newRow("floor ceil") << "floor(1.5) + ceil(1.5)" << 0.0 << 3.0;
        QTest::newRow("extensions") << "sign(-2) + tanh(0) + cosh(0)" << 0.0 << 0.0;
        QTest::newRow("sum of harmonics") << "sin(2*pi*x) + 0.3*sin(6*pi*x)" << 0.25 << 1.0 - 0.3;
    }
    void values()
    {
        QFETCH(QString, text);
        QFETCH(double, x);
        QFETCH(double, expected);
        const Expression e = Expression::parse(text);
        QVERIFY2(e.ok(), qPrintable(e.error()));
        QVERIFY(std::abs(e.evaluate(x) - expected) < 1e-9);
    }

    void errors_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("error");
        QTest::newRow("missing paren") << "sin(2*pi*x" << "Missing ')' (11)";
        QTest::newRow("unknown function") << "sn(x)" << "Unknown function 'sn' (1)";
        QTest::newRow("end") << "1 +" << "Unexpected end of expression (4)";
        QTest::newRow("unknown name") << "2*y" << "Unknown name 'y' (3)";
        QTest::newRow("stray paren") << "x)" << "Unexpected ')' (2)";
        QTest::newRow("arity") << "max(1)" << "'max' takes two arguments (1)";
        QTest::newRow("empty") << "  " << "Empty expression (3)";
    }
    void errors()
    {
        QFETCH(QString, text);
        QFETCH(QString, error);
        const Expression e = Expression::parse(text);
        QVERIFY(!e.ok());
        QCOMPARE(e.error(), error);
        QCOMPARE(e.evaluate(1.0), 0.0);
    }

    void vectorEvaluation()
    {
        const Expression e = Expression::parse(QStringLiteral("x*x"));
        std::vector<double> xs{1, 2, 3};
        std::vector<double> out(3);
        e.evaluate(xs, out);
        QCOMPARE(out, (std::vector<double>{1, 4, 9}));
        e.evaluate(xs, xs);   // in place
        QCOMPARE(xs, (std::vector<double>{1, 4, 9}));
    }

    void invalidValues()
    {
        QVERIFY(std::isnan(Expression::parse(QStringLiteral("sqrt(-1)")).evaluate(0)));
        QVERIFY(std::isinf(Expression::parse(QStringLiteral("ln(0)")).evaluate(0)));
    }
};

QTEST_GUILESS_MAIN(TestGenExpression)
#include "tst_genexpression.moc"
