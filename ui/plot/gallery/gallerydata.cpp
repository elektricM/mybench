#include "gallerydata.h"

#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/window.h>

#include <QDateTime>
#include <QMetaObject>
#include <QVariantMap>

#include <chrono>
#include <cmath>
#include <complex>
#include <random>

using namespace mybench::plot;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 200000.0;  // myDAQ AI rate

double nowSeconds()
{
    return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

// 1 kHz sine and an RC-filtered square, triggered at t = 0 with a little jitter.
void scopeRecord(std::mt19937 &rng, std::vector<double> &a, std::vector<double> &b, double jitter)
{
    std::normal_distribution<double> noise(0.0, 0.012);
    const size_t n = a.size();
    const double t0 = -1e-3;
    double rc = -1.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = t0 + double(i) / kRate + jitter;
        a[i] = 1.6 * std::sin(2 * kPi * 1000.0 * t) + 0.12 * std::sin(2 * kPi * 3000.0 * t + 0.4) + noise(rng);
        const double sq = std::sin(2 * kPi * 1000.0 * (t - 0.25e-3)) >= 0 ? 2.5 : -2.5;
        if (rc < -0.5)
            rc = sq;
        rc += (sq - rc) * (1.0 - std::exp(-1.0 / (kRate * 28e-6)));
        b[i] = rc + 0.5 * noise(rng);
    }
}

}  // namespace

GalleryData::GalleryData(QObject *parent) : QObject(parent)
{
    m_trend.setMaximumCount(3000);
    m_waterfall.setCapacity(240);
    m_logic.setLaneCount(8);
    m_logic.setMaximumSpan(30.0);
    computeStatic();
    m_thread = std::thread([this] { worker(); });
    m_recorderThread = std::thread([this] { recorder(); });
}

GalleryData::~GalleryData()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
    if (m_recorderThread.joinable())
        m_recorderThread.join();
}

void GalleryData::setRunning(bool on)
{
    if (m_running.exchange(on) != on)
        emit runningChanged();
}

void GalleryData::setDeepSamples(int n)
{
    n = std::clamp(n, 1000, 2000000);
    if (m_deepSamples.exchange(n) != n)
        emit deepSamplesChanged();
}

double GalleryData::trendMean() const
{
    std::lock_guard lock(m_statsMutex);
    return m_mean;
}

double GalleryData::trendSigma() const
{
    std::lock_guard lock(m_statsMutex);
    return m_sigma;
}

double GalleryData::trendLast() const
{
    std::lock_guard lock(m_statsMutex);
    return m_last;
}

double GalleryData::now() const
{
    std::lock_guard lock(m_statsMutex);
    return m_now;
}

void GalleryData::pushDeepFrame()
{
    const size_t count = std::min(size_t(m_deepSamples.load()), m_deepA.size() / 2);
    const size_t offset = (m_deepFrame.fetch_add(1) * 1667) % (m_deepA.size() - count);
    m_deep0.setUniform(0.0, 1.0 / kRate, std::span<const double>(m_deepA.data() + offset, count));
    m_deep1.setUniform(0.0, 1.0 / kRate, std::span<const double>(m_deepB.data() + offset, count));
}

