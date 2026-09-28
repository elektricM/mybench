#include "continuitytone.h"

#include <QGuiApplication>

#ifdef MYBENCH_DMM_HAVE_AUDIO
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>
#endif

namespace mybench::instruments {

#ifdef MYBENCH_DMM_HAVE_AUDIO
namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr double kFrequency = 1000.0;
constexpr double kVolume = 0.75;

// Endless 1 kHz sine: one period table of 44.1 samples does not tile, so the table holds
// 441 frames (10 periods exactly) and is read cyclically.
class SineSource : public QIODevice
{
public:
    SineSource()
    {
        constexpr int frames = kSampleRate / 100;   // 10 ms = 10 periods of 1 kHz
        m_table.resize(size_t(frames * kChannels));
        for (int i = 0; i < frames; ++i) {
            const auto sample = qint16(std::lround(
                0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * kFrequency * i / kSampleRate)));
            for (int c = 0; c < kChannels; ++c)
                m_table[size_t(i * kChannels + c)] = sample;
        }
        open(QIODevice::ReadOnly);
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 16; }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 tableBytes = qint64(m_table.size() * sizeof(qint16));
        qint64 written = 0;
        maxSize -= maxSize % qint64(kChannels * sizeof(qint16));
        while (written < maxSize) {
            const qint64 chunk = std::min(maxSize - written, tableBytes - m_position);
            std::memcpy(data + written, reinterpret_cast<const char*>(m_table.data()) + m_position, size_t(chunk));
            written += chunk;
            m_position = (m_position + chunk) % tableBytes;
        }
        return written;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    std::vector<qint16> m_table;
    qint64 m_position = 0;
};

}  // namespace

struct ContinuityTone::Private {
    bool attempted = false;
    bool ok = false;
    std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<SineSource> source;

    bool ensure(QObject* owner)
    {
        if (attempted)
            return ok;
        attempted = true;
        if (QGuiApplication::platformName() == QLatin1String("offscreen"))
            return false;   // screenshots / tests: never make noise
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull())
            return false;
        QAudioFormat format;
        format.setSampleRate(kSampleRate);
        format.setChannelCount(kChannels);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format))
            return false;
        sink = std::make_unique<QAudioSink>(device, format, owner);
        sink->setVolume(kVolume);
        sink->setBufferSize(kSampleRate / 20 * kChannels * int(sizeof(qint16)));   // 50 ms latency
        source = std::make_unique<SineSource>();
        ok = true;
        return ok;
    }
};

ContinuityTone::ContinuityTone(QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {}

ContinuityTone::~ContinuityTone()
{
    if (d->sink)
        d->sink->stop();
}

void ContinuityTone::setActive(bool active)
{
    if (active == m_active)
        return;
    if (active && !d->ensure(this))
        return;
    m_active = active;
    if (!d->sink)
        return;
    if (active)
        d->sink->start(d->source.get());
    else
        d->sink->stop();
}

bool ContinuityTone::available() const
{
    return !d->attempted || d->ok;
}

#else  // no Qt Multimedia: silent

struct ContinuityTone::Private {};

ContinuityTone::ContinuityTone(QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {}
ContinuityTone::~ContinuityTone() = default;

void ContinuityTone::setActive(bool active)
{
    m_active = false;
    Q_UNUSED(active)
}

bool ContinuityTone::available() const
{
    return false;
}

#endif

}  // namespace mybench::instruments
