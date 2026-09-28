// Waveform synthesis (function generator, ARB, simulator test signals) and periodic-buffer planning.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mydaq::dsp {

enum class Waveform { Sine, Square, Triangle, Sawtooth, Dc, Noise };

struct WaveformParams {
    Waveform type = Waveform::Sine;
    double frequency = 100.0;   // Hz
    double amplitude = 1.0;     // V peak (Vpp / 2); Noise: uniform in +-amplitude
    double offset = 0.0;        // V; the level of a Dc waveform
    double phase = 0.0;         // rad, at t = 0
    double dutyCycle = 0.5;     // Square: fraction of the period at +amplitude (0..1)
};

// Unit-amplitude shape at `cycles` (phase in periods, any real value). Every shape starts at phase 0
// like a sine (Sine/Triangle/Sawtooth: 0 and rising; Square: +1 for the first dutyCycle of the
// period). Sawtooth rises from -1 to +1 with its jump at half period. Dc and Noise return 0.
double waveformShape(Waveform type, double cycles, double dutyCycle = 0.5);

// n samples at t = startTime + k / sampleRate: offset + amplitude * shape(frequency*t + phase/2pi).
// Noise draws from a generator seeded with `seed` (same seed, same samples).
std::vector<double> generate(const WaveformParams& params, double sampleRate, size_t n,
                             double startTime = 0.0, uint64_t seed = 1);

// A regenerated AO buffer holding a whole number of periods.
struct BufferPlan {
    double sampleRate = 0;      // coerced device rate, S/s
    size_t length = 0;          // samples per channel
    size_t cycles = 0;          // periods in the buffer
    double frequency = 0;       // generated frequency = cycles * sampleRate / length
    double relativeError = 0;   // |frequency - requested| / requested
};

// Chooses a device rate (via coerceRate, never above maxRate) and a buffer of `cycles` whole periods
// so a regenerated buffer reproduces `frequency`. Prefers the highest rate, then the shortest buffer,
// whose relative frequency error is below `tolerance`; if none, returns the most accurate plan found.
// length is within [minLength, maxLength] (minLength is reached by repeating periods). A frequency
// <= 0 gives a DC plan (cycles = 0) of minLength samples at the highest rate.
BufferPlan planPeriodicBuffer(double frequency, double maxRate,
                              const std::function<double(double)>& coerceRate,
                              size_t maxLength = 100000, size_t minLength = 2, double tolerance = 1e-6);

// One buffer of `plan`: sample k = offset + amplitude * shape(cycles*k/length + phase/2pi), computed
// with exact integer phase so the buffer loops seamlessly. Noise ignores the plan's periodicity.
std::vector<double> generatePeriodic(const WaveformParams& params, const BufferPlan& plan, uint64_t seed = 1);

}  // namespace mydaq::dsp
