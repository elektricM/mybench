// Windowed amplitude / power spectra of real signals, display-unit conversion and spectrum averaging
// (the ELVISmx DSA feature set).
//
// Conventions: spectra are one-sided (bins 0..N/2, bin k at k * df, df = sampleRate / N) and
// window-compensated, so a sine of RMS amplitude A reads A (power A^2) at its bin whatever the
// window. Powers are in V^2 (RMS). Total power of a tone spread over several bins by the window is
// sum(power) / enbw (see estimatePeak()).
#pragma once

#include "mydaq/dsp/fft.h"
#include "mydaq/dsp/window.h"

#include <QString>

#include <cstddef>
#include <limits>
#include <vector>

namespace mydaq::dsp {

struct SpectrumInfo {
    size_t length = 0;       // time-domain record length N
    double df = 0;           // bin spacing, Hz
    double enbw = 1;         // equivalent noise bandwidth of the window, bins
    int lobeHalfWidth = 1;   // main-lobe half width of the window, bins
    bool hasNyquistBin() const { return length > 0 && length % 2 == 0; }
};

struct ComplexSpectrum {
    SpectrumInfo info;
    std::vector<Complex> bins;   // |bins[k]| = RMS amplitude (V) of a tone at bin k; DC bin = mean
};

struct PowerSpectrum {
    SpectrumInfo info;
    std::vector<double> power;   // V^2 (RMS) per bin
    double frequency(size_t bin) const { return double(bin) * info.df; }
    std::vector<double> frequencies() const;   // frequency axis, Hz
};

ComplexSpectrum complexSpectrum(const std::vector<double>& x, double sampleRate, WindowType window);
PowerSpectrum powerSpectrum(const std::vector<double>& x, double sampleRate, WindowType window);
PowerSpectrum powerOf(const ComplexSpectrum& spectrum);

// ---------------------------------------------------------------------------------------------
// Display units (DSA "Units", "Mode" and "Display Mode" controls)

enum class SpectrumMode { Rms, Peak };          // magnitude of a sine reads its RMS or its peak value
enum class SpectrumUnit { Linear, Db, Dbm };    // V, dB re 1 V (dBV), dB re 1 mW into referenceImpedance

struct SpectrumFormat {
    SpectrumMode mode = SpectrumMode::Rms;
    SpectrumUnit unit = SpectrumUnit::Db;
    bool density = false;              // power spectral density (per Hz of noise bandwidth)
    double referenceImpedance = 50.0;  // ohm, for dBm
};

// Converts powers (V^2 RMS) to display values: Peak doubles the power of AC bins; density divides by
// enbw * df; Linear shows sqrt (V or V/sqrt(Hz)), Db 10*log10(P / 1 V^2), Dbm 10*log10(P / R / 1 mW).
// Logarithmic units are floored at -400 dB for zero power.
std::vector<double> formatSpectrum(const PowerSpectrum& spectrum, const SpectrumFormat& format);
// Same for one value (e.g. an estimated tone power). acBin = false for the DC and Nyquist bins.
double formatPower(double power, const SpectrumFormat& format, const SpectrumInfo& info, bool acBin = true);
QString spectrumUnitLabel(const SpectrumFormat& format);   // "Vrms", "Vpk", "dBV", "dBm", "Vrms/√Hz", "dBV/Hz"...

// ---------------------------------------------------------------------------------------------
// Peak search

struct PeakEstimate {
    size_t bin = 0;          // largest bin of the peak
    double frequency = 0;    // Hz, power-weighted centroid of the lines around the peak
    double power = 0;        // V^2 RMS: sum of the lines around the peak / enbw
};

// Largest bin with frequency in [minFrequency, maxFrequency] outside the DC lobe (bins <=
// lobeHalfWidth), refined with estimatePeak(bin, span). Returns power 0 if no bin qualifies.
PeakEstimate findPeak(const PowerSpectrum& spectrum, int span = 3, double minFrequency = 0,
                      double maxFrequency = std::numeric_limits<double>::infinity());
// Power and frequency of the tone around `bin`, using the lines bin-span..bin+span
// (the DSA's "Fundamental Frequency Power" uses span = 3).
PeakEstimate estimatePeak(const PowerSpectrum& spectrum, size_t bin, int span = 3);

// ---------------------------------------------------------------------------------------------
// Averaging (DSA "Averaging" controls)

enum class AveragingMode {
    None,       // latest record only
    Vector,     // average of the complex spectra (needs phase-stable, triggered records)
    Rms,        // average of the powers
    PeakHold,   // per-bin maximum of the powers until restart(); weighting and count ignored
};
enum class AveragingWeighting {
    Linear,       // equal-weight mean of the first N records, then held until restart()
    Exponential,  // running average: equal weights while filling, then weight 1/N for new records
};

class SpectrumAverager {
public:
    explicit SpectrumAverager(AveragingMode mode = AveragingMode::Rms,
                              AveragingWeighting weighting = AveragingWeighting::Exponential,
                              int averages = 5);

    void configure(AveragingMode mode, AveragingWeighting weighting, int averages);   // restarts
    AveragingMode mode() const { return m_mode; }
    AveragingWeighting weighting() const { return m_weighting; }
    int averages() const { return m_averages; }

    void restart();
    // Adds one record (restarts first if the bin count or df changed).
    void add(const ComplexSpectrum& spectrum);
    PowerSpectrum result() const;   // empty power vector before the first add()
    int count() const { return m_count; }   // records in the current average (at most N except PeakHold)
    bool isComplete() const;                // N records averaged (None: one record; PeakHold: never)

private:
    AveragingMode m_mode;
    AveragingWeighting m_weighting;
    int m_averages;
    SpectrumInfo m_info;
    std::vector<Complex> m_vector;   // Vector mode
    std::vector<double> m_power;     // other modes
    int m_count = 0;
};

}  // namespace mydaq::dsp
