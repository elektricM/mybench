#include "digital_timeline.h"

#include <algorithm>
#include <cmath>

namespace mydaq::sim {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double fraction(double x) { return x - std::floor(x); }

}  // namespace

// ---------------------------------------------------------------------------------------------
// Segment. Thresholds of a square segment: integers (entering the high part) and k + duty (leaving
// it). With rate > 0 crossing an integer is a rising edge; with rate < 0 crossing k + duty is.

double DigitalTimeline::Segment::margin() const { return std::min({1e-4, 0.5 * duty, 0.5 * (1.0 - duty)}); }

bool DigitalTimeline::Segment::levelAtPhase(double p) const
{
    // Right-continuous in time: while the phase decreases the high part is (k, k + duty].
    const double f = fraction(p);
    return rate >= 0.0 ? f < duty : (f > 0.0 && f <= duty);
}

bool DigitalTimeline::Segment::levelNear(double t, bool after) const
{
    if (constant)
        return level;
    if (rate == 0.0)
        return levelAtPhase(phase0);
    const double direction = (after ? 1.0 : -1.0) * (rate > 0.0 ? 1.0 : -1.0);
    return levelAtPhase(phase(t) + direction * margin());
}

// Thresholds are tracked by integer index (rising edge at `up`, or `up + duty` when the phase
// decreases; falling at `down + duty`, or `down`) so stepping past one never stalls on rounding.
DigitalChange DigitalTimeline::Segment::nextInside(double t) const
{
    if (!moving())
        return {kInf, false};
    const double p = phase(std::max(t, begin));
    const bool forward = rate > 0.0;
    double up = forward ? std::floor(p) + 1.0 : std::ceil(p - duty) - 1.0;
    double down = forward ? std::floor(p - duty) + 1.0 : std::ceil(p) - 1.0;
    for (int i = 0; i < 8; ++i) {
        const double risingPhase = forward ? up : up + duty;
        const double fallingPhase = forward ? down + duty : down;
        const bool rising = forward ? risingPhase < fallingPhase : risingPhase > fallingPhase;
        const double time = begin + ((rising ? risingPhase : fallingPhase) - phase0) / rate;
        if (time > t)
            return time < end ? DigitalChange{time, rising} : DigitalChange{kInf, false};
        (rising ? up : down) += forward ? 1.0 : -1.0;   // rounding put it at or before t
    }
    return {kInf, false};
}

DigitalChange DigitalTimeline::Segment::previousInside(double t, bool first) const
{
    if (!moving())
        return {-kInf, false};
    const double until = std::min(t, end);
    const double p = phase(until);
    const bool forward = rate > 0.0;
    double up = forward ? std::ceil(p) - 1.0 : std::floor(p - duty) + 1.0;
    double down = forward ? std::ceil(p - duty) - 1.0 : std::floor(p) + 1.0;
    for (int i = 0; i < 8; ++i) {
        const double risingPhase = forward ? up : up + duty;
        const double fallingPhase = forward ? down + duty : down;
        const bool rising = forward ? risingPhase > fallingPhase : risingPhase < fallingPhase;
        const double time = begin + ((rising ? risingPhase : fallingPhase) - phase0) / rate;
        if (time < until)
            return (first || time > begin) ? DigitalChange{time, rising} : DigitalChange{-kInf, false};
        (rising ? up : down) += forward ? -1.0 : 1.0;
    }
    return {-kInf, false};
}

int64_t DigitalTimeline::Segment::countInside(double a, double b, bool includeEnd, bool rising) const
{
    const double pa = phase(a);
    const double pb = phase(b);
    // Thresholds of the requested kind are the integers shifted by `shift`.
    const bool integers = (rate > 0.0) == rising;
    const double shift = integers ? 0.0 : duty;
    const double lo = (rate > 0.0 ? pa : pb) - shift;
    const double hi = (rate > 0.0 ? pb : pa) - shift;
    double n = 0.0;
    if (rate > 0.0)   // thresholds in (pa, pb] or (pa, pb)
        n = includeEnd ? std::floor(hi) - std::floor(lo) : std::ceil(hi) - 1.0 - std::floor(lo);
    else              // phase decreasing: thresholds in [pb, pa) or (pb, pa)
        n = includeEnd ? std::ceil(hi) - std::ceil(lo) : std::ceil(hi) - std::floor(lo) - 1.0;
    return std::max<int64_t>(0, int64_t(n));
}

