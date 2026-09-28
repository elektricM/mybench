#include "dsaanalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mybench::instruments::dsa {

namespace dsp = mydaq::dsp;

namespace {

double finiteOrZero(double value)
{
    return std::isfinite(value) ? value : 0.0;
}

}  // namespace

int recordsForSingle(const AveragingSettings& settings)
{
    return settings.mode == dsp::AveragingMode::None ? 1 : std::max(1, settings.averages);
}

double halfPeakToPeak(std::span<const double> record)
{
    if (record.empty())
        return 0.0;
    const auto [lo, hi] = std::minmax_element(record.begin(), record.end());
    return finiteOrZero((*hi - *lo) / 2.0);
}

Measurements measure(const dsp::PowerSpectrum& spectrum, std::span<const double> record)
{
    Measurements m;
    m.vpk = halfPeakToPeak(record);
    const dsp::HarmonicAnalysis h = dsp::analyzeHarmonics(spectrum, kHighestHarmonic);
    if (!(h.fundamentalPower > 0.0) || !std::isfinite(h.fundamentalFrequency) || h.harmonicPower.empty())
        return m;

    // The fundamental's bin, found exactly as analyzeHarmonics() does (largest line outside the
    // DC lobe), then the ELVISmx "Power & Frequency Estimate" over ±3 lines.
    const int lobe = std::max(1, spectrum.info.lobeHalfWidth);
    const dsp::PeakEstimate peak = dsp::findPeak(spectrum, lobe);
    const dsp::PeakEstimate estimate = dsp::estimatePeak(spectrum, peak.bin, kFundamentalSpan);

    m.valid = true;
    m.fundamentalFrequency = finiteOrZero(h.fundamentalFrequency);
    m.fundamentalBin = peak.bin;
    m.fundamentalPower = finiteOrZero(estimate.power);
    m.thdPercent = finiteOrZero(100.0 * h.thd);
    m.sinadDb = finiteOrZero(h.sinad);
    m.harmonics.reserve(h.harmonicPower.size());
    for (size_t i = 0; i < h.harmonicPower.size() && i < h.harmonicFrequency.size(); ++i)
        m.harmonics.push_back({int(i) + 1, h.harmonicFrequency[i], finiteOrZero(h.harmonicPower[i])});
    return m;
}

std::vector<double> scaledTimeDomainWindow(std::span<const double> record, dsp::WindowType window)
{
    std::vector<double> out(record.begin(), record.end());
    if (window == dsp::WindowType::None || out.empty())
        return out;
    const std::vector<double> w = dsp::makeWindow(window, out.size());
    const double gain = dsp::coherentGain(w);
    const double scale = gain > 0.0 ? 1.0 / gain : 1.0;
    for (size_t k = 0; k < out.size(); ++k)
        out[k] *= w[k] * scale;
    return out;
}

std::vector<double> powersToDb(std::span<const double> powers)
{
    std::vector<double> db(powers.size());
    for (size_t k = 0; k < powers.size(); ++k)
        db[k] = powers[k] > 0.0 ? std::max(10.0 * std::log10(powers[k]), -400.0) : -400.0;
    return db;
}

std::vector<size_t> localMaxima(std::span<const double> values, double minProminence, size_t first, double minValue)
{
    std::vector<size_t> peaks;
    const size_t n = values.size();
    if (n < 3 || first + 2 > n)
        return peaks;
    for (size_t i = std::max<size_t>(first, 1); i + 1 < n; ++i) {
        const double v = values[i];
        // Rising into i, not rising after it (the first sample of a plateau is the peak).
        if (!(v > values[i - 1] && v >= values[i + 1]))
            continue;
        // Skip the rest of a plateau so it counts once.
        size_t j = i;
        while (j + 1 < n && values[j + 1] == v)
            ++j;
        if (j + 1 < n && values[j + 1] > v)
            continue;   // a shoulder, not a peak
        // Prominence: the lowest point on each side before a higher sample (or the edge); the
        // peak's base is the higher of the two.
        double leftMin = v;
        for (size_t k = i; k-- > first;) {
            if (values[k] > v)
                break;
            leftMin = std::min(leftMin, values[k]);
        }
        double rightMin = v;
        for (size_t k = j + 1; k < n; ++k) {
            if (values[k] > v)
                break;
            rightMin = std::min(rightMin, values[k]);
        }
        if (v - std::max(leftMin, rightMin) >= minProminence && v >= minValue)
            peaks.push_back(i);
        i = j;
    }
    return peaks;
}

