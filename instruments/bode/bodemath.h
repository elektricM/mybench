// Bode Analyzer: pure functions shared by the sweep engine, the controller and the tests
// (docs/instruments/bode.md, ELVISmx behaviour in docs/instruments/algorithms/bode.md).
#pragma once

#include <complex>
#include <vector>

namespace mybench::instruments::bode {

// Limits and fixed parameters recovered from ELVISmx (algorithms/bode.md §1, §3, §4).
inline constexpr double kMinFrequency = 1.0;        // Hz, myDAQ clamp
inline constexpr double kMaxFrequency = 20000.0;    // Hz, myDAQ clamp
inline constexpr int kMinSteps = 1;
inline constexpr int kMaxSteps = 1000;              // practical cap (ELVISmx: any I32)
inline constexpr double kMaxAmplitude = 5.0;        // V peak
inline constexpr double kAoSamplesPerPeriod = 10000.0;   // requested AO rate = 10 000 · f
// MYBENCH: the AO clock stays at most 90 % of the maximum so it never equals the AI clock (which
// saturates at the maximum). AI 0 then samples the stimulus's zero-order-hold steps at many
// different offsets and measures the same fundamental that drives the circuit; with equal
// clocks it would see the steps at one fixed offset and read up to half an AO sample of phase
// error (9° at 10 kHz).
inline constexpr double kAoRateFraction = 0.9;
inline constexpr int kAiSamples = 16000;           // samples per channel per acquisition
inline constexpr double kAiRatePerHertz = 3200.0;   // fs = 3200 · f (5 periods in 16 000 samples)
inline constexpr double kMinAiRate = 100.0;         // S/s
inline constexpr int kRangeProbeSamples = 16000;   // range pre-measurement: 16 000 samples ...
inline constexpr double kRangeProbeMaxRate = 200000.0;  // ... at up to 200 kS/s (80 ms), ±10 V (observed)
inline constexpr double kRangeHeadroom = 2.2;       // range must hold 2.2 × amplitude
inline constexpr double kClipFraction = 0.98;       // a sample at 98 % of the range = clipped
inline constexpr int kMaxAveragesPerPoint = 16;

// Log frequency grid of ELVISmx (subFreqArrayGen): x = log10(start) + k / steps while
// x <= log10(stop) (with a 1e-9 tolerance), point = 10^x. The first point is exactly `start`;
// `stop` is included only when it falls on the grid. start > stop gives an empty list.
std::vector<double> sweepFrequencies(double start, double stop, int stepsPerDecade);

// AI sample rate requested for a point: 3200 · f clamped to [100 S/s, maxRate].
double acquisitionRate(double frequency, double maxRate);
// AI rate and duration of the range pre-measurement (observed ELVISmx: 16 000 samples at 200 kS/s).
double rangeProbeRate(double maxRate);
double rangeProbeSeconds(double maxRate);
// Highest AO rate wanted for a point: 10 000 samples per period, at most kAoRateFraction × maxRate.
double stimulusRateLimit(double frequency, double maxRate);
// Input range (±2 V or ±10 V) that holds kRangeHeadroom × amplitude (ELVISmx autoscale table
// restricted to the myDAQ ranges).
double selectRange(double amplitude);
// True when a sample reaches kClipFraction of ±range.
bool isClipped(const std::vector<double>& samples, double range);
// Largest |sample| (0 for an empty record).
double peakMagnitude(const std::vector<double>& samples);

// Degrees wrapped to (-180, 180].
double wrapDegrees(double degrees);
// Consecutive values with 360° jumps removed (the first value is kept).
std::vector<double> unwrapDegrees(const std::vector<double>& wrapped);

// Complex response from the stimulus / response tones (amplitude V peak, phase rad): the ratio
// of amplitudes and the phase difference, corrected for the inter-channel skew (the response,
// sampled `skew` seconds after the stimulus, reads 2π·f·skew too much phase).
std::complex<double> response(double stimulusAmplitude, double stimulusPhase, double responseAmplitude,
                              double responsePhase, double frequency, double skew);

struct PointValues {
    double gain = 0;     // |H|
    double gainDb = 0;   // 20·log10|H| (-inf for 0)
    double phase = 0;    // degrees, (-180, 180]
};
// Displayed values of a complex response; `inverted` = Op-Amp Signal Polarity Inverted (H → -H).
PointValues pointValues(std::complex<double> h, bool inverted);

// Frequencies where y crosses `level` between consecutive points, interpolated linearly in
// log10(frequency). Points must be sorted by frequency.
std::vector<double> levelCrossings(const std::vector<double>& frequency, const std::vector<double>& y,
                                   double level);
// y interpolated at `f` (linear in log10 f); NaN outside the data.
double interpolateLog(const std::vector<double>& frequency, const std::vector<double>& y, double f);

// MYBENCH markers (bode.md "Markers").
struct Markers {
    double maxGainDb = 0;                 // maximum gain of the sweep
    std::vector<double> cutoff;           // -3 dB crossings of (max gain - 3 dB)
    double unityGainFrequency = 0;        // first 0 dB crossing (NaN: none)
    double phaseMargin = 0;               // 180 + phase at the unity-gain frequency (deg)
    double phaseCrossoverFrequency = 0;   // first -180° crossing of the unwrapped phase (NaN: none)
    double gainMargin = 0;                // -gain (dB) at the phase crossover
};
// gainDb and phase (degrees, unwrapped internally) per point, frequencies sorted.
Markers computeMarkers(const std::vector<double>& frequency, const std::vector<double>& gainDb,
                       const std::vector<double>& phase);

// Rough duration of one point, for the remaining-time estimate: range probe (Standard) or
// settling (Fast) + the 16 000-sample acquisition + a fixed overhead.
double estimatedPointSeconds(double frequency, double maxAiRate, bool rangeProbe, int averages);
// Settling wait of the fast mode: 3 periods, at least 20 ms, at most 3 s.
double settlingSeconds(double frequency);

}  // namespace mybench::instruments::bode
