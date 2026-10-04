#include "scopecore.h"

#include <mydaq/dsp/measure.h>
#include <mydaq/dsp/spectrum.h>

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <numbers>
#include <numeric>

using namespace Qt::StringLiterals;

namespace mybench::instruments::scope {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ScopeController", text);
}

// Round half to even (LabVIEW's float -> integer conversion).
int64_t roundEven(double value)
{
    return int64_t(std::nearbyint(value));   // default rounding mode: to nearest, ties to even
}

// Linear-interpolated crossing times (fractional sample indices) of `level`, with hysteresis.
struct Crossings {
    std::vector<double> rising;
    std::vector<double> falling;
};

Crossings crossingsOf(const std::vector<double>& x, double level, double hysteresis)
{
    Crossings c;
    // State: +1 above level + h, -1 below level - h, 0 unknown.
    int state = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double v = x[i];
        if (state <= 0 && v >= level + hysteresis) {
            if (state < 0) {
                // Find the sample pair crossing `level` going up, searching backwards.
                size_t j = i;
                while (j > 0 && x[j - 1] >= level)
                    --j;
                if (j > 0) {
                    const double a = x[j - 1], b = x[j];
                    c.rising.push_back(double(j - 1) + (b != a ? (level - a) / (b - a) : 0.0));
                }
            }
            state = 1;
        } else if (state >= 0 && v <= level - hysteresis) {
            if (state > 0) {
                size_t j = i;
                while (j > 0 && x[j - 1] <= level)
                    --j;
                if (j > 0) {
                    const double a = x[j - 1], b = x[j];
                    c.falling.push_back(double(j - 1) + (b != a ? (a - level) / (a - b) : 0.0));
                }
            }
            state = -1;
        }
    }
    return c;
}

// Crossing time of `level` between samples i-1 and i.
double interpolateCrossing(const std::vector<double>& x, size_t i, double level)
{
    const double a = x[i - 1], b = x[i];
    return double(i - 1) + (b != a ? (level - a) / (b - a) : 0.0);
}

// Histogram state levels: the most populated bin of the lower and of the upper half.
void stateLevels(const std::vector<double>& x, double mn, double mx, double* low, double* high)
{
    constexpr int kBins = 256;
    const double span = mx - mn;
    if (!(span > 0)) {
        *low = mn;
        *high = mx;
        return;
    }
    std::array<int, kBins> histogram{};
    for (double v : x)
        ++histogram[size_t(std::clamp(int((v - mn) / span * kBins), 0, kBins - 1))];
    const auto lowBin = std::max_element(histogram.begin(), histogram.begin() + kBins / 2) - histogram.begin();
    const auto highBin = std::max_element(histogram.begin() + kBins / 2, histogram.end()) - histogram.begin();
    *low = mn + (double(lowBin) + 0.5) * span / kBins;
    *high = mn + (double(highBin) + 0.5) * span / kBins;
}

// Average 10 % -> 90 % (rising) or 90 % -> 10 % (falling) transition time, in samples.
double transitionTime(const std::vector<double>& x, double low, double high, bool rising)
{
    const double lo = low + 0.1 * (high - low);
    const double hi = low + 0.9 * (high - low);
    double sum = 0;
    int count = 0;
    double start = kNaN;
    for (size_t i = 1; i < x.size(); ++i) {
        const double a = x[i - 1], b = x[i];
        if (rising) {
            if (a < lo && b >= lo)
                start = interpolateCrossing(x, i, lo);
            else if (b < lo)
                start = kNaN;
            if (!std::isnan(start) && a < hi && b >= hi) {
                sum += interpolateCrossing(x, i, hi) - start;
                ++count;
                start = kNaN;
            }
        } else {
            if (a > hi && b <= hi)
                start = interpolateCrossing(x, i, hi);
            else if (b > hi)
                start = kNaN;
            if (!std::isnan(start) && a > lo && b <= lo) {
                sum += interpolateCrossing(x, i, lo) - start;
                ++count;
                start = kNaN;
            }
        }
    }
    return count > 0 ? sum / count : kNaN;
}

}  // namespace

// ---------------------------------------------------------------------------------------------

bool isAudio(Source source)
{
    return source == Source::AudioLeft || source == Source::AudioRight;
}

mydaq::AiChannel aiChannel(Source source)
{
    switch (source) {
    case Source::AI0: return mydaq::AiChannel::AI0;
    case Source::AI1: return mydaq::AiChannel::AI1;
    case Source::AudioLeft: return mydaq::AiChannel::AudioInLeft;
    case Source::AudioRight: return mydaq::AiChannel::AudioInRight;
    }
    return mydaq::AiChannel::AI0;
}

