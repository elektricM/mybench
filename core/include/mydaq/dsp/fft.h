// Discrete Fourier transforms of any length.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace mydaq::dsp {

using Complex = std::complex<double>;

// Forward DFT in place, unscaled: X[k] = sum_n x[n] * exp(-2*pi*i*k*n/N).
// Any length: iterative radix-2 for powers of two, Bluestein's chirp-z otherwise (O(N log N)).
void fft(std::vector<Complex>& data);

// Inverse DFT in place, scaled by 1/N so that ifft(fft(x)) == x.
void ifft(std::vector<Complex>& data);

// DFT of a real signal: bins 0..N/2 (N/2 + 1 values) of the unscaled forward transform.
// Even lengths use a half-size complex transform.
std::vector<Complex> rfft(const std::vector<double>& x);

bool isPowerOfTwo(size_t n);
size_t nextPowerOfTwo(size_t n);   // smallest power of two >= n (1 for n == 0)

}  // namespace mydaq::dsp
