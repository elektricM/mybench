#include "mydaq/dsp/fft.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace mydaq::dsp {

namespace {

// Iterative radix-2 Cooley-Tukey, unscaled. sign = -1: forward, +1: inverse.
void radix2(std::vector<Complex>& a, double sign)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    std::vector<Complex> twiddle(n / 2);
    for (size_t k = 0; k < n / 2; ++k)
        twiddle[k] = std::polar(1.0, sign * 2.0 * std::numbers::pi * double(k) / double(n));
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len / 2;
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len) {
            for (size_t j = 0; j < half; ++j) {
                const Complex u = a[i + j];
                const Complex v = a[i + j + half] * twiddle[j * step];
                a[i + j] = u + v;
                a[i + j + half] = u - v;
            }
        }
    }
}

// Bluestein's chirp-z transform: any length through a power-of-two circular convolution.
void bluestein(std::vector<Complex>& a, double sign)
{
    const size_t n = a.size();
    const size_t m = nextPowerOfTwo(2 * n - 1);
    std::vector<Complex> chirp(n);
    const uint64_t period = 2 * uint64_t(n);
    for (size_t k = 0; k < n; ++k) {
        const uint64_t kk = (uint64_t(k) * k) % period;   // exact k^2 mod 2N keeps the angle accurate
        chirp[k] = std::polar(1.0, sign * std::numbers::pi * double(kk) / double(n));
    }
    std::vector<Complex> x(m), y(m);
    for (size_t k = 0; k < n; ++k)
        x[k] = a[k] * chirp[k];
    y[0] = std::conj(chirp[0]);
    for (size_t k = 1; k < n; ++k)
        y[k] = y[m - k] = std::conj(chirp[k]);
    radix2(x, -1.0);
    radix2(y, -1.0);
    for (size_t k = 0; k < m; ++k)
        x[k] *= y[k];
    radix2(x, +1.0);
    const double scale = 1.0 / double(m);
    for (size_t k = 0; k < n; ++k)
        a[k] = x[k] * chirp[k] * scale;
}

void transform(std::vector<Complex>& data, double sign)
{
    if (data.size() <= 1)
        return;
    if (isPowerOfTwo(data.size()))
        radix2(data, sign);
    else
        bluestein(data, sign);
}

}  // namespace

bool isPowerOfTwo(size_t n) { return n != 0 && (n & (n - 1)) == 0; }

size_t nextPowerOfTwo(size_t n)
{
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

void fft(std::vector<Complex>& data) { transform(data, -1.0); }

void ifft(std::vector<Complex>& data)
{
    transform(data, +1.0);
    if (data.empty())
        return;
    const double scale = 1.0 / double(data.size());
    for (Complex& c : data)
        c *= scale;
}

std::vector<Complex> rfft(const std::vector<double>& x)
{
    const size_t n = x.size();
    if (n == 0)
        return {};
    if (n % 2 != 0) {
        std::vector<Complex> full(x.begin(), x.end());
        fft(full);
        full.resize(n / 2 + 1);
        return full;
    }
    // Pack even/odd samples into one half-size complex signal z = e + i*o, then split:
    // E[k] = (Z[k] + conj(Z[h-k])) / 2, O[k] = (Z[k] - conj(Z[h-k])) / 2i, X[k] = E[k] + W^k O[k].
    const size_t h = n / 2;
    std::vector<Complex> z(h);
    for (size_t k = 0; k < h; ++k)
        z[k] = Complex(x[2 * k], x[2 * k + 1]);
    fft(z);
    std::vector<Complex> out(h + 1);
    for (size_t k = 0; k <= h; ++k) {
        const Complex zk = z[k % h];
        const Complex zc = std::conj(z[(h - k) % h]);
        const Complex even = 0.5 * (zk + zc);
        const Complex odd = Complex(0.0, -0.5) * (zk - zc);
        out[k] = even + std::polar(1.0, -2.0 * std::numbers::pi * double(k) / double(n)) * odd;
    }
    return out;
}

}  // namespace mydaq::dsp