double medianValue(std::span<const double> values, size_t first)
{
    if (first >= values.size())
        return 0.0;
    std::vector<double> v(values.begin() + std::ptrdiff_t(first), values.end());
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
    return v[v.size() / 2];
}

std::optional<size_t> searchPeak(std::span<const double> values, PeakSearch search, size_t from,
                                 double minProminence, size_t first, double minValue)
{
    const std::vector<size_t> peaks = localMaxima(values, minProminence, first, minValue);
    if (peaks.empty())
        return std::nullopt;
    switch (search) {
    case PeakSearch::Highest:
        return *std::max_element(peaks.begin(), peaks.end(),
                                 [&](size_t a, size_t b) { return values[a] < values[b]; });
    case PeakSearch::NextLower: {
        const double level = from < values.size() ? values[from] : std::numeric_limits<double>::infinity();
        std::optional<size_t> best;
        for (size_t p : peaks) {
            if (p == from || values[p] >= level)
                continue;
            if (!best || values[p] > values[*best])
                best = p;
        }
        return best;
    }
    case PeakSearch::Left: {
        std::optional<size_t> best;
        for (size_t p : peaks)
            if (p < from)
                best = p;
        return best;
    }
    case PeakSearch::Right:
        for (size_t p : peaks)
            if (p > from)
                return p;
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<SpectralPeak> strongestPeaks(const dsp::PowerSpectrum& spectrum, size_t maxCount, double minProminenceDb)
{
    std::vector<SpectralPeak> peaks;
    if (spectrum.power.size() < 4 || maxCount == 0)
        return peaks;
    const int lobe = std::max(1, spectrum.info.lobeHalfWidth);
    const std::vector<double> db = powersToDb(spectrum.power);
    // Only components 10 dB above the noise floor (median line), not the noise's own ripples.
    const double floor = medianValue(db, size_t(lobe) + 1) + 10.0;
    for (size_t bin : localMaxima(db, minProminenceDb, size_t(lobe) + 1, floor)) {
        const dsp::PeakEstimate e = dsp::estimatePeak(spectrum, bin, lobe);
        peaks.push_back({bin, finiteOrZero(e.frequency), finiteOrZero(e.power)});
    }
    std::sort(peaks.begin(), peaks.end(), [](const SpectralPeak& a, const SpectralPeak& b) { return a.power > b.power; });
    if (peaks.size() > maxCount)
        peaks.resize(maxCount);
    return peaks;
}

// ---------------------------------------------------------------------------------------------

SpectrumProcessor::SpectrumProcessor(const AveragingSettings& settings)
    : m_settings(settings), m_averager(settings.mode, settings.weighting, std::max(1, settings.averages))
{
}

void SpectrumProcessor::configure(const AveragingSettings& settings)
{
    m_settings = settings;
    m_averager.configure(settings.mode, settings.weighting, std::max(1, settings.averages));
    restart();
}

void SpectrumProcessor::restart()
{
    m_averager.restart();
    m_maxHold.clear();
    m_minHold.clear();
}

void SpectrumProcessor::add(std::span<const double> record, double sampleRate)
{
    m_record.assign(record.begin(), record.end());
    m_sampleRate = sampleRate;
    const size_t previousBins = m_averager.result().power.size();
    m_averager.add(dsp::complexSpectrum(m_record, sampleRate, m_settings.window));
    const dsp::PowerSpectrum p = m_averager.result();
    if (m_maxHold.size() != p.power.size() || previousBins != p.power.size()) {
        m_maxHold = p.power;
        m_minHold = p.power;
        return;
    }
    for (size_t k = 0; k < p.power.size(); ++k) {
        m_maxHold[k] = std::max(m_maxHold[k], p.power[k]);
        m_minHold[k] = std::min(m_minHold[k], p.power[k]);
    }
}

}  // namespace mybench::instruments::dsa
