#include "genchannel.h"

#include <mydaq/dsp/spectrum.h>
#include <mydaq/dsp/units.h>

#include <QFileInfo>
#include <QMetaProperty>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

constexpr double kMinFrequency = 0.2;
constexpr double kMaxFrequency = 20000.0;
constexpr double kSpectrumFloor = -140.0;   // dBV
constexpr double kMaxGain = 10.0;

// The myDAQ derives its sample clocks from 100 MHz / integer divisor (docs/device.md).
double coerceToTimebase(double rate, double maxRate)
{
    if (!(rate > 0))
        return rate;
    const double divisor = std::max(1.0, std::round(100e6 / std::min(rate, maxRate)));
    return 100e6 / divisor;
}

gen::Shape toEngine(GenChannel::Shape shape)
{
    switch (shape) {
    case GenChannel::Triangle: return gen::Shape::Triangle;
    case GenChannel::Square: return gen::Shape::Square;
    case GenChannel::Sine: break;
    }
    return gen::Shape::Sine;
}

template <typename T>
bool assign(T& member, T value)
{
    if (member == value)
        return false;
    member = value;
    return true;
}

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

}  // namespace

namespace {

// Value with an SI prefix (M, k, none, m) and a fixed number of decimals.
QString formatFixedPrefix(double value, int decimals, QLatin1StringView unit)
{
    if (!std::isfinite(value) || value <= 0)
        return QString::number(0.0, 'f', decimals) + u' ' + unit;
    static const struct {
        double scale;
        const char* prefix;
    } prefixes[] = {{1e6, "M"}, {1e3, "k"}, {1.0, ""}, {1e-3, "m"}};
    const double round = std::pow(10.0, decimals);
    for (const auto& p : prefixes) {
        const double mantissa = value / p.scale;
        // Round first so 999.99999 Hz becomes 1.0000 kHz.
        if (std::round(mantissa * round) / round >= 1.0 || p.scale == 1e-3)
            return QString::number(mantissa, 'f', decimals) + u' ' + QLatin1StringView(p.prefix) + unit;
    }
    return QString();
}

}  // namespace

QString formatFrequencyDisplay(double hz)
{
    return formatFixedPrefix(hz, 4, "Hz"_L1);
}

QString formatRateDisplay(double samplesPerSecond)
{
    return formatFixedPrefix(samplesPerSecond, 2, "S/s"_L1);
}

GenChannel::GenChannel(int index, QObject* parent) : QObject(parent), m_index(index)
{
    m_history.setMaximumCount(4000);
    updateExpected();
    updatePreview();
}

QString GenChannel::name() const
{
    return mydaq::toString(gen::aoChannel(m_pair, m_index));
}

void GenChannel::setPair(gen::OutputPair pair)
{
    if (m_pair == pair)
        return;
    m_pair = pair;
    emit nameChanged();
    if (isAudio()) {
        // AC-coupled audio outputs: +-2 V, no DC offset (docs/instruments/gen.md 2).
        m_function.amplitude = std::min(m_function.amplitude, 4.0);
        m_function.offset = 0.0;
        m_dcLevel = std::clamp(m_dcLevel, -2.0, 2.0);
        emit functionChanged();
        emit dcChanged();
    }
    changed();
}

// --- Setters ----------------------------------------------------------------------------------------

void GenChannel::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    if (m_outputOn || m_done)
        emit modeAboutToChange();
    if (mode == Off)
        m_lastActiveMode = m_mode;
    m_mode = mode;
    setErrorText({});
    emit modeChanged();
    changed();
}

void GenChannel::setShape(Shape shape)
{
    if (assign(m_function.shape, toEngine(shape))) {
        emit functionChanged();
        changed();
    }
}

void GenChannel::setFrequency(double hz)
{
    if (assign(m_function.frequency, std::clamp(finiteOr(hz, 100.0), kMinFrequency, kMaxFrequency))) {
        emit functionChanged();
        changed();
    }
}

void GenChannel::setAmplitude(double vpp)
{
    if (assign(m_function.amplitude, std::clamp(finiteOr(vpp, 1.0), 0.0, isAudio() ? 4.0 : 10.0))) {
        emit functionChanged();
        changed();
    }
}