QString sourceName(Source source)
{
    return mydaq::toString(aiChannel(source));
}

const std::vector<double>& timePerDivValues()
{
    static const std::vector<double> values{5e-6, 10e-6, 20e-6, 50e-6, 100e-6, 200e-6, 500e-6, 1e-3,
                                            2e-3, 5e-3, 10e-3, 20e-3, 50e-3, 100e-3, 200e-3};
    return values;
}

const std::vector<double>& voltsPerDivValues(bool audio)
{
    static const std::vector<double> ai{0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0};
    static const std::vector<double> audioValues{0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0};
    return audio ? audioValues : ai;
}

QString divisionLabel(double value, const QString& unit)
{
    static const std::array<std::pair<double, const char16_t*>, 3> prefixes{
        {{1.0, u""}, {1e-3, u"m"}, {1e-6, u"µ"}}};
    for (const auto& [scale, prefix] : prefixes) {
        if (value >= scale * 0.999)
            return QString::number(std::round(value / scale * 1000) / 1000, 'g', 4) + u' '
                   + QString::fromUtf16(prefix) + unit;
    }
    return QString::number(value * 1e9, 'g', 4) + u" n"_s + unit;
}

int nearestIndex(const std::vector<double>& ascending, double value)
{
    if (ascending.empty() || !(value > 0))
        return 0;
    int best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < ascending.size(); ++i) {
        const double d = std::abs(std::log(ascending[i] / value));
        if (d < bestDistance) {
            bestDistance = d;
            best = int(i);
        }
    }
    return best;
}

RatePlan planRate(double timePerDiv, double maxRate, SamplingMode mode)
{
    const double screen = 10.0 * timePerDiv;
    RatePlan plan;
    if (mode == SamplingMode::HighResolution) {
        plan.rate = std::min(maxRate, 100000.0 / screen);
        plan.samples = int64_t(std::llround(plan.rate * screen)) + 1;
        return plan;
    }
    int64_t samples = 0;
    if (maxRate * screen <= 2500.0 + 1e-6) {
        plan.rate = maxRate;
        samples = std::llround(maxRate * screen);
    } else {
        plan.rate = 2500.0 / screen;
        if (plan.rate >= 1500.0) {
            samples = 2500;
        } else {
            plan.rate = 2000.0;
            samples = std::llround(2000.0 * screen);
        }
    }
    plan.samples = std::max<int64_t>(samples, 1) + 1;
    return plan;
}

double inputRangeFor(Source source, double voltsPerDiv, InputRange mode)
{
    if (isAudio(source))
        return 2.0;
    switch (mode) {
    case InputRange::Range10V: return 10.0;
    case InputRange::Range2V: return 2.0;
    case InputRange::Auto: break;
    }
    return voltsPerDiv >= 0.5 - 1e-9 ? 10.0 : 2.0;
}

// ---------------------------------------------------------------------------------------------

int64_t pretriggerSamples(int64_t recordLength, double horizontalPercent)
{
    const double h = std::clamp(horizontalPercent, 0.0, 100.0);
    return std::clamp<int64_t>(roundEven(double(recordLength) * h * 0.01), 0, recordLength);
}

std::optional<int64_t> findTrigger(const std::vector<double>& x, int64_t recordLength, int64_t pre,
                                   const TriggerSettings& trigger)
{
    const int64_t size = int64_t(x.size());
    const double level = trigger.level;
    const double h = std::max(0.0, trigger.hysteresis);
    const bool rising = trigger.slope == Slope::Rising;
    bool armed = h <= 0;
    double previous = level;
    for (int64_t j = 0; j < recordLength && pre + j < size; ++j) {
        const double sample = x[size_t(pre + j)];
        if (rising) {
            if (armed && previous < level && level <= sample)
                return j;
            if (sample < level - h)
                armed = true;
        } else {
            if (armed && previous > level && sample <= level)
                return j;
            if (sample > level + h)
                armed = true;
        }
        previous = sample;
    }
    return std::nullopt;
}

double triggerSubSample(const std::vector<double>& x, int64_t index, double level)
{
    if (index < 1 || index >= int64_t(x.size()))
        return 0.0;
    const double previous = x[size_t(index - 1)];
    const double current = x[size_t(index)];
    if (previous == current)
        return 0.0;
    const double f = std::clamp((level - previous) / (current - previous), 0.0, 1.0);
    return 1.0 - f;
}

