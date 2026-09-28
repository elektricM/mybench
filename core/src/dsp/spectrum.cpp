#include "mydaq/dsp/spectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mydaq::dsp {

namespace {

constexpr double kLogFloorDb = -400.0;

double toDb(double ratio) { return ratio > 0.0 ? std::max(10.0 * std::log10(ratio), kLogFloorDb) : kLogFloorDb; }

bool isAcBin(const SpectrumInfo& info, size_t bin)
{
    return bin > 0 && !(info.hasNyquistBin() && bin == info.length / 2);
}

}  // namespace

std::vector<double> PowerSpectrum::frequencies() const
{
    std::vector<double> f(power.size());
    for (size_t k = 0; k < f.size(); ++k)
        f[k] = frequency(k);
    return f;
}

ComplexSpectrum complexSpectrum(const std::vector<double>& x, double sampleRate, WindowType window)
{
    ComplexSpectrum s;
    const size_t n = x.size();
    s.info.length = n;
    s.info.lobeHalfWidth = mainLobeHalfWidth(window);
    if (n == 0)
        return s;
    s.info.df = sampleRate / double(n);

    const std::vector<double> w = makeWindow(window, n);
    s.info.enbw = equivalentNoiseBandwidth(w);
    double sum = 0.0;
    std::vector<double> xw(n);
    for (size_t k = 0; k < n; ++k) {
        xw[k] = x[k] * w[k];
        sum += w[k];
    }
    s.bins = rfft(xw);
    // |X[k]| = A * sum(w) / 2 for a sine of peak amplitude A: scale AC bins to its RMS value.
    const double acScale = std::numbers::sqrt2 / sum;
    const double dcScale = 1.0 / sum;
    for (size_t k = 0; k < s.bins.size(); ++k)
        s.bins[k] *= isAcBin(s.info, k) ? acScale : dcScale;
    return s;
}

PowerSpectrum powerOf(const ComplexSpectrum& spectrum)
{
    PowerSpectrum p;
    p.info = spectrum.info;
    p.power.resize(spectrum.bins.size());
    for (size_t k = 0; k < p.power.size(); ++k)
        p.power[k] = std::norm(spectrum.bins[k]);
    return p;
}

PowerSpectrum powerSpectrum(const std::vector<double>& x, double sampleRate, WindowType window)
{
    return powerOf(complexSpectrum(x, sampleRate, window));
}

double formatPower(double power, const SpectrumFormat& format, const SpectrumInfo& info, bool acBin)
{
    double p = power;
    if (format.mode == SpectrumMode::Peak && acBin)
        p *= 2.0;
    if (format.density && info.df > 0.0)
        p /= info.enbw * info.df;
    switch (format.unit) {
    case SpectrumUnit::Linear: return std::sqrt(std::max(p, 0.0));
    case SpectrumUnit::Db: return toDb(p);
    case SpectrumUnit::Dbm: return toDb(p / format.referenceImpedance / 1e-3);
    }
    return p;
}

std::vector<double> formatSpectrum(const PowerSpectrum& spectrum, const SpectrumFormat& format)
{
    std::vector<double> out(spectrum.power.size());
    for (size_t k = 0; k < out.size(); ++k)
        out[k] = formatPower(spectrum.power[k], format, spectrum.info, isAcBin(spectrum.info, k));
    return out;
}

QString spectrumUnitLabel(const SpectrumFormat& format)
{
    const QString v = format.mode == SpectrumMode::Rms ? QStringLiteral("Vrms") : QStringLiteral("Vpk");
    switch (format.unit) {
    case SpectrumUnit::Linear: return format.density ? v + QStringLiteral("/√Hz") : v;
    case SpectrumUnit::Db: return format.density ? QStringLiteral("dBV/Hz") : QStringLiteral("dBV");
    case SpectrumUnit::Dbm: return format.density ? QStringLiteral("dBm/Hz") : QStringLiteral("dBm");
    }
    return v;
}

