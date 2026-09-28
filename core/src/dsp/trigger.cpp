#include "mydaq/dsp/trigger.h"

#include <algorithm>

namespace mydaq::dsp {

namespace {

std::vector<double> scan(const std::vector<double>& x, const EdgeTrigger& trigger, size_t from, bool firstOnly)
{
    std::vector<double> crossings;
    const bool rising = trigger.slope == Slope::Rising;
    const double level = trigger.level;
    const double armLevel = rising ? level - std::max(0.0, trigger.hysteresis)
                                   : level + std::max(0.0, trigger.hysteresis);
    bool armed = false;
    for (size_t i = from; i < x.size(); ++i) {
        const double v = x[i];
        if (!armed) {
            armed = rising ? v < armLevel : v > armLevel;
            continue;
        }
        if (rising ? v >= level : v <= level) {
            // While armed every earlier sample stayed on the other side, so x[i-1] != v.
            const double previous = x[i - 1];
            crossings.push_back(double(i - 1) + (level - previous) / (v - previous));
            if (firstOnly)
                break;
            armed = false;
        }
    }
    return crossings;
}

}  // namespace

std::optional<double> findEdge(const std::vector<double>& x, const EdgeTrigger& trigger, size_t from)
{
    const std::vector<double> c = scan(x, trigger, from, true);
    if (c.empty())
        return std::nullopt;
    return c.front();
}

std::vector<double> findEdges(const std::vector<double>& x, const EdgeTrigger& trigger, size_t from)
{
    return scan(x, trigger, from, false);
}

}  // namespace mydaq::dsp
