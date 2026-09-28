// Output history of one simulated AO channel, and an exact first-order filter driven by it.
#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <vector>

namespace mydaq::sim {

inline constexpr double kForever = std::numeric_limits<double>::infinity();

// Samples of one AO run. Streaming runs append while generating and drop samples that are no longer
// needed (indices keep counting from the start of the run).
class SampleStream {
public:
    SampleStream() = default;
    explicit SampleStream(std::vector<double> samples) : m_data(std::move(samples)) {}

    int64_t size() const { return m_base + int64_t(m_data.size()); }   // samples ever held
    double at(int64_t index) const;    // clamped to the retained range; size() must be > 0
    void append(const double* data, size_t count) { m_data.insert(m_data.end(), data, data + count); }
    void trimBefore(int64_t index);    // may forget samples with a smaller index

private:
    std::vector<double> m_data;
    int64_t m_base = 0;   // index of m_data[0]
};

// First-order low-pass y' = (u - y) / tau with zero-order-hold input, solved exactly per step.
class ZohLowPass {
public:
    explicit ZohLowPass(double cutoffHz) { setCutoff(cutoffHz); }
    void setCutoff(double cutoffHz);
    double cutoff() const { return m_cutoff; }
    double tau() const { return m_tau; }
    double time() const { return m_time; }
    double value() const { return m_value; }
    void reset(double time, double value);
    void holdUntil(double input, double time);   // input constant from time() to `time`

private:
    double m_cutoff = 1.0;
    double m_tau = 1.0;
    double m_time = -kForever;
    double m_value = 0.0;
};

// Piecewise description of an output over time: constant levels (static writes, held values after a
// task stops) and timed runs (sample j output during [begin + j/rate, begin + (j+1)/rate)).
class AoTimeline {
public:
    struct Level {
        double value = 0.0;
        bool driven = false;   // a timed run is generating, or the static level is not 0 V
    };

    AoTimeline();

    Level levelAt(double t) const;
    void setLevel(double t, double value);
    // A timed run from t. loop: the samples repeat (regeneration). trimmable: old samples may be
    // dropped by prune() (streaming).
    void play(double t, double rate, std::shared_ptr<SampleStream> samples, bool loop, bool trimmable);
    // Ends the current timed run at t; the output holds the sample it was generating.
    void stop(double t);
    // Advances the filter to time t, integrating this output exactly.
    void advance(ZohLowPass& filter, double t) const;
    // Forgets history before t (keeps what levelAt()/advance() need from t on).
    void prune(double t);

private:
    struct Segment {
        double begin = -kForever;
        double end = kForever;
        double level = 0.0;   // constant segments (rate == 0)
        double rate = 0.0;
        bool loop = false;
        bool trimmable = false;
        std::shared_ptr<SampleStream> samples;

        bool timed() const { return rate > 0.0; }
        int64_t indexAt(double t) const;
        double sample(int64_t index) const;   // honours loop / hold-last-sample
    };

    size_t segmentAt(double t) const;
    void append(Segment segment);

    std::deque<Segment> m_segments;
};

}  // namespace mydaq::sim