void GalleryData::computeStatic()
{
    // ---- Deep memory: an AM carrier and a ringing square wave, 2 s + 2 M samples ----
    {
        std::mt19937 drng(5);
        std::normal_distribution<double> dn(0.0, 1.0);
        const size_t deepLen = size_t(2 * kRate) + 2000000;
        m_deepA.resize(deepLen);
        m_deepB.resize(deepLen);
        for (size_t i = 0; i < deepLen; ++i) {
            const double t = double(i) / kRate;
            const double env = 1.0 + 0.75 * std::sin(2 * kPi * 1.7 * t);
            m_deepA[i] = 1.1 * env * std::sin(2 * kPi * 1200.0 * t) + 0.02 * dn(drng);
            const double phaseSq = std::fmod(t * 5.0, 1.0);
            const double sinceEdge = std::fmod(t * 10.0, 1.0) / 10.0;
            const double ring = 0.6 * std::exp(-sinceEdge * 900.0) * std::sin(2 * kPi * 2400.0 * sinceEdge);
            m_deepB[i] = (phaseSq < 0.5 ? 2.0 : -2.0) + (phaseSq < 0.5 ? -ring : ring) + 0.02 * dn(drng);
        }
    }

    // ---- Bode: 2nd-order low-pass (fc 1 kHz, Q 0.9) and an RC reference ----
    std::vector<double> f, gain, phase, gainRef, phaseRef;
    std::mt19937 rng(3);
    std::normal_distribution<double> noise(0.0, 1.0);
    const double f0 = 1000.0, q = 0.9;
    for (int i = 0; i <= 80; ++i) {
        const double fr = 10.0 * std::pow(10.0, i / 20.0);  // 10 Hz .. 100 kHz, 20/decade
        const std::complex<double> s(0.0, fr / f0);
        const std::complex<double> h = 1.0 / (s * s + s / q + 1.0);
        double g = 20 * std::log10(std::abs(h));
        g = 10 * std::log10(std::pow(10.0, g / 10) + std::pow(10.0, -78.0 / 10)) + noise(rng) * (g < -50 ? 0.9 : 0.04);
        f.push_back(fr);
        gain.push_back(g);
        phase.push_back(std::arg(h) * 180.0 / kPi + noise(rng) * (g < -55 ? 4.0 : 0.15));
        const std::complex<double> r = 1.0 / (1.0 + std::complex<double>(0.0, fr / 2200.0));
        gainRef.push_back(20 * std::log10(std::abs(r)));
        phaseRef.push_back(std::arg(r) * 180.0 / kPi);
    }
    m_bodeGain.setXY(std::span<const double>(f), std::span<const double>(gain));
    m_bodePhase.setXY(std::span<const double>(f), std::span<const double>(phase));
    m_bodeGainRef.setXY(std::span<const double>(f), std::span<const double>(gainRef));
    m_bodePhaseRef.setXY(std::span<const double>(f), std::span<const double>(phaseRef));
    // −3 dB point (relative to the passband gain at 10 Hz), by interpolation.
    const double ref = gain.front() - 3.0;
    m_cutoff = f.back();
    for (size_t i = 1; i < gain.size(); ++i) {
        if (gain[i] <= ref && gain[i - 1] > ref) {
            const double t = (ref - gain[i - 1]) / (gain[i] - gain[i - 1]);
            m_cutoff = std::pow(10.0, std::log10(f[i - 1]) + t * (std::log10(f[i]) - std::log10(f[i - 1])));
            break;
        }
    }
    {
        const std::complex<double> s(0.0, m_cutoff / f0);
        m_phaseAtCutoff = std::arg(1.0 / (s * s + s / q + 1.0)) * 180.0 / kPi;
    }

    // ---- Spectrum: 1 kHz with harmonics, Hanning, dBV ----
    const double fs = 51200.0;
    const size_t n = 8192;
    std::vector<double> x(n);
    const double amps[] = {0.7, 0.007, 0.022, 0.0012, 0.0055, 0.0004, 0.0019, 0.0002, 0.0008};
    std::normal_distribution<double> wn(0.0, 2e-5);
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / fs;
        double v = 0;
        for (int h = 0; h < 9; ++h)
            v += amps[h] * std::sin(2 * kPi * 1000.0 * (h + 1) * t + 0.3 * h);
        v += 0.0006 * std::sin(2 * kPi * 60.0 * t);  // mains hum
        x[i] = v + wn(rng);
    }
    const mydaq::dsp::PowerSpectrum ps = mydaq::dsp::powerSpectrum(x, fs, mydaq::dsp::WindowType::Hanning);
    mydaq::dsp::SpectrumFormat fmt;
    const std::vector<double> dbv = mydaq::dsp::formatSpectrum(ps, fmt);
    m_spectrum.setUniform(0.0, ps.info.df, std::span<const double>(dbv));
    std::vector<double> hold(dbv);
    for (double &v : hold)
        v += 3.0 + std::abs(noise(rng)) * 2.0;
    m_spectrumAverage.setUniform(0.0, ps.info.df, std::span<const double>(hold));
    for (int h = 1; h <= 6; ++h) {
        const mydaq::dsp::PeakEstimate p = mydaq::dsp::findPeak(ps, 3, 1000.0 * h - 200, 1000.0 * h + 200);
        const double level = mydaq::dsp::formatPower(p.power, fmt, ps.info);
        QVariantMap m;
        m.insert(QStringLiteral("x"), p.frequency);
        m.insert(QStringLiteral("y"), dbv[p.bin]);
        m.insert(QStringLiteral("name"), QStringLiteral("H%1").arg(h));
        m.insert(QStringLiteral("level"), level);
        m_harmonics.append(m);
    }

    // ---- Waterfall: pre-fill with a chirp spectrogram ----
    m_waterfall.setColumns(0.0, 20480.0 / 1024.0, 512);

    // ---- Trend: 60 s of history at 5 readings/s ----
    const double t1 = nowSeconds();
    std::normal_distribution<double> dmm(0.0, 0.00018);
    for (int i = 300; i > 0; --i) {
        const double t = t1 - i * 0.2;
        m_trend.append(t, 4.9985 + 0.0009 * std::sin(t * 0.21) + 0.0004 * std::sin(t * 1.3) + dmm(rng));
    }
}

