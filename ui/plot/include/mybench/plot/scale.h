// Axis math shared by PlotAxis, Plot and the tests: value <-> pixel mapping (linear / log10),
// "nice" 1-2-5 tick generation, fixed divisions (scope graticule), decade ticks for log axes and
// engineering-notation tick labels ("10 k", "−20", "500 m").
#pragma once

#include <QString>

#include <cmath>
#include <vector>

namespace mybench::plot {

enum class ScaleType { Linear, Log };
enum class LabelStyle { Engineering, Fixed, Time };

// Maps data values to pixels along one axis. For Log, min/max must be > 0.
struct ScaleMap {
    ScaleType type = ScaleType::Linear;
    double min = 0.0;
    double max = 1.0;
    double pixelStart = 0.0;     // pixel of `min` (after inversion: of `max`)
    double pixelLength = 1.0;    // signed length: negative for a y axis growing upwards
    bool inverted = false;

    bool isLog() const { return type == ScaleType::Log; }
    // Normalized position 0..1 of a value (unclamped; log uses log10). NaN for log of <= 0.
    double normalized(double v) const
    {
        double t;
        if (isLog()) {
            if (!(v > 0))
                return qQNaN();
            t = (std::log10(v) - std::log10(min)) / (std::log10(max) - std::log10(min));
        } else {
            t = (v - min) / (max - min);
        }
        return inverted ? 1.0 - t : t;
    }
    double fromNormalized(double t) const
    {
        if (inverted)
            t = 1.0 - t;
        if (isLog()) {
            const double lmin = std::log10(min), lmax = std::log10(max);
            return std::pow(10.0, lmin + t * (lmax - lmin));
        }
        return min + t * (max - min);
    }
    double toPixel(double v) const { return pixelStart + normalized(v) * pixelLength; }
    double fromPixel(double px) const { return fromNormalized((px - pixelStart) / pixelLength); }
    bool isValid() const
    {
        return std::isfinite(min) && std::isfinite(max) && max > min && pixelLength != 0
            && (!isLog() || min > 0);
    }
};

struct TickSet {
    std::vector<double> major;   // labeled ticks, ascending
    std::vector<double> minor;   // unlabeled ticks, ascending (excludes majors)
    double step = 0.0;           // linear: major step; log: decades per major (1, 2, ...)
    int decimals = 0;            // linear labels: digits after the point (after prefix scaling)
    int exponent = 0;            // linear labels: common engineering exponent (multiple of 3)
    bool logDecades = false;     // log: majors are decades (labels from the decade)
};

// Largest "nice" number (1, 2, 5 × 10^k) <= raw (raw > 0).
double niceStepBelow(double raw);
// Smallest "nice" number (1, 2, 5 × 10^k) >= raw (raw > 0).
double niceStep(double raw);

// Major ticks with a nice step giving about `targetCount` intervals over [min, max]; minors split
// each major interval into `minorPerMajor` parts (-1: 5 for 1/5-steps, 4 for 2-steps).
TickSet linearTicks(double min, double max, double targetCount, int minorPerMajor = -1);
// Fixed divisions (scope graticule): divisions+1 majors at equal spacing, minorPerDivision parts.
TickSet divisionTicks(double min, double max, int divisions, int minorPerDivision = 5);
// Log10 ticks for [min, max] (> 0) over `pixelLength` pixels: decades (or every n-th decade when
// crowded) as majors, 2..9 × 10^k as minors, also labels 2 and 5 when there is room; ranges under
// about one decade fall back to nice linear steps.
TickSet logTicks(double min, double max, double pixelLength, double minMajorSpacingPx = 60);
// Time ticks for values in seconds since the epoch (1-2-5-10-15-30 s, minutes, hours, days).
TickSet timeTicks(double min, double max, double targetCount);

// Label of one tick: engineering prefix per tick for log scales, common exponent for linear
// ("0", "5 k", "10 k"; "−0.5", "0", "0.5"). Uses U+2212 for the minus sign. With unit,
// "5 kHz"; Fixed style uses plain decimals; Time style formats hh:mm:ss.
QString formatTick(double value, const TickSet &ticks, ScaleType scale, LabelStyle style,
                   const QString &unit = {}, int fixedDecimals = -1);

// Engineering notation for readouts: "1.235 ms", "−20.5 dB", "10 kHz". `digits` significant digits
// (trailing zeros kept, so values line up). NaN gives "—".
QString formatEngineering(double value, const QString &unit = {}, int digits = 4);

// Replaces '-' with the typographic minus U+2212.
QString typographicMinus(QString text);

}  // namespace mybench::plot
