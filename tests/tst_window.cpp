#include "mydaq/dsp/window.h"

#include <QTest>

#include <cmath>

using namespace mydaq::dsp;

Q_DECLARE_METATYPE(mydaq::dsp::WindowType)

class TestWindow : public QObject {
    Q_OBJECT

private slots:
    // Published coherent gain and ENBW (two decimals, as tabulated by Harris 1978 / NI).
    void knownProperties_data()
    {
        QTest::addColumn<WindowType>("type");
        QTest::addColumn<QString>("name");
        QTest::addColumn<double>("cg");
        QTest::addColumn<double>("enbw");
        QTest::addColumn<int>("lobe");
        QTest::newRow("none") << WindowType::None << "None" << 1.00 << 1.00 << 1;
        QTest::newRow("hanning") << WindowType::Hanning << "Hanning" << 0.50 << 1.50 << 2;
        QTest::newRow("hamming") << WindowType::Hamming << "Hamming" << 0.54 << 1.36 << 2;
        QTest::newRow("bh3") << WindowType::BlackmanHarris << "Blackman-Harris" << 0.42 << 1.71 << 3;
        QTest::newRow("exact") << WindowType::ExactBlackman << "Exact Blackman" << 0.43 << 1.69 << 3;
        QTest::newRow("blackman") << WindowType::Blackman << "Blackman" << 0.42 << 1.73 << 3;
        QTest::newRow("flattop") << WindowType::FlatTop << "Flat Top" << 0.22 << 3.77 << 5;
        QTest::newRow("bh4") << WindowType::BlackmanHarris4 << "4 Term B-Harris" << 0.36 << 2.00 << 4;
        QTest::newRow("bh7") << WindowType::BlackmanHarris7 << "7 Term B-Harris" << 0.27 << 2.63 << 7;
        QTest::newRow("lowside") << WindowType::LowSidelobe << "Low Sidelobe" << 0.32 << 2.22 << 5;
    }
    void knownProperties()
    {
        QFETCH(WindowType, type);
        QFETCH(QString, name);
        QFETCH(double, cg);
        QFETCH(double, enbw);
        QFETCH(int, lobe);
        QCOMPARE(windowName(type), name);
        QVERIFY(std::abs(coherentGain(type) - cg) < 0.006);
        QVERIFY(std::abs(equivalentNoiseBandwidth(type) - enbw) < 0.006);
        QCOMPARE(mainLobeHalfWidth(type), lobe);

        // Actual coefficients match the theoretical values (exact for periodic windows).
        const std::vector<double> w = makeWindow(type, 4096);
        QVERIFY(std::abs(coherentGain(w) - coherentGain(type)) < 1e-12);
        QVERIFY(std::abs(equivalentNoiseBandwidth(w) - equivalentNoiseBandwidth(type)) < 1e-9);
        // Periodic (DFT-even) symmetry: w[k] == w[N - k].
        for (size_t k = 1; k < w.size(); ++k)
            QVERIFY(std::abs(w[k] - w[w.size() - k]) < 1e-12);
    }

    void menuOrder()
    {
        const std::vector<WindowType>& types = windowTypes();
        QCOMPARE(types.size(), size_t(10));
        QCOMPARE(types.front(), WindowType::None);
        QCOMPARE(types.back(), WindowType::LowSidelobe);
    }

    void hanningShape()
    {
        const std::vector<double> w = makeWindow(WindowType::Hanning, 8);
        QVERIFY(std::abs(w[0]) < 1e-15);
        QVERIFY(std::abs(w[4] - 1.0) < 1e-15);
        QVERIFY(std::abs(w[2] - 0.5) < 1e-15);
        QCOMPARE(makeWindow(WindowType::Hanning, 1), std::vector<double>{1.0});
        QVERIFY(makeWindow(WindowType::Hanning, 0).empty());
    }

    void apply()
    {
        std::vector<double> x(16, 2.0);
        applyWindow(x, WindowType::Hanning);
        const std::vector<double> w = makeWindow(WindowType::Hanning, 16);
        for (size_t k = 0; k < x.size(); ++k)
            QCOMPARE(x[k], 2.0 * w[k]);
    }
};

QTEST_GUILESS_MAIN(TestWindow)
#include "tst_window.moc"