// ---------------------------------------------------------------------------------------------

Measurements measure(const std::vector<double>& x, double sampleRate, double voltsPerDiv)
{
    Measurements m;
    if (x.empty() || !(sampleRate > 0))
        return m;
    double sum = 0, sum2 = 0;
    double mn = x.front(), mx = x.front();
    for (double v : x) {
        sum += v;
        sum2 += v * v;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    const double n = double(x.size());
    m.mean = sum / n;
    m.rms = std::sqrt(sum2 / n);
    m.minimum = mn;
    m.maximum = mx;
    m.peakToPeak = mx - mn;
    m.peak = std::max(std::abs(mx), std::abs(mn));
    m.acRms = std::sqrt(std::max(0.0, sum2 / n - m.mean * m.mean));
    if (m.rms > 0)
        m.crestFactor = std::max(std::abs(mx), std::abs(mn)) / m.rms;

    const mydaq::dsp::Tone tone = mydaq::dsp::extractSingleTone(x, sampleRate);
    if (std::isfinite(tone.frequency) && tone.amplitude > 0.2 * voltsPerDiv)
        m.frequency = tone.frequency;

    stateLevels(x, mn, mx, &m.low, &m.high);
    m.amplitude = m.high - m.low;

    // Timing from mid-level crossings with 10 % hysteresis (counter style).
    const double mid = 0.5 * (mx + mn);
    const Crossings c = crossingsOf(x, mid, 0.1 * (mx - mn));
    double span = 0;
    size_t intervals = 0;
    for (const auto* edges : {&c.rising, &c.falling}) {
        if (edges->size() >= 2) {
            span += edges->back() - edges->front();
            intervals += edges->size() - 1;
        }
    }
    if (intervals > 0 && mx > mn) {
        m.period = span / double(intervals) / sampleRate;
        m.counterFrequency = 1.0 / m.period;
    }
    double high = 0, low = 0;
    int highCount = 0, lowCount = 0;
    size_t f = 0;
    for (double r : c.rising) {
        while (f < c.falling.size() && c.falling[f] <= r)
            ++f;
        if (f < c.falling.size()) {
            high += c.falling[f] - r;
            ++highCount;
        }
    }
    size_t rr = 0;
    for (double fall : c.falling) {
        while (rr < c.rising.size() && c.rising[rr] <= fall)
            ++rr;
        if (rr < c.rising.size()) {
            low += c.rising[rr] - fall;
            ++lowCount;
        }
    }
    if (highCount > 0)
        m.positiveWidth = high / highCount / sampleRate;
    if (lowCount > 0)
        m.negativeWidth = low / lowCount / sampleRate;
    if (std::isfinite(m.period) && std::isfinite(m.positiveWidth))
        m.dutyCycle = std::clamp(m.positiveWidth / m.period, 0.0, 1.0);
    if (m.high > m.low) {
        m.riseTime = transitionTime(x, m.low, m.high, true) / sampleRate;
        m.fallTime = transitionTime(x, m.low, m.high, false) / sampleRate;
        const double amplitude = m.high - m.low;
        m.overshoot = std::max(0.0, mx - m.high) / amplitude * 100.0;
        m.preshoot = std::max(0.0, m.low - mn) / amplitude * 100.0;
        if (m.riseTime > 0)
            m.slewRate = 0.8 * amplitude / m.riseTime;   // mean slope between 10 % and 90 %
    }
    return m;
}

double phaseBetween(const std::vector<double>& a, const std::vector<double>& b, double sampleRate,
                    double frequency, double delay)
{
    const size_t n = std::min(a.size(), b.size());
    if (n < 4 || !(sampleRate > 0) || !(frequency > 0) || !std::isfinite(frequency))
        return kNaN;
    const double samplesPerPeriod = sampleRate / frequency;
    const size_t whole = size_t(std::floor(double(n) / samplesPerPeriod)) ;
    if (whole < 1)
        return kNaN;
    const size_t length = std::min(n, size_t(std::llround(double(whole) * samplesPerPeriod)));
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    double ar = 0, ai = 0, br = 0, bi = 0;
    for (size_t i = 0; i < length; ++i) {
        const double c = std::cos(w * double(i)), s = -std::sin(w * double(i));
        ar += a[i] * c;
        ai += a[i] * s;
        br += b[i] * c;
        bi += b[i] * s;
    }
    if (std::hypot(ar, ai) < 1e-9 || std::hypot(br, bi) < 1e-9)
        return kNaN;
    // arg(B / A): positive when b is ahead of a. b's samples were taken `delay` later than a's.
    double degrees = (std::atan2(bi, br) - std::atan2(ai, ar)) * 180.0 / std::numbers::pi;
    degrees -= 360.0 * frequency * delay;
    degrees = std::remainder(degrees, 360.0);
    return degrees == -180.0 ? 180.0 : degrees;
}

QString formatMeasurement(double value, const QString& unit)
{
    return formatFixed(value, unit, std::abs(value) >= 1.0 ? 3 : 2);
}

QString formatFixed(double value, const QString& unit, int decimals)
{
    if (!std::isfinite(value))
        return u"?"_s;
    static const std::array<const char16_t*, 9> prefixes{u"f", u"p", u"n", u"µ", u"m", u"", u"k", u"M", u"G"};
    int exponent = 0;
    if (value != 0.0)
        exponent = std::clamp(int(std::floor(std::log10(std::abs(value)) / 3.0)) * 3, -15, 9);
    double mantissa = value / std::pow(10.0, exponent);
    const double scale = std::pow(10.0, decimals);
    mantissa = std::round(mantissa * scale) / scale;
    if (std::abs(mantissa) >= 1000.0 && exponent < 9) {
        exponent += 3;
        mantissa /= 1000.0;
    }
    if (mantissa == 0.0)
        mantissa = 0.0;   // no "-0.00"
    const QString prefix = QString::fromUtf16(prefixes[size_t((exponent + 15) / 3)]);
    // ASCII minus, as ELVISmx prints it (exact text and scripts match).
    const QString text = QString::number(mantissa, 'f', decimals);
    return text + u' ' + prefix + unit;
}

// ---------------------------------------------------------------------------------------------

int thresholdIndex(const std::vector<double>& ascending, double value)
{
    const size_t n = ascending.size();
    if (n == 0 || std::isnan(value))
        return 0;
    if (value <= ascending.front())
        return 0;
    if (value >= ascending.back())
        return int(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        const double a = ascending[i], b = ascending[i + 1];
        if (a <= value && value <= b) {
            const double fraction = b > a ? (value - a) / (b - a) : 0.0;
            return int(std::clamp<int64_t>(roundEven(double(i) + fraction), 0, int64_t(n - 1)));
        }
    }
    return int(n - 1);
}

AutoscaleProbe analyseProbe(const std::vector<double>& block, double sampleRate)
{
    AutoscaleProbe p;
    if (block.empty())
        return p;
    const auto [mn, mx] = std::minmax_element(block.begin(), block.end());
    p.minimum = *mn;
    p.maximum = *mx;
    const mydaq::dsp::Tone tone = mydaq::dsp::extractSingleTone(block, sampleRate);
    p.toneFrequency = tone.frequency;
    p.toneAmplitude = tone.amplitude;
    return p;
}

AutoscaleChannel autoscaleChannel(const AutoscaleProbe& probe, bool audio)
{
    static const std::vector<double> aiRanges{0.05, 0.1, 0.25, 0.5, 1, 1, 2, 5, 10};
    static const std::vector<double> audioRanges{0.05, 0.1, 0.25, 0.5, 1, 1, 2};
    const std::vector<double>& ranges = audio ? audioRanges : aiRanges;
    AutoscaleChannel result;
    if (std::isnan(probe.maximum) || std::isnan(probe.minimum))
        return result;
    const double amplitude = (probe.maximum - probe.minimum) / 2.0 * 2.2;
    result.signalRange = ranges[size_t(thresholdIndex(ranges, amplitude))];
    result.midLevel = (probe.maximum + probe.minimum) / 2.0;
    // Volts/Div nearest to 3/10 of A (observed in ELVISmx: a ±10 V ramp gives 5 V/div, a ±7.5 V
    // one 5 V/div).
    const std::vector<double>& scales = voltsPerDivValues(audio);
    result.voltsPerDiv = scales[size_t(thresholdIndex(scales, amplitude * 3.0 / 10.0))];
    result.tonePresent = std::isfinite(probe.toneFrequency) && probe.toneFrequency > 0
                         && probe.toneAmplitude > 0.2 * result.signalRange;
    if (result.tonePresent)
        result.frequency = probe.toneFrequency;
    return result;
}

double autoscaleTimePerDiv(double lowestFrequency)
{
    if (!(lowestFrequency > 0) || !std::isfinite(lowestFrequency))
        return kNaN;
    const std::vector<double>& list = timePerDivValues();
    return list[size_t(thresholdIndex(list, 0.5 / lowestFrequency))];
}

const std::vector<double>& autoscaleRates()
{
    static const std::vector<double> rates{50000.0, 5000.0};
    return rates;
}

// ---------------------------------------------------------------------------------------------

bool isFft(MathFunction f)
{
    return f == MathFunction::FftA || f == MathFunction::FftB;
}

bool needsBothChannels(MathFunction f)
{
    return f == MathFunction::Sum || f == MathFunction::Difference || f == MathFunction::Product;
}

QString mathName(MathFunction f)
{
    switch (f) {
    case MathFunction::Sum: return u"CH0 + CH1"_s;
    case MathFunction::Difference: return u"CH0 − CH1"_s;
    case MathFunction::Product: return u"CH0 × CH1"_s;
    case MathFunction::InvertA: return u"−CH0"_s;
    case MathFunction::InvertB: return u"−CH1"_s;
    case MathFunction::FftA: return u"FFT CH0"_s;
    case MathFunction::FftB: return u"FFT CH1"_s;
    }
    return {};
}

std::vector<double> computeMath(MathFunction f, const std::vector<double>& a, const std::vector<double>& b)
{
    std::vector<double> out;
    switch (f) {
    case MathFunction::Sum:
    case MathFunction::Difference:
    case MathFunction::Product: {
        const size_t n = std::min(a.size(), b.size());
        out.resize(n);
        for (size_t i = 0; i < n; ++i)
            out[i] = f == MathFunction::Sum ? a[i] + b[i] : f == MathFunction::Difference ? a[i] - b[i] : a[i] * b[i];
        break;
    }
    case MathFunction::InvertA:
    case MathFunction::InvertB: {
        const std::vector<double>& x = f == MathFunction::InvertA ? a : b;
        out.resize(x.size());
        std::transform(x.begin(), x.end(), out.begin(), [](double v) { return -v; });
        break;
    }
    case MathFunction::FftA:
    case MathFunction::FftB:
        break;
    }
    return out;
}

FftResult fftAnalyse(const std::vector<double>& x, double sampleRate)
{
    FftResult r;
    if (x.size() < 4 || !(sampleRate > 0))
        return r;
    const mydaq::dsp::PowerSpectrum spectrum = mydaq::dsp::powerSpectrum(x, sampleRate, mydaq::dsp::WindowType::Hanning);
    mydaq::dsp::SpectrumFormat format;
    format.mode = mydaq::dsp::SpectrumMode::Rms;
    format.unit = mydaq::dsp::SpectrumUnit::Db;
    r.db = mydaq::dsp::formatSpectrum(spectrum, format);
    r.binWidth = spectrum.info.df;
    const mydaq::dsp::PeakEstimate peak = mydaq::dsp::findPeak(spectrum);
    if (peak.power > 0) {
        r.peakFrequency = peak.frequency;
        r.peakLevel = 10.0 * std::log10(peak.power);
    }
    return r;
}

// ---------------------------------------------------------------------------------------------

bool Snapshot::sameDisplay(const Snapshot& other) const
{
    for (int ch = 0; ch < 2; ++ch)
        if (channel[ch].source != other.channel[ch].source || channel[ch].enabled != other.channel[ch].enabled
            || channel[ch].voltsPerDiv != other.channel[ch].voltsPerDiv)
            return false;
    return timePerDiv == other.timePerDiv && sampling == other.sampling && roll == other.roll
           && triggerType == other.triggerType && triggerSource == other.triggerSource && slope == other.slope
           && level == other.level && horizontalPercent == other.horizontalPercent;
}

QString validate(const Snapshot& s)
{
    const bool e0 = s.channel[0].enabled, e1 = s.channel[1].enabled;
    if (!e0 && !e1)
        return tr("Enable at least one channel.");
    if (e0 && e1 && isAudio(s.channel[0].source) != isAudio(s.channel[1].source))
        return tr("AI channels cannot be used with AudioInput channels. Select the same kind of source for both channels.");
    return {};
}

AcquisitionPlan planAcquisition(const Snapshot& s, const mydaq::Device& device)
{
    AcquisitionPlan plan;
    std::vector<Source> sources;
    for (int ch = 0; ch < 2; ++ch) {
        const ChannelSettings& c = s.channel[ch];
        if (!c.enabled)
            continue;
        const double range = inputRangeFor(c.source, c.voltsPerDiv, c.range);
        auto it = std::find(sources.begin(), sources.end(), c.source);
        if (it == sources.end()) {
            sources.push_back(c.source);
            plan.config.channels.push_back({aiChannel(c.source), range});
            plan.column[ch] = int(sources.size()) - 1;
        } else {
            plan.column[ch] = int(it - sources.begin());
            auto& config = plan.config.channels[size_t(plan.column[ch])];
            config.range = std::max(config.range, range);   // same source shown twice
        }
    }
    for (int ch = 0; ch < 2; ++ch)
        if (plan.column[ch] >= 0)
            plan.range[ch] = plan.config.channels[size_t(plan.column[ch])].range;

    const RatePlan rate = planRate(s.timePerDiv, device.maxAiRate(), s.sampling);
    plan.recordLength = rate.samples;
    plan.config.sampleRate = device.coerceAiRate(rate.rate);
    plan.config.mode = mydaq::SampleMode::Finite;
    plan.edge = s.triggerType == TriggerType::Edge && !s.roll;
    plan.config.samplesPerChannel = plan.edge ? 2 * rate.samples : rate.samples;
    int trigger = s.triggerSource == TriggerSource::Chan1 ? 1 : 0;
    if (plan.column[trigger] < 0)
        trigger = 1 - trigger;
    plan.triggerChannel = trigger;
    return plan;
}

bool sameTask(const AcquisitionPlan& a, const AcquisitionPlan& b)
{
    if (a.config.channels.size() != b.config.channels.size() || a.config.sampleRate != b.config.sampleRate
        || a.config.samplesPerChannel != b.config.samplesPerChannel || a.config.mode != b.config.mode)
        return false;
    for (size_t i = 0; i < a.config.channels.size(); ++i)
        if (a.config.channels[i].channel != b.config.channels[i].channel || a.config.channels[i].range != b.config.channels[i].range)
            return false;
    return true;
}

void removeMean(std::vector<double>& x)
{
    if (x.empty())
        return;
    const double m = std::accumulate(x.begin(), x.end(), 0.0) / double(x.size());
    for (double& v : x)
        v -= m;
}

// ---------------------------------------------------------------------------------------------

std::vector<std::vector<double>> Acquirer::readChunked(mydaq::AiTask& task, int64_t total, const StopFn& stop)
{
    const size_t columns = task.config().channels.size();
    std::vector<std::vector<double>> out(columns);
    for (auto& column : out)
        column.reserve(size_t(total));
    const double rate = task.sampleRate();
    const int64_t chunk = std::max<int64_t>(1, int64_t(rate * 0.08));
    int64_t got = 0;
    while (got < total) {
        if (stop && stop()) {
            task.stop();
            return {};
        }
        const int64_t n = std::min(total - got, chunk);
        const int timeoutMs = int(double(n) / rate * 1000.0) + 1500;
        auto data = task.read(n, timeoutMs);
        if (data.size() != columns)
            throw mydaq::DeviceError(mydaq::DeviceError::Generic, tr("unexpected channel count from the device"));
        int64_t received = int64_t(data.front().size());
        if (received == 0)
            break;
        for (size_t c = 0; c < columns; ++c)
            out[c].insert(out[c].end(), data[c].begin(), data[c].end());
        got += received;
    }
    return out;
}

CycleResult Acquirer::acquire(mydaq::Device& device, const Snapshot& snapshot, const StopFn& stop)
{
    CycleResult result;
    AcquisitionPlan plan = planAcquisition(snapshot, device);
    if (plan.edge) {
        AcquisitionPlan base = plan;
        base.config.samplesPerChannel = 0;
        if (!sameTask(base, m_basePlan)) {
            m_basePlan = base;
            m_searchFactor = 1;
            m_triggeredStreak = 0;
        }
        const int64_t n = plan.recordLength;
        const int64_t cap = std::max<int64_t>(n, std::min<int64_t>(int64_t(plan.config.sampleRate * 0.25), 200000));
        plan.config.samplesPerChannel = n + std::min(n * m_searchFactor, cap);
    }
    if (!m_task || !sameTask(plan, m_plan)) {
        m_task.reset();   // one AI task at a time: release before creating the new one
        m_task = device.createAiTask(plan.config);
    }
    m_plan = plan;
    m_task->start();
    std::vector<std::vector<double>> data = readChunked(*m_task, plan.config.samplesPerChannel, stop);
    if (data.empty()) {
        result.stopped = true;
        return result;
    }
    result.samplesMoved = plan.config.samplesPerChannel * int64_t(data.size());

    const int64_t n = plan.recordLength;
    int64_t start = 0;
    Record record;
    if (plan.edge) {
        const int tc = plan.triggerChannel;
        const int64_t pre = pretriggerSamples(n, snapshot.horizontalPercent);
        TriggerSettings trigger{snapshot.level, snapshot.slope,
                                std::max(0.0, snapshot.hysteresisDiv) * snapshot.channel[tc].voltsPerDiv};
        if (trigger.hysteresis > 0)
            trigger.hysteresis = std::max(trigger.hysteresis, 1e-3);
        // The trigger sees the channel after its coupling (audio: mean removed).
        std::vector<double>& triggerData = data[size_t(plan.column[tc])];
        if (isAudio(snapshot.channel[tc].source))
            removeMean(triggerData);
        const auto k = findTrigger(triggerData, int64_t(triggerData.size()) - n + 1, pre, trigger);   // whole search window
        if (k) {
            if (++m_triggeredStreak >= 8 && m_searchFactor > 1) {
                m_searchFactor = std::max<int64_t>(1, m_searchFactor / 2);
                m_triggeredStreak = 0;
            }
            start = *k;
            record.triggered = true;
            record.triggerIndex = pre;
            record.triggerShift = triggerSubSample(triggerData, pre + *k, trigger.level);
        } else {
            m_triggeredStreak = 0;
            if (m_searchFactor < 4096)
                m_searchFactor *= 2;
            result.timedOut = true;
            if (snapshot.noTrigger != NoTrigger::Auto)
                return result;   // ELVISmx: nothing to display (the caller clears or keeps)
        }
    }
    record.sampleRate = m_task->sampleRate();
    const double skew = m_task->channelSkew();
    for (int ch = 0; ch < 2; ++ch) {
        const int column = plan.column[ch];
        if (column < 0)
            continue;
        const auto& source = data[size_t(column)];
        const auto first = source.begin() + std::min<int64_t>(start, int64_t(source.size()));
        const auto last = source.begin() + std::min<int64_t>(start + n, int64_t(source.size()));
        record.data[ch].assign(first, last);
        if (isAudio(snapshot.channel[ch].source))
            removeMean(record.data[ch]);
        record.skew[ch] = double(column) * skew;
    }
    result.record = std::move(record);
    return result;
}

void Acquirer::reset()
{
    m_task.reset();
    m_plan = {};
    m_basePlan = {};
    m_searchFactor = 1;
    m_triggeredStreak = 0;
}

double Acquirer::sampleRate() const
{
    return m_task ? m_task->sampleRate() : 0.0;
}

// ---------------------------------------------------------------------------------------------

void RecordAverager::setWindow(int records)
{
    records = std::max(1, records);
    if (records != m_window) {
        m_window = records;
        reset();
    }
}

void RecordAverager::reset()
{
    m_count = 0;
    m_rate = 0;
    m_mean[0].clear();
    m_mean[1].clear();
}

int RecordAverager::apply(Record& record)
{
    if (m_window <= 1)
        return 1;
    bool same = m_count > 0 && m_rate == record.sampleRate;
    for (int ch = 0; ch < 2 && same; ++ch)
        same = m_mean[ch].size() == record.data[ch].size();
    if (!same)
        reset();
    m_rate = record.sampleRate;
    m_count = std::min(m_count + 1, m_window);
    for (int ch = 0; ch < 2; ++ch) {
        auto& x = record.data[ch];
        auto& mean = m_mean[ch];
        if (x.empty()) {
            mean.clear();
            continue;
        }
        if (m_count == 1 || mean.size() != x.size()) {
            mean = x;
        } else {
            const double k = 1.0 / double(m_count);
            for (size_t i = 0; i < x.size(); ++i)
                mean[i] += (x[i] - mean[i]) * k;
        }
        x = mean;
    }
    return m_count;
}

// ---------------------------------------------------------------------------------------------

std::optional<AutoscaleResult> runAutoscale(mydaq::Device& device, const Snapshot& snapshot,
                                            const Acquirer::StopFn& stop)
{
    // One task with every enabled source (equivalent to ELVISmx's per-channel probes, faster).
    Snapshot probe = snapshot;
    for (auto& c : probe.channel)
        c.range = InputRange::Range10V;   // ±10 V (audio: 2 V, forced by inputRangeFor)
    AcquisitionPlan plan = planAcquisition(probe, device);
    AutoscaleResult result;
    bool found[2] = {false, false};
    bool have[2] = {false, false};
    for (double rate : autoscaleRates()) {
        plan.config.sampleRate = device.coerceAiRate(rate);
        plan.config.samplesPerChannel = kAutoscaleSamples;
        plan.config.mode = mydaq::SampleMode::Finite;
        std::vector<std::vector<double>> data;
        double actualRate = 0;
        {
            auto task = device.createAiTask(plan.config);
            actualRate = task->sampleRate();
            task->start();
            data = Acquirer::readChunked(*task, kAutoscaleSamples, stop);
        }
        if (data.empty())
            return std::nullopt;
        bool missing = false;
        for (int ch = 0; ch < 2; ++ch) {
            if (plan.column[ch] < 0 || found[ch])
                continue;
            std::vector<double> block = data[size_t(plan.column[ch])];
            if (isAudio(snapshot.channel[ch].source))
                removeMean(block);
            const AutoscaleChannel c = autoscaleChannel(analyseProbe(block, actualRate),
                                                        isAudio(snapshot.channel[ch].source));
            if (c.tonePresent || !have[ch]) {
                result.channel[ch] = c;
                have[ch] = true;
            }
            found[ch] = c.tonePresent;
            missing = missing || !c.tonePresent;
        }
        if (!missing)
            break;
    }
    double lowest = std::numeric_limits<double>::infinity();
    for (int ch = 0; ch < 2; ++ch)
        if (found[ch])
            lowest = std::min(lowest, result.channel[ch].frequency);
    result.timePerDiv = std::isfinite(lowest) ? autoscaleTimePerDiv(lowest) : kNaN;
    return result;
}

// ---------------------------------------------------------------------------------------------

void Roller::start(mydaq::Device& device, const Snapshot& snapshot)
{
    Snapshot s = snapshot;
    s.roll = true;
    m_plan = planAcquisition(s, device);
    m_plan.config.mode = mydaq::SampleMode::Continuous;
    m_plan.config.samplesPerChannel = std::max<int64_t>(m_plan.recordLength, int64_t(m_plan.config.sampleRate));
    m_task.reset();
    m_task = device.createAiTask(m_plan.config);
    m_ring.assign(m_plan.config.channels.size(), std::vector<double>(size_t(m_plan.recordLength), 0.0));
    m_count = 0;
    m_task->start();
}

void Roller::stop()
{
    m_task.reset();
}

int64_t Roller::poll()
{
    if (!m_task)
        return 0;
    auto data = m_task->read(-1, 0);   // whatever arrived, without blocking (Overflow if too late)
    if (data.empty() || data.front().empty())
        return 0;
    const size_t received = data.front().size();
    const int64_t length = m_plan.recordLength;
    for (size_t i = 0; i < received; ++i) {
        const size_t slot = size_t((m_count + int64_t(i)) % length);
        for (size_t c = 0; c < data.size(); ++c)
            m_ring[c][slot] = data[c][i];
    }
    m_count += int64_t(received);
    return int64_t(received) * int64_t(data.size());
}

bool Roller::matches(const AcquisitionPlan& plan) const
{
    if (!m_task || plan.recordLength != m_plan.recordLength || plan.config.sampleRate != m_plan.config.sampleRate
        || plan.config.channels.size() != m_plan.config.channels.size())
        return false;
    for (size_t i = 0; i < plan.config.channels.size(); ++i)
        if (plan.config.channels[i].channel != m_plan.config.channels[i].channel
            || plan.config.channels[i].range != m_plan.config.channels[i].range)
            return false;
    return plan.column[0] == m_plan.column[0] && plan.column[1] == m_plan.column[1];
}

int64_t Roller::filled() const
{
    return std::min(m_count, m_plan.recordLength);
}

Record Roller::record() const
{
    Record record;
    if (!m_task)
        return record;
    record.sampleRate = m_task->sampleRate();
    const int64_t length = m_plan.recordLength;
    const int64_t count = filled();
    const int64_t first = m_count - count;
    const double skew = m_task->channelSkew();
    for (int ch = 0; ch < 2; ++ch) {
        const int column = m_plan.column[ch];
        if (column < 0)
            continue;
        std::vector<double>& out = record.data[ch];
        out.resize(size_t(count));
        const auto& ring = m_ring[size_t(column)];
        for (int64_t i = 0; i < count; ++i)
            out[size_t(i)] = ring[size_t((first + i) % length)];
        if (mydaq::isAudio(m_plan.config.channels[size_t(column)].channel))
            removeMean(out);
        record.skew[ch] = double(column) * skew;
    }
    return record;
}

}  // namespace mybench::instruments::scope