void GenChannel::setOffset(double volts)
{
    if (assign(m_function.offset, isAudio() ? 0.0 : std::clamp(finiteOr(volts, 0.0), -5.0, 5.0))) {
        emit functionChanged();
        changed();
    }
}

void GenChannel::setDutyCycle(double percent)
{
    if (assign(m_function.dutyCycle, std::clamp(std::round(finiteOr(percent, 50.0)), 0.0, 100.0))) {
        emit functionChanged();
        changed();
    }
}

void GenChannel::setPhase(double degrees)
{
    if (assign(m_function.phase, std::clamp(finiteOr(degrees, 0.0), -180.0, 180.0))) {
        emit functionChanged();
        changed();
    }
}

void GenChannel::setSweepStart(double hz)
{
    if (assign(m_sweep.start, std::clamp(finiteOr(hz, 100.0), kMinFrequency, kMaxFrequency))) {
        emit sweepChanged();
        changed();
    }
}

void GenChannel::setSweepStop(double hz)
{
    if (assign(m_sweep.stop, std::clamp(finiteOr(hz, 1000.0), kMinFrequency, kMaxFrequency))) {
        emit sweepChanged();
        changed();
    }
}

void GenChannel::setSweepStep(double hz)
{
    if (assign(m_sweep.step, std::clamp(finiteOr(hz, 100.0), 0.0, kMaxFrequency))) {
        emit sweepChanged();
        changed();
    }
}

void GenChannel::setSweepInterval(double ms)
{
    if (assign(m_sweep.intervalMs, std::clamp(std::round(finiteOr(ms, 1000.0)), 1.0, 300000.0))) {
        emit sweepChanged();
        changed();
    }
}

void GenChannel::setSweepBlockTiming(bool on)
{
    if (assign(m_sweep.blockTiming, on)) {
        emit sweepChanged();
        changed();
    }
}

void GenChannel::setGain(double gain)
{
    // -10 ... +10 (gen.md 4.2.2); larger products still clip at the output range (4.2.4).
    if (assign(m_gain, std::clamp(finiteOr(gain, 1.0), -kMaxGain, kMaxGain))) {
        emit arbitraryChanged();
        changed();
    }
}

void GenChannel::setUpdateRate(double rate)
{
    if (assign(m_updateRate, std::clamp(finiteOr(rate, 1000.0), gen::kMinUpdateRate, m_maxRate))) {
        emit arbitraryChanged();
        emit updateRateEdited(m_updateRate);
        changed();
    }
}

void GenChannel::setGenerationMode(GenerationMode mode)
{
    if (assign(m_generationMode, mode)) {
        emit arbitraryChanged();
        changed();
    }
}

void GenChannel::setInterpolation(Interpolation interpolation)
{
    if (assign(m_interpolation, interpolation)) {
        emit arbitraryChanged();
        changed();
    }
}

void GenChannel::setZeroAfterRunOnce(bool on)
{
    if (assign(m_zeroAfterRunOnce, on)) {
        emit arbitraryChanged();
        changed();
    }
}

void GenChannel::setEnginePreference(gen::EnginePreference preference)
{
    if (m_preference == preference)
        return;
    m_preference = preference;
    updateExpected();
}

void GenChannel::setDcLevel(double volts)
{
    const double range = outputRange();
    if (assign(m_dcLevel, std::clamp(finiteOr(volts, 0.0), -range, range))) {
        emit dcChanged();
        changed();
    }
}

void GenChannel::setMaxRate(double rate)
{
    if (rate > 0 && rate != m_maxRate) {
        m_maxRate = rate;
        if (m_updateRate > rate)
            setUpdateRate(rate);
        updateExpected();
    }
}

void GenChannel::changed()
{
    emit settingChanged();
    emit summaryChanged();
    updateExpected();
    updatePreview();
}

// --- Waveform ----------------------------------------------------------------------------------------

QString GenChannel::waveformName() const
{
    return hasWaveform() ? m_source.name : tr("< empty >");
}

QString GenChannel::waveformDetails() const
{
    if (!hasWaveform())
        return tr("No waveform loaded");
    QStringList lines;
    if (!m_source.path.isEmpty())
        lines << QFileInfo(m_source.path).absoluteFilePath();
    else if (m_source.kind == Source::Document)
        lines << tr("Waveform Editor document (stored in the session)");
    lines << tr("%n samples", nullptr, waveformPoints());
    if (m_source.sampleRate > 0)
        lines << tr("Made for %1").arg(mydaq::dsp::formatEng(m_source.sampleRate, u"S/s"_s, 4));
    return lines.join(u'\n');
}

