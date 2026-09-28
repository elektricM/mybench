// Level history of one simulated digital line, with exact edge times for the counter.
#pragma once

#include <cstdint>
#include <deque>
#include <limits>

namespace mydaq::sim {

struct DigitalChange {
    double time;   // s (device time); +-infinity when there is no such change
    bool level;    // level after the change
};

// Piecewise description of a digital signal: constant levels and square waves. In a square
// segment the phase (in cycles) advances linearly, phase(t) = phase0 + rate * (t - begin), and the
// level is high while frac(phase) < duty. rate may be negative (an encoder turning backwards).
// Setting the signal from time t on discards whatever was scheduled after t.
class DigitalTimeline {
public:
    DigitalTimeline();   // low forever

    void setConstant(double t, bool level);
    void setSquare(double t, double phaseAtT, double rate, double duty);

    bool levelAt(double t) const;
    double phaseAt(double t) const;   // phase of the square segment covering t (0 for constant)
    DigitalChange nextChange(double t) const;       // first change strictly after t
    DigitalChange previousChange(double t) const;   // last change strictly before t
    DigitalChange nextEdge(double t, bool rising) const;       // first change to `rising` after t
    DigitalChange previousEdge(double t, bool rising) const;   // last change to `rising` before t
    int64_t countEdges(double t0, double t1, bool rising) const;   // changes to `rising` in (t0, t1]
    void prune(double t);   // forgets segments that ended before t

private:
    struct Segment {
        double begin = -std::numeric_limits<double>::infinity();
        double end = std::numeric_limits<double>::infinity();
        bool constant = true;
        bool level = false;          // constant segments
        double phase0 = 0.0;         // square segments
        double rate = 0.0;
        double duty = 0.5;
        bool entryLevel = false;     // level right after begin
        bool levelBefore = false;    // level of the previous segment right before begin

        bool moving() const { return !constant && rate != 0.0; }
        double phase(double t) const { return phase0 + rate * (t - begin); }
        double margin() const;       // phase distance that is safely inside one level
        bool levelAtPhase(double p) const;
        bool levelNear(double t, bool after) const;
        DigitalChange nextInside(double t) const;       // change in (max(t, begin), end), time +inf if none
        DigitalChange previousInside(double t, bool first) const;
        int64_t countInside(double a, double b, bool includeEnd, bool rising) const;
    };

    size_t segmentAt(double t) const;
    void append(Segment segment);

    std::deque<Segment> m_segments;
};

}  // namespace mydaq::sim