void GalleryData::recorder()
{
    std::mt19937 rng(17);
    std::normal_distribution<double> noise(0.0, 1.0);
    // Recording: 100 s at 200 kS/s pre-recorded (2e7 samples), then real time.
    const double recStart = nowSeconds() - 100.0;
    m_recording.startUniform(recStart, 1.0 / kRate);
    double oscA = 0, oscB = 0;
    size_t recIndex = 0;
    auto recordBlock = [&](size_t count) {
        std::vector<double> block(count);
        for (size_t k = 0; k < count; ++k, ++recIndex) {
            const double t = double(recIndex) / kRate;
            oscA = std::sin(2 * kPi * 0.05 * t);
            oscB = std::sin(2 * kPi * 0.9 * t);
            const double burst = std::fmod(t, 7.0) < 0.35 ? std::sin(2 * kPi * 350.0 * t) * 0.8 : 0.0;
            block[k] = 1.5 * oscA + 0.35 * oscB + burst + 0.01 * noise(rng);
        }
        m_recording.appendSamples(std::span<const double>(block));
    };
    for (int i = 0; i < 100; ++i)
        recordBlock(size_t(kRate));

    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; !m_stop.load(); ++frame) {
        if (m_running.load())
            recordBlock(3333);
        std::this_thread::sleep_until(start + std::chrono::microseconds(16667) * (frame + 1));
    }
}

