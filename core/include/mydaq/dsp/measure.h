// Waveform measurements: levels (Scope), timing, single-tone analysis (Bode, FGEN readback) and
// harmonic distortion (DSA). Functions return NaN when a measurement is undefined (empty input,
// no complete period...).
//
// Tone convention: x[n] ~= offset + amplitude * sin(2*pi*frequency*n/sampleRate + phase), with phase
// in radians in (-pi, pi] and n = 0 at the first sample.
#pragma once

#include "mydaq/dsp/fft.h"
#include "mydaq/dsp/spectrum.h"
#include "mydaq/dsp/window.h"

#include <vector>

namespace mydaq::dsp {

// ---------------------------------------------------------------------------------------------
// Levels

double mean(const std::vector<double>& x);
double rms(const std::vector<double>& x);          // includes DC
double acRms(const std::vector<double>& x);        // RMS of x - mean(x)
double minimum(const std::vector<double>& x);
double maximum(const std::vector<double>& x);
double peakToPeak(const std::vector<double>& x);   // Vp-p = max - min

// ---------------------------------------------------------------------------------------------
// Timing (Scope "Freq"): crossings of the mid-reference level (max + min) / 2 in the same direction,
// with hysteresis = hysteresisFraction * Vp-p (the signal must go that far past the mid level on the
// other side before the next crossing counts). Periods are averaged over all complete cycles of both
// directions.

double period(const std::vector<double>& x, double sampleRate, double hysteresisFraction = 0.1);   // s
double frequency(const std::vector<double>& x, double sampleRate, double hysteresisFraction = 0.1);  // Hz
// Fraction of the period spent above the mid level (0..1), averaged over complete cycles.
double dutyCycle(const std::vector<double>& x, double hysteresisFraction = 0.1);

// ---------------------------------------------------------------------------------------------
// Tones

struct Tone {
    double frequency = 0;   // Hz
    double amplitude = 0;   // V peak
    double phase = 0;       // rad
    double offset = 0;      // V
};

// Strongest non-DC tone: Hanning-interpolated FFT peak refined by an iterative 4-parameter sine fit
// (IEEE 1057). Frequency error is far below one bin (typically < 1e-6 relative for clean tones).
Tone extractSingleTone(const std::vector<double>& x, double sampleRate);
// 3-parameter least-squares sine fit at a known frequency: exact for any record length (no leakage
// from DC or the negative-frequency image). Preferred for Bode gain/phase.
Tone fitSine(const std::vector<double>& x, double sampleRate, double frequency);
// Windowed single-bin DFT at `frequency` (any frequency, not only bin centers). Offset = window-
// weighted mean.
Tone toneAt(const std::vector<double>& x, double sampleRate, double frequency,
            WindowType window = WindowType::Hanning);
// Raw single-bin DFT: sum_n x[n] * exp(-2*pi*i*cyclesPerSample*n).
Complex singleBinDft(const std::vector<double>& x, double cyclesPerSample);

// ---------------------------------------------------------------------------------------------
// Harmonic distortion (DSA THD / SINAD, THD+N, SNR). Component powers are integrated over the
// window's main lobe (+-lobeHalfWidth lines) and divided by the ENBW; the DC lobe is excluded.

struct HarmonicAnalysis {
    double fundamentalFrequency = 0;       // Hz
    double fundamentalPower = 0;           // V^2 RMS
    std::vector<double> harmonicFrequency; // [0] = fundamental, [k] = harmonic k+1 (Hz)
    std::vector<double> harmonicPower;     // same indexing, V^2 RMS
    double totalPower = 0;                 // everything except the DC lobe, V^2 RMS
    double noisePower = 0;                 // total minus fundamental and harmonics
    double thd = 0;                        // sqrt(sum harmonics k>=2 / fundamental), ratio (x100 for %)
    double thdPlusNoise = 0;               // sqrt((total - fundamental) / fundamental), ratio
    double sinad = 0;                      // dB: 10*log10(total / (total - fundamental))
    double snr = 0;                        // dB: 10*log10(fundamental / noise)
    // Level of harmonic `order` (2 = second harmonic) relative to the fundamental, dB.
    double harmonicLevelDb(int order) const;
};

// maxHarmonic: highest harmonic order included (0: every harmonic below Nyquist).
// The fundamental is the largest peak at or above minFrequency.
HarmonicAnalysis analyzeHarmonics(const PowerSpectrum& spectrum, int maxHarmonic = 0,
                                  double minFrequency = 0);
HarmonicAnalysis analyzeHarmonics(const std::vector<double>& x, double sampleRate,
                                  WindowType window = WindowType::BlackmanHarris7, int maxHarmonic = 0);

}  // namespace mydaq::dsp
