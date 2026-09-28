// Screen-space polyline construction with min/max decimation.
//
// Sorted data (uniform or explicit x) is split into pixel buckets (columns of `bucketPx` pixels,
// computed through the axis mapping, so log axes decimate correctly). A bucket holding more than
// two samples contributes its minimum and maximum (so every peak stays visible and the envelope
// is exact), ordered so the connection from the previous bucket is the shorter one; sparse
// buckets contribute their samples unchanged. Bucket min/max come from TraceBuffer's min/max
// pyramid, so the cost is O(buckets · log n) at any zoom level, even for 10^8-sample recordings.
// Only the visible x range (plus one sample on each side, so lines reach the edges) is used.
//
// Unsorted data (XY mode) is emitted as a path, skipping points closer than half a pixel to the
// previously emitted one.
//
// The core library's mydaq::dsp::minMaxDecimate() splits by sample count; the plot needs buckets
// aligned to pixel columns (and to log10 columns), hence this separate implementation.
#pragma once

#include "mybench/plot/scale.h"
#include "mybench/plot/tracebuffer.h"

#include <vector>

namespace mybench::plot {

struct PixelPoint {
    float x;
    float y;   // NaN x/y marks a gap (the polyline restarts after it)
    bool isGap() const { return x != x || y != y; }
};

// display = value * scale + offset (per-trace transform: scope V/div and position).
struct TraceTransform {
    double xScale = 1.0;
    double xOffset = 0.0;
    double yScale = 1.0;
    double yOffset = 0.0;
};

struct PolylineInfo {
    bool decimated = false;       // min/max decimation was applied
    size_t samplesVisited = 0;    // samples covered by the polyline
    double yMin = qQNaN();        // display-unit range of the visited finite samples
    double yMax = qQNaN();
};

// Appends the polyline to `out` (cleared first). `step`: hold each value until the next sample
// (only when not decimated). `bucketPx`: bucket width in pixels (1 / devicePixelRatio for 2 points
// per device pixel column; 1.0 for 2 per logical pixel).
PolylineInfo buildPolyline(const TraceData &data, const ScaleMap &xMap, const ScaleMap &yMap,
                           const TraceTransform &transform, bool step, double bucketPx,
                           std::vector<PixelPoint> &out);

}  // namespace mybench::plot
