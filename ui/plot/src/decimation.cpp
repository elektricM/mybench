#include "mybench/plot/decimation.h"

#include <algorithm>
#include <limits>

namespace mybench::plot {

namespace {

constexpr float kGap = std::numeric_limits<float>::quiet_NaN();
constexpr double kPixelClamp = 2e5;  // keep far-away points finite and float-precise

struct Builder {
    const TraceData &data;
    const ScaleMap &xMap;
    const ScaleMap &yMap;
    TraceTransform tf;
    std::vector<PixelPoint> &out;
    PolylineInfo &info;

    double mapX(double dataX) const { return xMap.toPixel(dataX * tf.xScale + tf.xOffset); }
    float mapY(double dataY) const
    {
        const double p = yMap.toPixel(dataY * tf.yScale + tf.yOffset);
        if (!std::isfinite(p))
            return kGap;
        return float(std::clamp(p, -kPixelClamp, kPixelClamp));
    }
    void track(double dataY)
    {
        if (!std::isfinite(dataY))
            return;
        const double d = dataY * tf.yScale + tf.yOffset;
        if (!(info.yMin <= d))
            info.yMin = d;
        if (!(info.yMax >= d))
            info.yMax = d;
    }
    void gap()
    {
        if (!out.empty() && !out.back().isGap())
            out.push_back({kGap, kGap});
    }
    void add(double px, double dataY)
    {
        track(dataY);
        const float py = mapY(dataY);
        if (!std::isfinite(px) || py != py) {
            gap();
            return;
        }
        out.push_back({float(std::clamp(px, -kPixelClamp, kPixelClamp)), py});
    }
    void point(size_t i) { add(mapX(data.xAt(i)), data.yAt(i)); }
};

}  // namespace

PolylineInfo buildPolyline(const TraceData &data, const ScaleMap &xMap, const ScaleMap &yMap,
                           const TraceTransform &transformIn, StepMode step, double bucketPx,
                           std::vector<PixelPoint> &out)
{
    out.clear();
    PolylineInfo info;
    const size_t n = data.size();
    if (n == 0 || !xMap.isValid() || !yMap.isValid())
        return info;
    TraceTransform tf = transformIn;
    if (!(tf.xScale > 0) || !std::isfinite(tf.xScale))
        tf.xScale = 1.0;
    if (!std::isfinite(tf.yScale))
        tf.yScale = 1.0;
    Builder b{data, xMap, yMap, tf, out, info};

    if (!data.sorted) {
        // XY path: drop points that stay within half a pixel of the last emitted one.
        info.samplesVisited = n;
        out.reserve(std::min<size_t>(n, 1 << 16));
        float lastX = 0, lastY = 0;
        bool haveLast = false;
        for (size_t i = 0; i < n; ++i) {
            const double yv = data.yAt(i);
            b.track(yv);
            const double pxd = b.mapX(data.xAt(i));
            const float py = b.mapY(yv);
            if (!std::isfinite(pxd) || py != py) {
                b.gap();
                haveLast = false;
                continue;
            }
            const float px = float(std::clamp(pxd, -kPixelClamp, kPixelClamp));
            if (haveLast && std::abs(px - lastX) < 0.5f && std::abs(py - lastY) < 0.5f && i + 1 < n)
                continue;
            out.push_back({px, py});
            lastX = px;
            lastY = py;
            haveLast = true;
        }
        return info;
    }

    const double dLo = (xMap.min - tf.xOffset) / tf.xScale;
    const double dHi = (xMap.max - tf.xOffset) / tf.xScale;
    size_t i0 = data.lowerBound(dLo);
    if (i0 > 0)
        --i0;
    size_t i1 = data.upperBound(dHi);
    if (i1 < n)
        ++i1;
    if (i1 <= i0)
        return info;
    const size_t count = i1 - i0;
    bucketPx = bucketPx > 0.05 ? bucketPx : 1.0;
    const size_t buckets = std::max<size_t>(1, size_t(std::ceil(std::abs(xMap.pixelLength) / bucketPx)));

    if (count <= 2 * buckets) {
        info.samplesVisited = count;
        const bool stepped = step != StepMode::None;
        const bool center = step == StepMode::CenterHorizontal || step == StepMode::CenterVertical;
        out.reserve(stepped ? count * (center ? 3 : 2) : count);
        for (size_t i = i0; i < i1; ++i) {
            if (stepped && !out.empty() && !out.back().isGap()) {
                const size_t before = out.size();
                const PixelPoint prev = out.back();
                b.point(i);
                if (out.size() > before && !out.back().isGap()) {
                    const PixelPoint cur = out.back();
                    out.pop_back();
                    switch (step) {
                    case StepMode::Hold:
                        out.push_back({cur.x, prev.y});
                        break;
                    case StepMode::Vertical:
                        out.push_back({prev.x, cur.y});
                        break;
                    case StepMode::CenterHorizontal: {
                        const float mx = 0.5f * (prev.x + cur.x);
                        out.push_back({mx, prev.y});
                        out.push_back({mx, cur.y});
                        break;
                    }
                    case StepMode::CenterVertical: {
                        const float my = 0.5f * (prev.y + cur.y);
                        out.push_back({prev.x, my});
                        out.push_back({cur.x, my});
                        break;
                    }
                    case StepMode::None:
                        break;
                    }
                    out.push_back(cur);
                }
            } else {
                b.point(i);
            }
        }
        return info;
    }

    // Decimate: per pixel bucket, min and max of its index range (pyramid range query, so the cost
    // is O(buckets * log n) whatever the number of samples in view).
    info.decimated = true;
    out.reserve(buckets * 2 + 8);
    const bool log = xMap.isLog();
    const double lmin = log ? std::log10(xMap.min) : xMap.min;
    const double lspan = log ? std::log10(xMap.max) - lmin : xMap.max - xMap.min;
    auto displayAt = [&](double u) { return log ? std::pow(10.0, lmin + u * lspan) : lmin + u * lspan; };
    auto dataAt = [&](double u) { return (displayAt(u) - tf.xOffset) / tf.xScale; };

    size_t i = i0;
    if (i < i1 && data.xAt(i) < dLo)  // the sample before the view
        b.point(i++);
    const size_t last = data.upperBound(dHi);
    for (size_t k = 0; k < buckets && i < last; ++k) {
        size_t e = k + 1 == buckets ? last : data.lowerBound(dataAt(double(k + 1) / double(buckets)));
        e = std::clamp(e, i, last);
        const size_t m = e - i;
        if (m == 0)
            continue;
        if (m <= 2) {
            for (size_t j = i; j < e; ++j)
                b.point(j);
        } else {
            double mn, mx;
            if (!data.rangeMinMax(i, e, &mn, &mx)) {
                b.gap();
            } else {
                const double px = xMap.toPixel(displayAt((double(k) + 0.5) / double(buckets)));
                // Order the pair so the connection from the previous column is the shorter one.
                bool maxFirst = false;
                if (!out.empty() && !out.back().isGap()) {
                    const float prevY = out.back().y;
                    maxFirst = std::abs(prevY - b.mapY(mx)) < std::abs(prevY - b.mapY(mn));
                }
                if (mn == mx) {
                    b.add(px, mn);
                } else if (maxFirst) {
                    b.add(px, mx);
                    b.add(px, mn);
                } else {
                    b.add(px, mn);
                    b.add(px, mx);
                }
            }
        }
        info.samplesVisited += m;
        i = e;
    }
    if (last < i1)  // the sample after the view
        b.point(last);
    return info;
}

}  // namespace mybench::plot
