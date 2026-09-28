#include "mydaq/dsp/decimate.h"

#include <algorithm>

namespace mydaq::dsp {

MinMaxEnvelope minMaxDecimate(const std::vector<double>& x, size_t buckets, size_t begin, size_t end)
{
    MinMaxEnvelope env;
    end = std::min(end, x.size());
    env.end = std::max(begin, end);
    if (begin >= end || buckets == 0)
        return env;

    const size_t n = end - begin;
    const size_t count = std::min(buckets, n);
    env.minimum.resize(count);
    env.maximum.resize(count);
    env.first.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t first = begin + i * n / count;
        const size_t last = begin + (i + 1) * n / count;
        const auto [lo, hi] = std::minmax_element(x.begin() + ptrdiff_t(first), x.begin() + ptrdiff_t(last));
        env.first[i] = first;
        env.minimum[i] = *lo;
        env.maximum[i] = *hi;
    }
    return env;
}

}  // namespace mydaq::dsp