PeakEstimate estimatePeak(const PowerSpectrum& spectrum, size_t bin, int span)
{
    PeakEstimate e;
    e.bin = bin;
    const size_t n = spectrum.power.size();
    if (bin >= n)
        return e;
    const size_t first = bin >= size_t(span) ? bin - size_t(span) : 0;
    const size_t last = std::min(n - 1, bin + size_t(span));
    double sum = 0.0;
    double moment = 0.0;
    for (size_t k = first; k <= last; ++k) {
        sum += spectrum.power[k];
        moment += double(k) * spectrum.power[k];
    }
    e.power = sum / spectrum.info.enbw;
    e.frequency = sum > 0.0 ? moment / sum * spectrum.info.df : spectrum.frequency(bin);
    return e;
}

PeakEstimate findPeak(const PowerSpectrum& spectrum, int span, double minFrequency, double maxFrequency)
{
    size_t best = 0;
    double bestPower = -1.0;
    for (size_t k = size_t(spectrum.info.lobeHalfWidth) + 1; k < spectrum.power.size(); ++k) {
        const double f = spectrum.frequency(k);
        if (f < minFrequency || f > maxFrequency)
            continue;
        if (spectrum.power[k] > bestPower) {
            bestPower = spectrum.power[k];
            best = k;
        }
    }
    if (bestPower < 0.0)
        return {};
    return estimatePeak(spectrum, best, span);
}

// ---------------------------------------------------------------------------------------------

SpectrumAverager::SpectrumAverager(AveragingMode mode, AveragingWeighting weighting, int averages)
    : m_mode(mode), m_weighting(weighting), m_averages(std::max(1, averages))
{
}

void SpectrumAverager::configure(AveragingMode mode, AveragingWeighting weighting, int averages)
{
    m_mode = mode;
    m_weighting = weighting;
    m_averages = std::max(1, averages);
    restart();
}

void SpectrumAverager::restart()
{
    m_count = 0;
    m_vector.clear();
    m_power.clear();
}

void SpectrumAverager::add(const ComplexSpectrum& spectrum)
{
    const size_t n = spectrum.bins.size();
    const size_t current = m_mode == AveragingMode::Vector ? m_vector.size() : m_power.size();
    if (m_count > 0 && (n != current || spectrum.info.df != m_info.df))
        restart();
    m_info = spectrum.info;

    if (m_mode == AveragingMode::None) {
        m_power.resize(n);
        for (size_t k = 0; k < n; ++k)
            m_power[k] = std::norm(spectrum.bins[k]);
        m_count = 1;
        return;
    }
    if (m_mode == AveragingMode::PeakHold) {
        if (m_count == 0)
            m_power.assign(n, 0.0);
        for (size_t k = 0; k < n; ++k)
            m_power[k] = std::max(m_power[k], std::norm(spectrum.bins[k]));
        ++m_count;
        return;
    }
    if (m_weighting == AveragingWeighting::Linear && m_count >= m_averages)
        return;   // complete: held until restart()

    m_count = std::min(m_count + 1, m_averages);
    const double weight = 1.0 / double(m_count);
    if (m_mode == AveragingMode::Vector) {
        if (m_vector.empty())
            m_vector.assign(n, Complex());
        for (size_t k = 0; k < n; ++k)
            m_vector[k] += (spectrum.bins[k] - m_vector[k]) * weight;
    } else {
        if (m_power.empty())
            m_power.assign(n, 0.0);
        for (size_t k = 0; k < n; ++k)
            m_power[k] += (std::norm(spectrum.bins[k]) - m_power[k]) * weight;
    }
}

PowerSpectrum SpectrumAverager::result() const
{
    PowerSpectrum p;
    p.info = m_info;
    if (m_mode == AveragingMode::Vector) {
        p.power.resize(m_vector.size());
        for (size_t k = 0; k < m_vector.size(); ++k)
            p.power[k] = std::norm(m_vector[k]);
    } else {
        p.power = m_power;
    }
    return p;
}

bool SpectrumAverager::isComplete() const
{
    switch (m_mode) {
    case AveragingMode::None: return m_count >= 1;
    case AveragingMode::PeakHold: return false;
    case AveragingMode::Vector:
    case AveragingMode::Rms: return m_count >= m_averages;
    }
    return false;
}

}  // namespace mydaq::dsp
