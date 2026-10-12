// Spectrum processing of the Spectrum Analyzer (docs/instruments/dsa.md §3). Plain C++ on top of mydaq::dsp, used by the worker
// thread (SpectrumProcessor) and by the UI thread (peak search for the cursor actions). No Qt
// objects, no device access: everything here is unit-tested directly.
#pragma once

#include <mydaq/dsp/measure.h>
#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/window.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace mybench::instruments::dsa {

// ELVISmx constants: harmonics up to the 10th for THD, fundamental power over ±3 lines.
inline constexpr int kHighestHarmonic = 10;
inline constexpr int kFundamentalSpan = 3;

// The settings that restart the averaging when they change (window and Averaging group).
struct AveragingSettings {
    mydaq::dsp::WindowType window = mydaq::dsp::WindowType::None;
    mydaq::dsp::AveragingMode mode = mydaq::dsp::AveragingMode::Rms;
    mydaq::dsp::AveragingWeighting weighting = mydaq::dsp::AveragingWeighting::Exponential;
    int averages = 5;
    bool operator==(const AveragingSettings&) const = default;
};

// Number of records a Single (Run Once) acquires: 1 without averaging, # of Averages otherwise
// (RMS, Vector and Peak hold, either weighting).
int recordsForSingle(const AveragingSettings& settings);

struct Harmonic {
    int order = 1;          // 1 = fundamental
    double frequency = 0;   // Hz
    double power = 0;       // V² RMS, integrated over the window main lobe
};

struct SpectralPeak {
    size_t bin = 0;
    double frequency = 0;   // Hz (power-weighted centroid)
    double power = 0;       // V² RMS
};

// Measurements on the averaged power spectrum and the latest record (dsa.md §3.4, algorithms §3).
struct Measurements {
    bool valid = false;               // a fundamental was found (else every value is 0)
    double fundamentalFrequency = 0;  // Hz, "Detected Fundamental Frequency"
    double fundamentalPower = 0;      // V² RMS over ±3 lines ("Fundamental Frequency Power")
    size_t fundamentalBin = 0;
    double thdPercent = 0;            // 100 × sqrt(sum harmonics 2..10 / fundamental)
    double sinadDb = 0;               // 10 log10(total / (total - fundamental))
    double snrDb = 0;                 // 10 log10(fundamental / noise), harmonics 2..10 excluded
    double vpk = 0;                   // (max - min) / 2 of the record
    std::vector<Harmonic> harmonics;  // [0] = fundamental, then orders 2..10 below the last bin
};

// Never NaN/infinite: undefined values are 0 (dsa.md §3.4 "never NaN on screen").
Measurements measure(const mydaq::dsp::PowerSpectrum& spectrum, std::span<const double> record);

// ELVISmx "Vpk": the peak-to-peak amplitude divided by 2.
double halfPeakToPeak(std::span<const double> record);

// The record multiplied by the window scaled to unit coherent gain (the ELVISmx "Scaled Time
// Domain Window"): identical to the record for None.
std::vector<double> scaledTimeDomainWindow(std::span<const double> record, mydaq::dsp::WindowType window);

// The strongest spectral peaks (local maxima standing out by at least `minProminenceDb` from the
// surrounding spectrum and 10 dB above the median line, DC lobe excluded), strongest first.
std::vector<SpectralPeak> strongestPeaks(const mydaq::dsp::PowerSpectrum& spectrum, size_t maxCount,
                                         double minProminenceDb = 6.0);

// ---------------------------------------------------------------------------------------------
// Peak search on any sampled curve (cursor actions Peak / Next peak / Peak left / Peak right).
// `values` are in a unit where differences are meaningful for the prominence (dB for spectra,
// volts for records).

// Indices of the local maxima of values[first..] whose prominence (height above the higher of
// the two surrounding minima) is at least `minProminence` and whose value is at least `minValue`
// (e.g. a margin above a spectrum's noise floor).
std::vector<size_t> localMaxima(std::span<const double> values, double minProminence, size_t first = 0,
                                double minValue = -std::numeric_limits<double>::infinity());

enum class PeakSearch {
    Highest,     // the highest peak
    NextLower,   // the highest peak lower than the value at `from`
    Left,        // the nearest peak left of `from`
    Right,       // the nearest peak right of `from`
};
// Index of the peak found, or nullopt (the cursor stays where it is).
std::optional<size_t> searchPeak(std::span<const double> values, PeakSearch search, size_t from,
                                 double minProminence, size_t first = 0,
                                 double minValue = -std::numeric_limits<double>::infinity());

// Median of values[first..] (0 when empty): the noise floor of a spectrum in dB.
double medianValue(std::span<const double> values, size_t first = 0);

// Powers (V²) to dB for peak searches (zero power maps to -400 dB).
std::vector<double> powersToDb(std::span<const double> powers);

// ---------------------------------------------------------------------------------------------

// Worker-side processing of successive records: spectrum, averaging (dsp::SpectrumAverager),
// max/min hold of the averaged spectrum. Not thread-safe (one worker).
class SpectrumProcessor
{
public:
    explicit SpectrumProcessor(const AveragingSettings& settings = {});

    // New settings: restarts the averaging and the holds.
    void configure(const AveragingSettings& settings);
    const AveragingSettings& settings() const { return m_settings; }
    // Averaging "Restart": clears the average and the max/min holds.
    void restart();

    // Adds one record of samples taken at `sampleRate`.
    void add(std::span<const double> record, double sampleRate);

    // Records in the current average (Peak hold: since the restart).
    int count() const { return m_averager.count(); }
    // Records a Single needs (recordsForSingle()).
    int target() const { return recordsForSingle(m_settings); }
    bool hasEnoughForSingle() const { return count() >= target(); }
    // Linear weighting holds its completed average; Exponential keeps running.
    bool averageComplete() const { return m_averager.isComplete(); }

    mydaq::dsp::PowerSpectrum averaged() const { return m_averager.result(); }
    const std::vector<double>& maxHold() const { return m_maxHold; }   // V² per bin
    const std::vector<double>& minHold() const { return m_minHold; }
    const std::vector<double>& lastRecord() const { return m_record; }
    double sampleRate() const { return m_sampleRate; }

private:
    AveragingSettings m_settings;
    mydaq::dsp::SpectrumAverager m_averager;
    std::vector<double> m_maxHold;
    std::vector<double> m_minHold;
    std::vector<double> m_record;
    double m_sampleRate = 0;
};

}  // namespace mybench::instruments::dsa
