// Software edge trigger (Scope "Edge" trigger, crossing detection for measurements).
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace mydaq::dsp {

enum class Slope { Rising, Falling };

struct EdgeTrigger {
    double level = 0.0;       // V
    Slope slope = Slope::Rising;
    double hysteresis = 0.0;  // V (>= 0): the signal must first be below level - hysteresis (rising)
                              // or above level + hysteresis (falling) to arm the trigger
};

// First armed crossing of `level` at or after sample `from`, as a fractional sample index
// (linear interpolation between the two samples around the crossing). nullopt if none.
std::optional<double> findEdge(const std::vector<double>& x, const EdgeTrigger& trigger, size_t from = 0);
// Every armed crossing at or after `from` (the trigger re-arms after each one).
std::vector<double> findEdges(const std::vector<double>& x, const EdgeTrigger& trigger, size_t from = 0);

}  // namespace mydaq::dsp
