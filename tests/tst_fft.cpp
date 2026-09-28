#include "mydaq/dsp/fft.h"

#include <QTest>

#include <cmath>
#include <numbers>
#include <random>

using namespace mydaq::dsp;

namespace {

std::vector<Complex> naiveDft(const std::vector<Complex>& x, double sign)
{
    const size_t n = x.size();
    std::vector<Complex> out(n);
    for (size_t k = 0; k < n; ++k) {
        Complex sum;
        for (size_t j = 0; j < n; ++j)
            sum += x[j] * std::polar(1.0, sign * 2.0 * std::numbers::pi * double((k * j) % n) / double(n));
        out[k] = sum;
    }
    return out;
}

std::vector<Complex> randomComplex(size_t n, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<Complex> x(n);
    for (Complex& c : x)
        c = Complex(u(rng), u(rng));
    return x;
}

double maxError(const std::vector<Complex>& a, const std::vector<Complex>& b)
{
    double e = 0.0;
    for (size_t k = 0; k < std::min(a.size(), b.size()); ++k)
        e = std::max(e, std::abs(a[k] - b[k]));
    return e;
}

void addSizes()
{
    QTest::addColumn<int>("n");
    for (int n : {1, 2, 3, 4, 5, 7, 8, 12, 16, 17, 31, 64, 97, 100, 127, 128, 257, 1000, 1024, 1031})
        QTest::newRow(qPrintable(QString::number(n))) << n;
}

}  // namespace

class TestFft : public QObject {
    Q_OBJECT

private slots:
    void forwardMatchesNaiveDft_data() { addSizes(); }
    void forwardMatchesNaiveDft()
    {
        QFETCH(int, n);
        const std::vector<Complex> x = randomComplex(size_t(n), unsigned(n));
        std::vector<Complex> y = x;
        fft(y);
        QVERIFY2(maxError(y, naiveDft(x, -1.0)) < 1e-10 * n, "FFT differs from the naive DFT");
    }

    void inverseMatchesNaiveDft_data() { addSizes(); }
    void inverseMatchesNaiveDft()
    {
        QFETCH(int, n);
        const std::vector<Complex> x = randomComplex(size_t(n), unsigned(n) + 7);
        std::vector<Complex> y = x;
        ifft(y);
        std::vector<Complex> expected = naiveDft(x, +1.0);
        for (Complex& c : expected)
            c /= double(n);
        QVERIFY(maxError(y, expected) < 1e-12 * n);
    }

    void roundTrip_data() { addSizes(); }
    void roundTrip()
    {
        QFETCH(int, n);
        const std::vector<Complex> x = randomComplex(size_t(n), 99);
        std::vector<Complex> y = x;
        fft(y);
        ifft(y);
        QVERIFY(maxError(x, y) < 1e-12 * n);
    }

    void realInput_data() { addSizes(); }
    void realInput()
    {
        QFETCH(int, n);
        const std::vector<Complex> c = randomComplex(size_t(n), 5);
        std::vector<double> x(static_cast<size_t>(n));
        std::vector<Complex> xc(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            x[size_t(k)] = c[size_t(k)].real();
            xc[size_t(k)] = x[size_t(k)];
        }
        const std::vector<Complex> bins = rfft(x);
        QCOMPARE(bins.size(), size_t(n / 2 + 1));
        QVERIFY(maxError(bins, naiveDft(xc, -1.0)) < 1e-10 * n);
    }

    void largePrimeLength()
    {
        const size_t n = 10007;   // prime: Bluestein path
        std::vector<Complex> x(n);
        for (size_t k = 0; k < n; ++k)
            x[k] = std::cos(2.0 * std::numbers::pi * 123.0 * double(k) / double(n));
        fft(x);
        QVERIFY(std::abs(std::abs(x[123]) - double(n) / 2.0) < 1e-6);
        QVERIFY(std::abs(x[124]) < 1e-6);
    }

    void helpers()
    {
        QVERIFY(isPowerOfTwo(1));
        QVERIFY(isPowerOfTwo(1024));
        QVERIFY(!isPowerOfTwo(0));
        QVERIFY(!isPowerOfTwo(1000));
        QCOMPARE(nextPowerOfTwo(0), size_t(1));
        QCOMPARE(nextPowerOfTwo(1000), size_t(1024));
        QCOMPARE(nextPowerOfTwo(1024), size_t(1024));
        QVERIFY(rfft({}).empty());
    }
};

QTEST_GUILESS_MAIN(TestFft)
#include "tst_fft.moc"