double GenChannel::waveformDuration() const
{
    return waveformPoints() / m_updateRate;
}

void GenChannel::setSource(Source source)
{
    m_source = std::move(source);
    setErrorText({});
    // Thumbnail: min/max pairs of about 600 buckets.
    std::vector<double> thumb;
    if (hasWaveform()) {
        const std::vector<double>& w = *m_source.samples;
        const size_t buckets = std::min<size_t>(w.size(), 600);
        const double per = double(w.size()) / double(buckets);
        for (size_t b = 0; b < buckets; ++b) {
            const size_t from = size_t(double(b) * per);
            const size_t to = std::max(from + 1, size_t(double(b + 1) * per));
            const auto [lo, hi] = std::minmax_element(w.begin() + qsizetype(from), w.begin() + qsizetype(std::min(to, w.size())));
            thumb.push_back(*lo);
            if (to - from > 1)
                thumb.push_back(*hi);
        }
    }
    if (thumb.empty())
        m_thumbnail.clear();
    else
        m_thumbnail.setUniform(0.0, 1.0, thumb);
    emit waveformChanged();
    emit summaryChanged();
    emit settingChanged();
    updatePreview();
}

void GenChannel::clearSource()
{
    setSource(Source{});
}

// --- State -------------------------------------------------------------------------------------------

GenChannel::State GenChannel::state() const
{
    if (m_mode == Off)
        return Inactive;
    if (m_paused && m_outputOn)
        return Paused;
    if (m_done)
        return Done;
    switch (m_engineState) {
    case gen::ChannelState::Running: return Running;
    case gen::ChannelState::Sweeping: return Sweeping;
    case gen::ChannelState::Done: return Done;
    case gen::ChannelState::Idle: break;
    }
    return Ready;
}

QString GenChannel::readoutText() const
{
    switch (state()) {
    case Inactive:
    case Ready: return tr("OFF");
    case Paused: return tr("PAUSED");
    case Done: return tr("Done");
    case Running:
    case Sweeping: break;
    }
    switch (m_mode) {
    case Function: return formatFrequencyDisplay(m_status.frequency);
    case Arbitrary: return formatRateDisplay(m_status.updateRate);
    case Dc: return QString::number(m_status.level, 'f', 2) + u" V"_s;
    case Off: break;
    }
    return tr("OFF");
}

QString GenChannel::readoutCaption() const
{
    const State s = state();
    if (s == Done)
        return tr("Holding %1 V").arg(QString::number(m_status.level, 'f', 3));
    if (s == Sweeping)
        return tr("Sweep step %1 of %2").arg(m_status.sweepStep).arg(m_status.sweepSteps);
    switch (m_mode) {
    case Function:
        if (s == Running && m_strategy == gen::StrategyKind::Stream)
            return tr("DDS · phase-continuous");
        if (s == Running && m_status.bufferLength > 0)
            return tr("Buffer %1 · actual %2").arg(tr("%n samples", nullptr, m_status.bufferLength),
                                                   formatFrequencyDisplay(m_status.bufferFrequency));
        if (std::abs(m_expectedFrequency - m_function.frequency) > 1e-9 * m_function.frequency)
            return tr("Actual %1").arg(formatFrequencyDisplay(m_expectedFrequency));
        return tr("Ready");
    case Arbitrary:
        if (!hasWaveform())
            return tr("No waveform");
        return tr("%1 · %2").arg(m_generationMode != RunOnce ? tr("Run Continuously")
                                 : m_zeroAfterRunOnce              ? tr("Run Once, then 0 V")
                                                                   : tr("Run Once"),
                                 tr("%n points", nullptr, waveformPoints()));
    case Dc:
        if (s != Running)
            return tr("Ready");
        return m_strategy == gen::StrategyKind::Static ? tr("on-demand output") : tr("in the AO task");
    case Off: break;
    }
    return {};
}

double GenChannel::sweepProgress() const
{
    return m_status.sweepSteps > 0 ? double(m_status.sweepStep) / double(m_status.sweepSteps) : 0.0;
}

void GenChannel::setErrorText(const QString& text)
{
    if (m_errorText == text)
        return;
    m_errorText = text;
    emit errorTextChanged();
}

