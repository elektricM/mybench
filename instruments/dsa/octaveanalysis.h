// Third-octave analysis for the Spectrum Analyzer's "1/3 Octave" mode (the ELVISmx featured
// application "Octave Analyzer", docs/instruments/dsa.md).
//
// Base-10 third-octave bands (IEC 61260): exact centres fm = 1000 · 10^(n/10) Hz, edges
// fm · 10^(∓1/20), nominal labels 20 Hz … 20 kHz (the 31 ISO bands of ELVISmx). Each band is a 6th-order Butterworth band-pass
// (3rd-order low-pass prototype transformed to a band-pass, bilinear transform with prewarped
// edges, three biquads with their zeros at z = ±1, normalized to 0 dB at the centre, double
// precision). The squared output of every band goes through an exponential averager (sound level
// meter time weighting: Slow 1 s, Fast 125 ms, Impulse 35 ms rise / 1.5 s decay, or a custom
// time constant). Frequency weightings A/B/C (IEC 61672-1) are applied to the band powers at the
// band centres, as the ELVISmx application does.
#pragma once

#include <QString>

#include <array>
#include <complex>
#include <span>
#include <vector>

namespace mybench::instruments::octave {

// Acquisition of the ELVISmx application: 50 kS/s continuous, reads of 1000 samples.
inline constexpr double kSampleRate = 50000.0;
inline constexpr int kBlockSize = 1000;

struct Band {
    double centre = 0;    // exact centre, Hz
    double nominal = 0;   // nominal (preferred) centre, Hz
    double lower = 0;     // lower edge, Hz
    double upper = 0;     // upper edge, Hz
    QString label;        // "31.5 Hz", "1.25 kHz"
};

// The 31 ISO 1/3-octave bands 20 Hz … 20 kHz, lowest first (Low/High Band are indices 0..30).
const std::vector<Band>& bands();
// Index of the band whose nominal centre is closest to `hertz`.
int bandIndex(double hertz);

enum class Weighting { Z, A, B, C };   // Z = none (Linear)
// Weighting gain in dB at `frequency` (IEC 61672-1 analytic expressions, 0 dB at 1 kHz).
double weightingDb(Weighting weighting, double frequency);

enum class Averaging { Slow, Fast, Impulse, Custom };
struct TimeConstants {
    double rise = 0.125;   // s, while the level increases
    double fall = 0.125;   // s, while it decreases
};
// Slow 1 s, Fast 125 ms, Impulse 35 ms / 1.5 s, Custom `customMs` (0 = no averaging).
TimeConstants timeConstants(Averaging averaging, double customMs);

// One second-order section, transposed direct form II; b1 = 0 (zeros at z = ±1).
struct Biquad {
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0;
    double s1 = 0, s2 = 0;
    double process(double x)
    {
        const double y = b0 * x + s1;
        s1 = -a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { s1 = s2 = 0; }
    std::complex<double> response(double omega) const;   // omega in rad/sample
};

// 6th-order Butterworth band-pass between `lower` and `upper` (Hz).
class BandPassFilter
{
public:
    BandPassFilter(double lower, double upper, double centre, double sampleRate);
    double process(double x)
    {
        return m_sections[2].process(m_sections[1].process(m_sections[0].process(x)));
    }
    void reset();
    // Complex response at `frequency` (Hz).
    std::complex<double> response(double frequency) const;
    double responseDb(double frequency) const;
    // Time for the slowest pole's transient to decay to 1 % (s).
    double settlingTime() const { return m_settlingTime; }

private:
    std::array<Biquad, 3> m_sections;
    double m_sampleRate;
    double m_settlingTime = 0;
};

// The whole bank: every band filtered and averaged, sample by sample.
class OctaveFilterBank
{
public:
    explicit OctaveFilterBank(double sampleRate = kSampleRate);

    double sampleRate() const { return m_sampleRate; }
    size_t bandCount() const { return m_filters.size(); }
    const BandPassFilter& filter(size_t band) const { return m_filters[band]; }

    // Averager time constants; the current averages are kept.
    void setTimeConstants(const TimeConstants& constants);
    // Clears the filter states, the averages and the elapsed time.
    void reset();
    void process(std::span<const double> samples);

    // Averaged mean square of each band's output (V²).
    const std::vector<double>& meanSquare() const { return m_meanSquare; }
    // Seconds of signal processed since the last reset.
    double elapsed() const { return double(m_samples) / m_sampleRate; }

private:
    double m_sampleRate;
    std::vector<BandPassFilter> m_filters;
    std::vector<double> m_meanSquare;
    double m_riseAlpha = 1;
    double m_fallAlpha = 1;
    long long m_samples = 0;
};

}  // namespace mybench::instruments::octave
