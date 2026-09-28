#include "mydaq/dsp/decimate.h"

#include <QTest>

using namespace mydaq::dsp;

class TestDecimate : public QObject {
    Q_OBJECT

private slots:
    void rampBuckets()
    {
        std::vector<double> x(1000);
        for (size_t k = 0; k < x.size(); ++k)
            x[k] = double(k);
        const MinMaxEnvelope env = minMaxDecimate(x, 10);
        QCOMPARE(env.minimum.size(), size_t(10));
        QCOMPARE(env.end, size_t(1000));
        for (size_t i = 0; i < 10; ++i) {
            QCOMPARE(env.first[i], i * 100);
            QCOMPARE(env.minimum[i], double(i * 100));
            QCOMPARE(env.maximum[i], double(i * 100 + 99));
        }
    }

    void spikesSurvive()
    {
        std::vector<double> x(100000, 0.0);
        x[12345] = 5.0;
        x[67890] = -3.0;
        const MinMaxEnvelope env = minMaxDecimate(x, 700);
        QCOMPARE(env.minimum.size(), size_t(700));
        double hi = 0.0;
        double lo = 0.0;
        for (size_t i = 0; i < env.minimum.size(); ++i) {
            hi = std::max(hi, env.maximum[i]);
            lo = std::min(lo, env.minimum[i]);
            const size_t end = i + 1 < env.first.size() ? env.first[i + 1] : env.end;
            if (12345 >= env.first[i] && 12345 < end)
                QCOMPARE(env.maximum[i], 5.0);
        }
        QCOMPARE(hi, 5.0);
        QCOMPARE(lo, -3.0);
    }

    void rangeAndFewSamples()
    {
        const std::vector<double> x{3, 1, 4, 1, 5, 9, 2, 6};
        const MinMaxEnvelope all = minMaxDecimate(x, 100);   // fewer samples than buckets
        QCOMPARE(all.minimum, x);
        QCOMPARE(all.maximum, x);
        const MinMaxEnvelope part = minMaxDecimate(x, 2, 2, 6);   // {4, 1 | 5, 9}
        QCOMPARE(part.minimum, (std::vector<double>{1, 5}));
        QCOMPARE(part.maximum, (std::vector<double>{4, 9}));
        QCOMPARE(part.first, (std::vector<size_t>{2, 4}));
        QCOMPARE(part.end, size_t(6));
        QVERIFY(minMaxDecimate(x, 0).minimum.empty());
        QVERIFY(minMaxDecimate({}, 10).minimum.empty());
    }
};

QTEST_GUILESS_MAIN(TestDecimate)
#include "tst_decimate.moc"