QString GenChannel::summary() const
{
    static const char* shapes[] = {QT_TR_NOOP("sine"), QT_TR_NOOP("triangle"), QT_TR_NOOP("square")};
    switch (m_mode) {
    case Off: return tr("Off");
    case Function:
        return tr("%1 %2 · %3").arg(mydaq::dsp::formatEng(m_function.frequency, u"Hz"_s, 4),
                                    tr(shapes[int(shape())]),
                                    QString::number(m_function.amplitude, 'f', 2) + u" Vpp"_s);
    case Arbitrary:
        return hasWaveform() ? tr("%1 · %2").arg(m_source.name, mydaq::dsp::formatEng(m_updateRate, u"S/s"_s, 4))
                             : tr("Arbitrary · no waveform");
    case Dc: return tr("DC %1 V").arg(QString::number(m_dcLevel, 'f', 2));
    }
    return {};
}

gen::ChannelSettings GenChannel::engineSettings() const
{
    gen::ChannelSettings s;
    switch (m_mode) {
    case Off: s.mode = gen::ChannelMode::Off; break;
    case Function: s.mode = gen::ChannelMode::Function; break;
    case Arbitrary: s.mode = gen::ChannelMode::Arbitrary; break;
    case Dc: s.mode = gen::ChannelMode::Dc; break;
    }
    s.active = m_outputOn && !m_paused;
    s.sweep = m_sweepRequested;
    s.startId = m_startId;
    s.function = m_function;
    s.sweepSettings = m_sweep;
    s.arb.waveform = m_source.samples;
    s.arb.gain = m_gain;
    s.arb.updateRate = m_updateRate;
    s.arb.once = m_generationMode == RunOnce;
    s.arb.linear = m_interpolation == Linear;
    s.arb.zeroAfterOnce = m_zeroAfterRunOnce;
    s.dcLevel = m_dcLevel;
    return s;
}

void GenChannel::setOutput(bool on, bool sweep, uint64_t startId)
{
    m_outputOn = on;
    m_sweepRequested = on && sweep;
    m_paused = false;
    m_done = false;
    if (on) {
        m_startId = startId;
        if (m_mode == Dc)
            clearHistory();
    } else {
        m_engineState = gen::ChannelState::Idle;
        m_status = {};
    }
    updateExpected();
    emit stateChanged();
    emit readoutChanged();
    updatePreview();
}

void GenChannel::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    if (paused) {
        m_engineState = gen::ChannelState::Idle;
        m_status = {};
    }
    emit stateChanged();
    emit readoutChanged();
}

void GenChannel::applyStatus(const gen::ChannelStatus& status, gen::StrategyKind strategy)
{
    if (status.startId != m_startId && status.state != gen::ChannelState::Idle)
        return;   // an older run of this channel
    const bool sweepStepChanged = status.sweepStep != m_status.sweepStep;
    const gen::ChannelState previous = m_engineState;
    m_status = status;
    m_strategy = strategy;
    m_engineState = m_outputOn ? status.state : gen::ChannelState::Idle;
    if (previous != m_engineState)
        emit stateChanged();
    updateExpected();
    emit readoutChanged();
    if (sweepStepChanged && m_sweepRequested)
        updatePreview();   // follows the current sweep frequency
}

void GenChannel::setDone(bool done, double level)
{
    m_done = done;
    if (done) {
        m_status.level = level;
        m_engineState = gen::ChannelState::Idle;
    }
    emit stateChanged();
    emit readoutChanged();
}

void GenChannel::setTaskRate(double rate)
{
    if (m_taskRate == rate)
        return;
    m_taskRate = rate;
    updateExpected();
    emit readoutChanged();
    if (m_mode == Function)
        updatePreview();   // coarse output samples follow the task rate
}

void GenChannel::updateExpected()
{
    // What the output really produces: the stream's DDS frequency (exact for display), or the
    // frequency of the ELVISmx looped buffer (Regenerate), which can differ slightly.
    double expected = m_function.frequency;
    if (generating() && m_status.bufferFrequency > 0) {
        expected = m_status.bufferFrequency;
    } else if (!generating() && m_preference == gen::EnginePreference::Regenerate) {
        const gen::FgenPlan plan = gen::planFgenBuffer(m_function.frequency, m_maxRate,
                                                       [this](double r) { return coerceToTimebase(r, m_maxRate); });
        expected = plan.frequency;
    }
    if (expected != m_expectedFrequency) {
        m_expectedFrequency = expected;
        emit readoutChanged();
    }
}

