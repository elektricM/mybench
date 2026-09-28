// Min/max envelope decimation for drawing long traces (every peak stays visible).
#pragma once

#include <cstddef>
#include <vector>

namespace mydaq::dsp {

struct MinMaxEnvelope {
    std::vector<double> minimum;   // per bucket
    std::vector<double> maximum;   // per bucket
    std::vector<size_t> first;     // index of each bucket's first sample; bucket i is [first[i], first[i+1])
    size_t end = 0;                // one past the last sample (end of the last bucket)
};

// Splits x[begin, end) into min(buckets, end - begin) buckets of (nearly) equal size and returns the
// minimum and maximum of each. With fewer samples than buckets, each sample is its own bucket.
MinMaxEnvelope minMaxDecimate(const std::vector<double>& x, size_t buckets, size_t begin = 0,
                              size_t end = static_cast<size_t>(-1));

}  // namespace mydaq::dsp
