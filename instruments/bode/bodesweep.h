// BodeSweeper: measures one frequency point after another on a mydaq::Device (worker thread).
//
// Per point (docs/instruments/algorithms/bode.md §3-§5, docs/instruments/bode.md §3.2):
//   1. Stimulus on AO 0: a regenerated buffer of whole periods of a sine (about 10 000 samples
//      per period, at most the AO maximum rate; the planned buffer gives the exact frequency).
//   2. Input ranges. Standard (ELVISmx): a 1.6 s pre-measurement at 10 kS/s on ±10 V picks each
//      channel's range (±2 V when 2.2 × amplitude fits) and doubles as the settling time.
//      Fast (MYBENCH): no pre-measurement; the stimulus range follows the peak amplitude, the
//      response range adapts from the previous point, and the circuit settles for 3 periods.
//   3. 16 000 samples per channel of AI 0 (stimulus) and AI 1 (response) at 3200·f, clamped to
//      [100 S/s, max]. A channel clipped on ±2 V is measured again on ±10 V; a response clipped on
//      ±10 V marks the point.
//   4. Sine fit at the known frequency on both channels; H = response / stimulus, phase corrected
//      for the AI channel skew. `averages` > 1 averages the complex response of several records.
// finish() stops the generation and leaves AO 0 at 0 V.
#pragma once

#include <mydaq/device.h>

#include <complex>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace mybench::instruments::bode {

struct SweepOptions {
    double amplitude = 2.0;        // V peak of the stimulus
    bool rangeProbe = true;        // Standard (ELVISmx) mode; false = Fast
    int averages = 1;              // records averaged per point (1..16)
};

struct MeasuredPoint {
    double frequency = 0;              // generated frequency (Hz)
    std::complex<double> response;     // H, non-inverted, skew-corrected
    bool clipped = false;              // response clipped on ±10 V
    double stimulusAmplitude = 0;      // V peak (AI 0)
    double responseAmplitude = 0;      // V peak (AI 1)
    double stimulusRange = 10;         // ±V used
    double responseRange = 10;
    double sampleRate = 0;             // AI rate of the measurement
};

class BodeSweeper
{
public:
    // `stopRequested` is polled between reads (every ≤ 100 ms).
    BodeSweeper(mydaq::Device& device, std::function<bool()> stopRequested);
    ~BodeSweeper();

    // Measures `frequency` (requested Hz). Returns nullopt when stopped. Throws
    // mydaq::DeviceError on device errors and when AI 0 sees no stimulus.
    std::optional<MeasuredPoint> measure(double frequency, const SweepOptions& options);
    // Destroys the tasks and writes 0 V to AO 0 (safe state). Called by the destructor too.
    void finish();

    // Message of the "no stimulus" error (bode.md §3.2).
    static QString noStimulusMessage();

private:
    struct Record {
        std::vector<double> stimulus;
        std::vector<double> response;
        double rate = 0;
        double skew = 0;
    };
    bool generate(double frequency, double amplitude, double* actualFrequency);
    std::optional<Record> acquire(double rate, int samples, double stimulusRange, double responseRange);

    mydaq::Device& m_device;
    std::function<bool()> m_stop;
    std::unique_ptr<mydaq::AoTask> m_ao;
    std::unique_ptr<mydaq::AiTask> m_ai;
    mydaq::AiConfig m_aiConfig;
    double m_responseRange = 10;   // Fast mode: range for the next point
    bool m_firstPoint = true;
    bool m_finished = false;
};

}  // namespace mybench::instruments::bode