// --- Previews ----------------------------------------------------------------------------------------

void GenChannel::setSpectrumVisible(bool visible)
{
    if (m_spectrumVisible == visible)
        return;
    m_spectrumVisible = visible;
    updateSpectrum();
}

double GenChannel::naturalSpan() const
{
    switch (m_mode) {
    case Function: {
        const double f = (m_sweepRequested && m_status.frequency > 0) ? m_status.frequency : m_function.frequency;
        return 2.0 / f;
    }
    case Arbitrary:
        return hasWaveform() ? double(m_source.samples->size()) / m_updateRate : 0.0;
    case Off:
    case Dc:
        break;
    }
    return 0.0;
}

void GenChannel::setPreviewWindow(double seconds)
{
    seconds = std::isfinite(seconds) && seconds > 0 ? seconds : 0.0;
    if (seconds == m_previewWindow)
        return;
    m_previewWindow = seconds;
    updatePreview();
}

void GenChannel::updatePreview()
{
    // Above this many samples a preview is drawn as its envelope (a band between the lowest and the
    // highest level): at that compression the plot could only show aliasing, not the shape.
    constexpr size_t kMaxPreviewSamples = 200000;
    const double range = outputRange();
    const double own = naturalSpan();
    double lo = -1, hi = 1;
    bool step = false;
    double span = m_previewWindow > 0 ? m_previewWindow : (own > 0 ? own : 1.0);
    int clipped = 0;
    // An envelope band: alternate the extremes over the window.
    const auto band = [&](double bandLo, double bandHi) {
        constexpr size_t n = 2000;
        std::vector<double> y(n);
        for (size_t i = 0; i < n; ++i)
            y[i] = (i % 2) ? bandHi : bandLo;
        m_preview.setUniform(0.0, span / double(n - 1), y);
    };
    switch (m_mode) {
    case Off:
        m_preview.clear();
        break;
    case Function: {
        const double f = (m_sweepRequested && m_status.frequency > 0) ? m_status.frequency : m_function.frequency;
        const double periods = span * f;
        const double clampedLo = std::clamp(m_function.offset - m_function.amplitude / 2, -range, range);
        const double clampedHi = std::clamp(m_function.offset + m_function.amplitude / 2, -range, range);
        // Samples of the real output when they are coarse (ELVISmx buffer at high frequencies).
        const double taskRate = m_taskRate > 0 ? m_taskRate : m_maxRate;
        const double perPeriod = taskRate / f;
        double n;
        double dt;
        if (perPeriod < 100) {
            step = true;
            dt = 1.0 / taskRate;
            n = std::ceil(span / dt) + 1;
        } else {
            n = std::max(801.0, std::ceil(periods * 400.0) + 1);
            dt = span / (n - 1);
        }
        if (n > double(kMaxPreviewSamples)) {
            step = false;
            band(clampedLo, clampedHi);
        } else {
            std::vector<double> y(static_cast<size_t>(n));
            const double duty = m_function.dutyCycle / 100.0;
            for (size_t i = 0; i < y.size(); ++i) {
                const double cycles = f * dt * double(i) + m_function.phase / 360.0;
                y[i] = std::clamp(m_function.offset + m_function.amplitude / 2 * gen::shapeValue(m_function.shape, cycles, duty),
                                  -range, range);
            }
            m_preview.setUniform(0.0, dt, y);
        }
        lo = m_function.offset - m_function.amplitude / 2;
        hi = m_function.offset + m_function.amplitude / 2;
        break;
    }
    case Arbitrary:
        if (!hasWaveform()) {
            m_preview.clear();
            break;
        } else {
            const std::vector<double>& w = *m_source.samples;
            std::vector<double> pass(w.size());
            lo = hi = std::clamp(m_gain * w[0], -range, range);
            for (size_t i = 0; i < w.size(); ++i) {
                const double v = m_gain * w[i];
                pass[i] = std::clamp(v, -range, range);
                clipped += std::abs(v) > range;
                lo = std::min(lo, pass[i]);
                hi = std::max(hi, pass[i]);
            }
            const double dt = 1.0 / m_updateRate;
            const size_t total = size_t(std::max(1.0, std::ceil(span / dt - 1e-9)));
            step = w.size() < 400;
            if (total <= w.size()) {
                m_preview.setUniform(0.0, dt, pass);   // one pass fills the window
            } else {
                // Continuous: the waveform repeats; Run Once: it holds its last sample (or 0 V).
                const bool once = m_generationMode == RunOnce;
                const double after = once ? (m_zeroAfterRunOnce ? 0.0 : pass.back()) : 0.0;
                if (once) {
                    lo = std::min(lo, after);
                    hi = std::max(hi, after);
                }
                if (total > kMaxPreviewSamples) {
                    step = false;
                    band(lo, hi);
                } else {
                    std::vector<double> y(total);
                    for (size_t i = 0; i < total; ++i)
                        y[i] = once ? (i < pass.size() ? pass[i] : after) : pass[i % pass.size()];
                    m_preview.setUniform(0.0, dt, y);
                }
            }
        }
        break;
    case Dc: {
        const std::vector<double> y{m_dcLevel, m_dcLevel};
        m_preview.setUniform(0.0, span, y);
        lo = hi = m_dcLevel;
        break;
    }
    }
    // Axis window: the data with 10 % margin; a flat waveform gets +-1 V around its value.
    if (hi - lo < 1e-9) {
        lo -= 1.0;
        hi += 1.0;
    } else {
        const double margin = (hi - lo) * 0.1;
        lo -= margin;
        hi += margin;
    }
    m_previewSpan = span;
    m_previewMin = lo;
    m_previewMax = hi;
    m_previewStep = step;
    m_clipped = clipped;
    emit previewChanged();
    updateSpectrum();
    if (own != m_lastNaturalSpan) {
        m_lastNaturalSpan = own;
        emit naturalSpanChanged();
    }
}

