// Audio Equalizer DSP (no Qt objects; unit-tested in tests/tst_aeq.cpp):
//   - Bessel IIR filter design (analog prototype normalized to -3 dB at the cutoff, lowpass and
//     lowpass-to-bandpass transforms, bilinear transform with prewarped band edges), realized
//     as cascades of second-order sections;
//   - the three-band equalizer of the ELVISmx "Audio Equalizer" featured application: Bass =
//     4th-order Bessel lowpass 320 Hz, Midtone = 4th-order Bessel bandpass 320-2560 Hz, Treble =
//     4th-order Bessel bandpass 2560-20 840 Hz, output = volume x sum(10^(dB/20) x band);
//   - fractional-octave band analysis of a power spectrum (the 1/24-octave display) and level
//     meters.
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace mybench::instruments::aeq {

using Complex = std::complex<double>;

// One second-order section, transposed direct form II. a0 = 1.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double s1 = 0, s2 = 0;   // state

    double process(double x)
    {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    Complex response(double frequency, double sampleRate) const;
    void reset() { s1 = s2 = 0; }
};

class Cascade
{
public:
    std::vector<Biquad> sections;

    double process(double x)
    {
        for (Biquad& s : sections)
            x = s.process(x);
        return x;
    }
    Complex response(double frequency, double sampleRate) const;
    void reset();
    // Largest pole radius (< 1 for a stable filter).
    double maxPoleRadius() const;
    // Flushes tiny state values to zero (avoids denormal slowdowns in silence).
    void flushDenormals();
};

// Poles of the Bessel analog lowpass prototype of `order` (2..10), normalized so that
// |H(j1)| = 1/sqrt(2) (-3 dB at 1 rad/s).
std::vector<Complex> besselPrototypePoles(int order);
// Digital Bessel lowpass (-3 dB at `cutoff` Hz) and bandpass (-3 dB at `low` and `high` Hz;
// 2 x order poles), unity gain at DC / at the geometric band center.
Cascade besselLowpass(int order, double cutoff, double sampleRate);
Cascade besselBandpass(int order, double low, double high, double sampleRate);

// Band definitions recovered from ELVISmx (docs/instruments/algorithms/featured.md).
constexpr int kFilterOrder = 4;
constexpr double kBassCutoff = 320.0;
constexpr double kMidLow = 320.0;
constexpr double kMidHigh = 2560.0;
constexpr double kTrebleLow = 2560.0;
constexpr double kTrebleHigh = 20840.0;
constexpr double kMaxGainDb = 12.0;

enum Band { Bass, Mid, Treble, BandCount };

struct EqSettings {
    std::array<double, BandCount> gainsDb{1.0, 1.0, 1.0};   // ELVISmx saved defaults
    double volume = 0.5;                                     // 0..1
    bool bypass = false;                                     // output = volume x input

    std::array<double, BandCount> linearGains() const;
};

// The three band filters of one audio channel.
class Equalizer
{
public:
    explicit Equalizer(double sampleRate = 50000.0);
    double sampleRate() const { return m_sampleRate; }
    const Cascade& band(Band b) const { return m_bands[b]; }

    // Filters `input` into `output` (same length). Gains and volume ramp linearly over the block
    // from the previous call's settings (no zipper noise). The filters always run, so leaving
    // bypass has no transient.
    void process(std::span<const double> input, std::span<double> output, const EqSettings& settings);
    void reset();

    // Transfer function of the whole equalizer (complex sum of the bands).
    Complex response(double frequency, const EqSettings& settings) const;
    Complex bandResponse(Band b, double frequency) const { return m_bands[b].response(frequency, m_sampleRate); }

private:
    double m_sampleRate;
    std::array<Cascade, BandCount> m_bands;
    std::array<double, BandCount> m_gains{};
    double m_volume = -1;   // < 0: no previous block
    bool m_bypass = false;
};

// Fractional-octave bands (base 2, centers 1 kHz x 2^(k / fraction)) whose centers lie in
// [lowest, highest].
struct OctaveBands {
    std::vector<double> centers;
    std::vector<double> lower;
    std::vector<double> upper;
};
OctaveBands octaveBands(int fraction, double lowest, double highest);
// Power in each band from a one-sided power spectrum (bin k at k x df, V^2 per bin): each bin's
// power is spread over [f - df/2, f + df/2] and integrated over the band, so narrow bands below
// the bin spacing still get their share.
std::vector<double> bandPowers(const OctaveBands& bands, std::span<const double> power, double df);

// FFT length of the spectrum display for `filled` samples of history at sample rate `fs`: the
// largest power of two that fits, at most the 1 s window capped at 65536 points (1.3 s at 50 kS/s,
// resolves the 1/24-octave bands from 25 Hz; a 262144-point FFT every 100 ms made the 200 kS/s
// stream drop out), and 0 until 0.3 s (the old, coarser window) has been collected
// so the first spectrum appears quickly and sharpens as the history grows.
size_t analysisLength(size_t filled, double fs);
size_t maximumAnalysisLength(double fs);   // the 1 s window, at most 65536 points

// Peak and RMS level of a block, in dB relative to `fullScale` volts peak (dBFS; sine at full
// scale: peak 0 dBFS, RMS -3 dBFS). -120 for silence.
struct Level {
    double peakDb = -120;
    double rmsDb = -120;
};
Level measureLevel(std::span<const double> x, double fullScale);

}  // namespace mybench::instruments::aeq