void GalleryData::worker()
{
    std::mt19937 rng(11);
    std::normal_distribution<double> noise(0.0, 1.0);
    {
        std::vector<double> a0(401), b0(401);
        scopeRecord(rng, a0, b0, 0.0);
        m_scope0.setUniform(-1e-3, 1.0 / kRate, std::span<const double>(a0));
        m_scope1.setUniform(-1e-3, 1.0 / kRate, std::span<const double>(b0));
        pushDeepFrame();
    }

    // Logic: 8 DIO lanes sampled at 200 kHz.
    double logicTime = 0.0;
    size_t logicIndex = 0;
    const char *message = "MYBENCH myDAQ ";
    auto logicBlock = [&](size_t count) {
        std::vector<quint8> states(count);
        for (size_t k = 0; k < count; ++k, ++logicIndex) {
            const double t = double(logicIndex) / kRate;
            quint8 s = 0;
            const int clk = int(t * 2000.0);  // 1 kHz clock
            s |= (clk & 1) ? 1 : 0;
            // UART 9600 8N1, one byte every 2 ms
            const double inByte = std::fmod(t, 2e-3) * 9600.0;
            const int bit = int(inByte);
            const char c = message[size_t(t / 2e-3) % 14];
            bool uart = true;
            if (bit == 0)
                uart = false;
            else if (bit >= 1 && bit <= 8)
                uart = (c >> (bit - 1)) & 1;
            s |= uart ? 2 : 0;
            const double duty = 0.5 + 0.4 * std::sin(2 * kPi * 0.8 * t);
            s |= std::fmod(t * 200.0, 1.0) < duty ? 4 : 0;
            s |= (clk & 2) ? 8 : 0;
            s |= (clk & 4) ? 16 : 0;
            s |= std::fmod(t, 0.01) < 0.0007 ? 0 : 32;                                 // CS pulses
            s |= (std::fmod(t, 0.1) < 0.012 && (int(t * 40000.0) & 1)) ? 64 : 0;       // 20 kHz bursts
            s |= std::fmod(t, 1.3) < 0.00002 ? 0 : 128;                                 // glitch
            states[k] = s;
        }
        m_logic.appendSamples(logicTime, 1.0 / kRate, std::span<const quint8>(states));
        logicTime += double(count) / kRate;
    };
    logicBlock(size_t(2 * kRate));

    // Waterfall pre-fill.
    const double wfRate = 20480.0;
    const size_t wfN = 1024;
    double wfTime = 0;
    auto waterfallRow = [&]() {
        std::vector<double> x(wfN);
        for (size_t i = 0; i < wfN; ++i) {
            const double t = wfTime + double(i) / wfRate;
            const double sweep = std::fmod(t, 6.0) / 6.0;
            const double fChirp = 800.0 + 7500.0 * sweep;
            const double phaseChirp = 2 * kPi * (800.0 * std::fmod(t, 6.0) + 0.5 * 7500.0 / 6.0 * std::pow(std::fmod(t, 6.0), 2));
            Q_UNUSED(fChirp);
            x[i] = 0.5 * std::sin(phaseChirp) + 0.12 * std::sin(2 * kPi * 3000.0 * t)
                 + 0.05 * std::sin(2 * kPi * 6000.0 * t) * (std::fmod(t, 2.0) < 1.0 ? 1 : 0) + 0.004 * noise(rng);
        }
        wfTime += 0.05;
        const mydaq::dsp::PowerSpectrum ps = mydaq::dsp::powerSpectrum(x, wfRate, mydaq::dsp::WindowType::BlackmanHarris);
        std::vector<double> db = mydaq::dsp::formatSpectrum(ps, mydaq::dsp::SpectrumFormat());
        db.resize(512);
        m_waterfall.appendRow(std::span<const double>(db));
        m_liveSpectrum.setUniform(0.0, ps.info.df, std::span<const double>(db));
    };
    for (int i = 0; i < 240; ++i)
        waterfallRow();

    std::vector<double> a(401), b(401);
    std::vector<double> xyX(6000), xyY(6000);
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; !m_stop.load(); ++frame) {
        const auto next = start + std::chrono::microseconds(16667) * (frame + 1);
        if (m_running.load()) {
            // Scope record.
            scopeRecord(rng, a, b, noise(rng) * 2.5e-6);
            m_scope0.setUniform(-1e-3, 1.0 / kRate, std::span<const double>(a));
            m_scope1.setUniform(-1e-3, 1.0 / kRate, std::span<const double>(b));

            pushDeepFrame();

            // XY Lissajous (3:2).
            const double phi = frame * 0.004;
            for (size_t i = 0; i < xyX.size(); ++i) {
                const double t = double(i) / double(xyX.size());
                xyX[i] = 2.6 * std::sin(2 * kPi * 3 * t) + 0.01 * noise(rng);
                xyY[i] = 2.6 * std::sin(2 * kPi * 2 * t + phi) + 0.01 * noise(rng);
            }
            m_xy.setXY(std::span<const double>(xyX), std::span<const double>(xyY));

            if (frame % 3 == 0)
                waterfallRow();
            logicBlock(3333);

            if (frame % 12 == 0) {
                const double t = nowSeconds();
                const double v = 4.9985 + 0.0009 * std::sin(t * 0.21) + 0.0004 * std::sin(t * 1.3) + 0.00018 * noise(rng);
                m_trend.append(t, v);
                const TraceSnapshot s = m_trend.snapshot();
                double sum = 0, sum2 = 0;
                for (size_t i = 0; i < s->size(); ++i) {
                    sum += s->yAt(i);
                    sum2 += s->yAt(i) * s->yAt(i);
                }
                const double nS = double(std::max<size_t>(1, s->size()));
                {
                    std::lock_guard lock(m_statsMutex);
                    m_mean = sum / nS;
                    m_sigma = std::sqrt(std::max(0.0, sum2 / nS - m_mean * m_mean));
                    m_last = v;
                    m_now = t;
                }
                QMetaObject::invokeMethod(this, &GalleryData::statsChanged, Qt::QueuedConnection);
            }
        }
        std::this_thread::sleep_until(next);
    }
}
