// Time-domain windows for spectral analysis: the set offered by the ELVISmx Dynamic Signal Analyzer.
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

namespace mydaq::dsp {

// Sum-of-cosines windows in periodic (DFT-even) form: w[k] = sum_m (-1)^m a_m cos(2*pi*m*k/N).
enum class WindowType {
    None,              // rectangular (uniform)
    Hanning,
    Hamming,
    BlackmanHarris,    // 3-term Blackman-Harris
    ExactBlackman,
    Blackman,
    FlatTop,           // best amplitude accuracy for tones between bins
    BlackmanHarris4,   // "4 Term B-Harris" (-92 dB sidelobes)
    BlackmanHarris7,   // "7 Term B-Harris" (-180 dB sidelobes)
    LowSidelobe,
};

const std::vector<WindowType>& windowTypes();   // every type, in DSA menu order
QString windowName(WindowType type);            // "None", "Hanning", ..., "4 Term B-Harris", "Low Sidelobe"

std::vector<double> makeWindow(WindowType type, size_t n);   // n == 1 gives {1}
void applyWindow(std::vector<double>& x, WindowType type);  // x[k] *= w[k]

// Theoretical properties (large-N limit of the periodic window).
double coherentGain(WindowType type);              // mean of the window (a0); 1 for None
double equivalentNoiseBandwidth(WindowType type);  // ENBW in bins; 1 for None, 1.5 for Hanning
int mainLobeHalfWidth(WindowType type);            // bins from the peak to the first null (= number of terms)

// Properties of actual coefficients: CG = sum(w) / N, ENBW = N * sum(w^2) / sum(w)^2 (bins).
double coherentGain(const std::vector<double>& window);
double equivalentNoiseBandwidth(const std::vector<double>& window);

}  // namespace mydaq::dsp