void GenChannel::updateSpectrum()
{
    if (!m_spectrumVisible || m_mode == Off || (m_mode == Arbitrary && !hasWaveform())) {
        m_spectrum.clear();
        return;
    }
    std::vector<double> y;
    double rate = 1.0;
    const double range = outputRange();
    if (m_mode == Function) {
        // 64 periods of 64 samples: every harmonic falls on a bin (up to the 31st).
        const double f = (m_sweepRequested && m_status.frequency > 0) ? m_status.frequency : m_function.frequency;
        rate = 64.0 * f;
        y.resize(4096);
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = std::clamp(m_function.offset + m_function.amplitude / 2
                                  * gen::shapeValue(m_function.shape, double(i) / 64.0 + m_function.phase / 360.0,
                                                    m_function.dutyCycle / 100.0),
                              -range, range);
        m_spectrumSpan = 10.5 * f;
    } else if (m_mode == Arbitrary) {
        const std::vector<double>& w = *m_source.samples;
        const size_t n = std::min<size_t>(w.size(), 1 << 20);
        y.resize(std::max<size_t>(n, 2));
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = std::clamp(m_gain * w[i % w.size()], -range, range);
        rate = m_updateRate;
        m_spectrumSpan = rate / 2;
    } else {
        y.assign(64, m_dcLevel);
        rate = 64;
        m_spectrumSpan = 10;
    }
    const mydaq::dsp::PowerSpectrum ps = mydaq::dsp::powerSpectrum(y, rate, mydaq::dsp::WindowType::None);
    std::vector<double> db = mydaq::dsp::formatSpectrum(ps, {});
    for (double& v : db)
        v = std::max(v, kSpectrumFloor);
    m_spectrum.setUniform(0.0, ps.info.df, db);
    emit previewChanged();
}

void GenChannel::appendHistory(double t)
{
    const double level = generating() && m_mode == Dc ? m_status.level : 0.0;
    m_history.append(t, level);
    m_lastHistory = t;
    emit historyChanged();
}

void GenChannel::clearHistory()
{
    m_history.clear();
    m_lastHistory = -1;
    emit historyChanged();
}

QList<QMetaProperty> GenChannel::settingProperties()
{
    QList<QMetaProperty> list;
    const QMetaObject& mo = staticMetaObject;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
        const QMetaProperty p = mo.property(i);
        if (p.isWritable() && p.isStored() && p.hasNotifySignal())
            list.append(p);
    }
    return list;
}

}  // namespace mybench::instruments