// ---------------------------------------------------------------------------------------------

DigitalTimeline::DigitalTimeline() { m_segments.push_back(Segment{}); }

size_t DigitalTimeline::segmentAt(double t) const
{
    const auto it = std::upper_bound(m_segments.begin(), m_segments.end(), t,
                                     [](double time, const Segment& s) { return time < s.begin; });
    return it == m_segments.begin() ? 0 : size_t(it - m_segments.begin()) - 1;
}

void DigitalTimeline::append(Segment segment)
{
    while (m_segments.size() > 1 && m_segments.back().begin > segment.begin)
        m_segments.pop_back();
    segment.entryLevel = segment.levelNear(segment.begin, true);
    if (m_segments.size() > 1 && m_segments.back().begin == segment.begin) {
        segment.levelBefore = m_segments.back().levelBefore;
        m_segments.pop_back();
    } else if (m_segments.size() == 1 && m_segments.back().begin >= segment.begin) {
        m_segments.back() = segment;
        m_segments.back().end = kInf;
        return;
    } else {
        segment.levelBefore = m_segments.back().levelNear(segment.begin, false);
    }
    m_segments.back().end = segment.begin;
    segment.end = kInf;
    m_segments.push_back(segment);
}

void DigitalTimeline::setConstant(double t, bool level)
{
    Segment s;
    s.begin = t;
    s.level = level;
    append(s);
}

void DigitalTimeline::setSquare(double t, double phaseAtT, double rate, double duty)
{
    if (!(duty > 0.0) || !(duty < 1.0)) {
        setConstant(t, duty >= 1.0);
        return;
    }
    Segment s;
    s.begin = t;
    s.constant = false;
    s.phase0 = phaseAtT;
    s.rate = rate;
    s.duty = duty;
    append(s);
}

bool DigitalTimeline::levelAt(double t) const
{
    const Segment& s = m_segments[segmentAt(t)];
    return s.constant ? s.level : s.levelAtPhase(s.phase(t));
}

double DigitalTimeline::phaseAt(double t) const
{
    const Segment& s = m_segments[segmentAt(t)];
    return s.constant ? 0.0 : s.phase(t);
}

DigitalChange DigitalTimeline::nextChange(double t) const
{
    for (size_t i = segmentAt(t);;) {
        const DigitalChange c = m_segments[i].nextInside(t);
        if (c.time < kInf)
            return c;
        if (++i == m_segments.size())
            return {kInf, false};
        const Segment& next = m_segments[i];
        if (next.begin > t && next.levelBefore != next.entryLevel)
            return {next.begin, next.entryLevel};
    }
}

DigitalChange DigitalTimeline::previousChange(double t) const
{
    double until = t;
    for (size_t i = segmentAt(t);; --i) {
        const Segment& s = m_segments[i];
        const DigitalChange c = s.previousInside(until, i == 0);
        if (c.time > -kInf)
            return c;
        if (i == 0)
            return {-kInf, false};
        if (s.begin < until && s.levelBefore != s.entryLevel)
            return {s.begin, s.entryLevel};
        until = std::min(until, s.begin);
    }
}

DigitalChange DigitalTimeline::nextEdge(double t, bool rising) const
{
    DigitalChange c = nextChange(t);   // changes alternate: at most two steps
    if (c.time < kInf && c.level != rising)
        c = nextChange(c.time);
    return c;
}

DigitalChange DigitalTimeline::previousEdge(double t, bool rising) const
{
    DigitalChange c = previousChange(t);
    if (c.time > -kInf && c.level != rising)
        c = previousChange(c.time);
    return c;
}

int64_t DigitalTimeline::countEdges(double t0, double t1, bool rising) const
{
    if (!(t1 > t0))
        return 0;
    int64_t n = 0;
    for (size_t i = segmentAt(t0); i < m_segments.size(); ++i) {
        const Segment& s = m_segments[i];
        if (s.begin > t1)
            break;
        if (i > 0 && s.begin > t0 && s.levelBefore != s.entryLevel && s.entryLevel == rising)
            ++n;
        const double a = std::max(t0, s.begin);
        const double b = std::min(t1, s.end);
        if (s.moving() && b > a)
            n += s.countInside(a, b, b < s.end, rising);
    }
    return n;
}

void DigitalTimeline::prune(double t)
{
    while (m_segments.size() > 1 && m_segments[1].begin <= t)
        m_segments.pop_front();
}

}  // namespace mydaq::sim
