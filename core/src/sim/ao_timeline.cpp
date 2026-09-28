#include "ao_timeline.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mydaq::sim {

namespace {

constexpr int64_t kTrimChunk = 1 << 16;   // trim streaming buffers in chunks (amortized erase)
constexpr double kSettleTaus = 20.0;      // after this many time constants the old state is gone

}  // namespace

// ---------------------------------------------------------------------------------------------

double SampleStream::at(int64_t index) const
{
    const int64_t i = std::clamp(index - m_base, int64_t(0), int64_t(m_data.size()) - 1);
    return m_data[size_t(i)];
}

void SampleStream::trimBefore(int64_t index)
{
    const int64_t drop = std::min(index - m_base, int64_t(m_data.size()) - 1);
    if (drop < kTrimChunk)
        return;
    m_data.erase(m_data.begin(), m_data.begin() + drop);
    m_base += drop;
}

// ---------------------------------------------------------------------------------------------

void ZohLowPass::setCutoff(double cutoffHz)
{
    m_cutoff = cutoffHz;
    m_tau = 1.0 / (2.0 * std::numbers::pi * cutoffHz);
}

void ZohLowPass::reset(double time, double value)
{
    m_time = time;
    m_value = value;
}

void ZohLowPass::holdUntil(double input, double time)
{
    if (time <= m_time)
        return;
    m_value = input + (m_value - input) * std::exp(-(time - m_time) / m_tau);
    m_time = time;
}

// ---------------------------------------------------------------------------------------------

int64_t AoTimeline::Segment::indexAt(double t) const
{
    const double x = (t - begin) * rate;
    return x <= 0.0 ? 0 : int64_t(std::floor(x));
}

double AoTimeline::Segment::sample(int64_t index) const
{
    const int64_t n = samples->size();
    return samples->at(loop ? index % n : std::min(index, n - 1));
}

AoTimeline::AoTimeline() { m_segments.push_back(Segment{}); }

size_t AoTimeline::segmentAt(double t) const
{
    // Last segment starting at or before t (the first one covers everything earlier).
    const auto it = std::upper_bound(m_segments.begin(), m_segments.end(), t,
                                     [](double time, const Segment& s) { return time < s.begin; });
    return it == m_segments.begin() ? 0 : size_t(it - m_segments.begin()) - 1;
}

AoTimeline::Level AoTimeline::levelAt(double t) const
{
    const Segment& s = m_segments[segmentAt(t)];
    if (!s.timed())
        return {s.level, s.level != 0.0};
    const int64_t j = s.indexAt(t);
    const double value = s.sample(j);
    return {value, s.loop || j < s.samples->size() || value != 0.0};
}

void AoTimeline::append(Segment segment)
{
    Segment& last = m_segments.back();
    last.end = segment.begin;
    m_segments.push_back(std::move(segment));
}

void AoTimeline::setLevel(double t, double value)
{
    Segment s;
    s.begin = t;
    s.level = value;
    append(std::move(s));
}

void AoTimeline::play(double t, double rate, std::shared_ptr<SampleStream> samples, bool loop, bool trimmable)
{
    Segment s;
    s.begin = t;
    s.rate = rate;
    s.loop = loop;
    s.trimmable = trimmable;
    s.samples = std::move(samples);
    append(std::move(s));
}

void AoTimeline::stop(double t)
{
    const Segment& current = m_segments.back();
    if (!current.timed())
        return;
    setLevel(t, current.sample(current.indexAt(t)));
}

void AoTimeline::advance(ZohLowPass& filter, double t) const
{
    if (t <= filter.time())
        return;
    // Older state has decayed below double precision relevance: restart from the input level.
    const double settle = kSettleTaus * filter.tau();
    if (t - filter.time() > settle) {
        const double start = t - settle;
        filter.reset(start, levelAt(start).value);
    }
    for (size_t i = segmentAt(filter.time()); filter.time() < t; ++i) {
        const Segment& s = m_segments[i];
        const double end = std::min(s.end, t);
        if (!s.timed()) {
            filter.holdUntil(s.level, end);
            continue;
        }
        const int64_t n = s.samples->size();
        for (int64_t j = s.indexAt(filter.time()); filter.time() < end; ++j) {
            if (!s.loop && j >= n) {
                filter.holdUntil(s.sample(n - 1), end);
                break;
            }
            filter.holdUntil(s.sample(j), std::min(end, s.begin + double(j + 1) / s.rate));
        }
    }
}

void AoTimeline::prune(double t)
{
    while (m_segments.size() > 1 && m_segments[1].begin <= t)
        m_segments.pop_front();
    for (Segment& s : m_segments) {
        if (s.timed() && s.trimmable)
            s.samples->trimBefore(s.indexAt(t) - 1);
    }
}

}  // namespace mydaq::sim
