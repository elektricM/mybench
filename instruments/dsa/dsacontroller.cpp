#include "dsacontroller.h"
#include "dsalogwriter.h"

#include <mybench/dataexport.h>
#include <mybench/devicehub.h>
#include <mybench/units.h>

#include <mydaq/dsp/units.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <utility>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace mybench::instruments {

namespace dsp = mydaq::dsp;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array kLines{200, 400, 800, 1600, 3200};   // ELVISmx Resolution (lines) ring
constexpr std::array kRanges{10.0, 2.0};                  // +/- 10V, +/- 2V
constexpr double kMinimumSpan = 405.0;                     // Hz
constexpr double kFallbackMaxRate = 200000.0;             // S/s, when no device is selected
constexpr auto kPublishInterval = 33ms;                   // display updates: at most ~30/s
constexpr size_t kPeakCount = 8;                          // rows of the peak table
constexpr double kSpectrumPeakProminence = 6.0;           // dB, cursor peak search
constexpr double kSpectrumPeakFloorMargin = 10.0;         // dB above the median line
constexpr double kBarInset = 0.12;                        // octave bars: gap, fraction of a band (log)
constexpr double kBarBottom = -1000.0;                    // octave bars: below any plot range

const std::array<mydaq::AiChannel, 4> kChannels{mydaq::AiChannel::AI0, mydaq::AiChannel::AI1,
                                                mydaq::AiChannel::AudioInLeft, mydaq::AiChannel::AudioInRight};

QString frequencyText(double hertz)
{
    return dsp::formatEng(std::isfinite(hertz) ? hertz : 0.0, u"Hz"_s, 4, false);
}

QString engText(double value, const QString& unit)
{
    return dsp::formatEng(std::isfinite(value) ? value : 0.0, unit, 4, false).replace(u'-', QChar(0x2212));
}

// Records an overflow and forgets those older than 10 s (3 within 10 s stop the run, dsa.md §5).
void countOverflow(std::deque<Clock::time_point>& overflows)
{
    const Clock::time_point now = Clock::now();
    while (!overflows.empty() && now - overflows.front() > 10s)
        overflows.pop_front();
    overflows.push_back(now);
}

}  // namespace

DsaController::DsaController() : InstrumentController(u"dsa"_s)
{
    setRequiredResources(ResourceArbiter::AnalogInput);
    m_waterfall.setCapacity(m_waterfallHistory);
    connect(this, &InstrumentController::stateChanged, this, &DsaController::updateRestartEnabled);
    connect(DeviceHub::instance(), &DeviceHub::currentChanged, this, &DsaController::acquisitionSettingsChanged);
    connect(this, &DsaController::acquisitionSettingsChanged, this, [this] {
        setSummary(isOctave() ? tr("AI %1 · 1/3 octave").arg(Units::format(expectedSampleRate(), u"S/s"_s, 3))
                              : tr("AI %1 · %2 lines").arg(Units::format(expectedSampleRate(), u"S/s"_s, 3)).arg(lines()));
    });
    // Key value (sidebar, Overview): "1.00 kHz −6.02 dBV" (fundamental), "Overall −12.3 dBV(A)".
    auto updateKeyValue = [this] {
        setKeyValue(isOctave() ? (hasOctaveResult() ? tr("Overall %1 dBV%2").arg(dsa::fixedText(octaveOverall(), 1).replace(u'-', QChar(0x2212)),
                                                                            octaveWeightingSuffix())
                                                    : QString())
                    : hasResult() && fundamentalFrequency() > 0
                        ? Units::format(fundamentalFrequency(), u"Hz"_s, 3, false) + u' ' + fundamentalPowerText()
                        : QString());
    };
    connect(this, &DsaController::resultsChanged, this, updateKeyValue);
    connect(this, &DsaController::octaveResultsChanged, this, updateKeyValue);
}

DsaController::~DsaController()
{
    shutdown();
}

// ---------------------------------------------------------------------------------------------
// Settings

void DsaController::setAnalysisMode(AnalysisMode mode)
{
    if (mode == m_analysisMode || (mode != FftAnalysis && mode != OctaveAnalysis))
        return;
    m_analysisMode = mode;
    emit analysisModeChanged();
    emit acquisitionSettingsChanged();
    emit frequencyRangeChanged();
    updateRestartEnabled();
    updateCursors();
    restart();
}

void DsaController::setSourceChannel(int channel)
{
    channel = std::clamp(channel, 0, int(kChannels.size()) - 1);
    if (channel == m_source)
        return;
    m_source = channel;
    emit sourceChannelChanged();
    emit acquisitionSettingsChanged();
    restart();
}

void DsaController::setVoltageRangeIndex(int index)
{
    index = std::clamp(index, 0, int(kRanges.size()) - 1);
    if (index == m_rangeIndex)
        return;
    m_rangeIndex = index;
    emit voltageRangeIndexChanged();
    emit acquisitionSettingsChanged();
    if (!audioSource())
        restart();
}

void DsaController::setFrequencySpan(double hertz)
{
    if (!std::isfinite(hertz))
        return;
    hertz = std::clamp(std::round(hertz), kMinimumSpan, std::floor(maximumSpan()));
    if (hertz == m_span)
        return;
    m_span = hertz;
    emit frequencySpanChanged();
    emit acquisitionSettingsChanged();
    emit frequencyRangeChanged();
    restart();
}

void DsaController::setResolutionIndex(int index)
{
    index = std::clamp(index, 0, int(kLines.size()) - 1);
    if (index == m_resolutionIndex)
        return;
    m_resolutionIndex = index;
    emit resolutionIndexChanged();
    emit acquisitionSettingsChanged();
    emit frequencyRangeChanged();
    restart();
}

void DsaController::setWindowIndex(int index)
{
    index = std::clamp(index, 0, int(dsp::windowTypes().size()) - 1);
    if (index == m_windowIndex)
        return;
    m_windowIndex = index;
    emit windowIndexChanged();
    postLiveSettings();
}

void DsaController::setAveragingMode(AveragingMode mode)
{
    if (mode == m_averagingMode || mode < NoAveraging || mode > PeakHoldAveraging)
        return;
    m_averagingMode = mode;
    emit averagingModeChanged();
    updateRestartEnabled();
    postLiveSettings();
}

void DsaController::setWeighting(Weighting weighting)
{
    if (weighting == m_weighting || (weighting != LinearWeighting && weighting != ExponentialWeighting))
        return;
    m_weighting = weighting;
    emit weightingChanged();
    updateRestartEnabled();
    postLiveSettings();
}

void DsaController::setAverages(int count)
{
    count = std::clamp(count, 1, 10000);
    if (count == m_averages)
        return;
    m_averages = count;
    emit averagesChanged();
    postLiveSettings();
}

void DsaController::setUnits(DisplayUnits units)
{
    if (units == m_units || units < LinearUnits || units > DbmUnits)
        return;
    m_units = units;
    emit unitsChanged();
    // Maximum/Minimum go to the new unit's defaults (ELVISmx), whatever the scale mode, so a
    // later switch to Manual starts from sensible values.
    const dsa::ValueRange defaults = dsa::defaultManualRange(displayFormat().unit);
    setScaleMaximum(defaults.maximum);
    setScaleMinimum(defaults.minimum);
    m_autoScale.reset();
    m_waterfall.clear();
    emit displayChanged();
    updateSpectrumDisplay(false);
}

void DsaController::setMagnitudeMode(MagnitudeMode mode)
{
    if (mode == m_magnitudeMode || (mode != RmsMagnitude && mode != PeakMagnitude))
        return;
    m_magnitudeMode = mode;
    emit magnitudeModeChanged();
    m_autoScale.reset();
    m_waterfall.clear();
    emit displayChanged();
    updateSpectrumDisplay(false);
}

void DsaController::setSpectrumDisplay(SpectrumDisplay display)
{
    if (display == m_spectrumDisplay || (display != PowerSpectrumDisplay && display != PowerSpectralDensityDisplay))
        return;
    m_spectrumDisplay = display;
    emit spectrumDisplayChanged();
    m_autoScale.reset();
    m_waterfall.clear();
    emit displayChanged();
    updateSpectrumDisplay(false);
}

void DsaController::setScaleMode(ScaleMode mode)
{
    if (mode == m_scaleMode || (mode != AutoScale && mode != ManualScale))
        return;
    m_scaleMode = mode;
    emit scaleModeChanged();
    updateScale();
}

void DsaController::setScaleMaximum(double value)
{
    if (!std::isfinite(value) || value == m_scaleMaximum)
        return;
    m_scaleMaximum = std::round(value * 1000.0) / 1000.0;   // "%.3f"
    emit scaleMaximumChanged();
    updateScale();
}

void DsaController::setScaleMinimum(double value)
{
    if (!std::isfinite(value) || value == m_scaleMinimum)
        return;
    m_scaleMinimum = std::round(value * 1000.0) / 1000.0;
    emit scaleMinimumChanged();
    updateScale();
}

void DsaController::setCursorsOn(bool on)
{
    if (on == m_cursorsOn)
        return;
    m_cursorsOn = on;
    emit cursorsOnChanged();
    updateCursors();
}

void DsaController::setCursorPlot(int cursor, CursorPlot plot)
{
    if (plot == m_cursorPlot[cursor] || (plot != SpectrumPlot && plot != TimePlot))
        return;
    m_cursorPlot[cursor] = plot;
    if (cursor == 0)
        emit cursor1PlotChanged();
    else
        emit cursor2PlotChanged();
    resetCursor(cursor);
    updateCursors();
}

void DsaController::setCursorSelect(int cursor)
{
    cursor = std::clamp(cursor, 0, 1);
    if (cursor == m_cursorSelect)
        return;
    m_cursorSelect = cursor;
    emit cursorSelectChanged();
}

void DsaController::setHarmonicMarkers(bool on)
{
    if (on == m_harmonicMarkers)
        return;
    m_harmonicMarkers = on;
    emit harmonicMarkersChanged();
}

void DsaController::setAnalysisTables(bool on)
{
    if (on == m_analysisTables)
        return;
    m_analysisTables = on;
    emit analysisTablesChanged();
}

void DsaController::setWaterfallVisible(bool on)
{
    if (on == m_waterfallVisible)
        return;
    m_waterfallVisible = on;
    m_waterfall.clear();
    emit waterfallVisibleChanged();
}

void DsaController::setWaterfallHistory(int rows)
{
    rows = std::clamp(rows, 50, 1000);
    if (rows == m_waterfallHistory)
        return;
    m_waterfallHistory = rows;
    m_waterfall.setCapacity(rows);
    emit waterfallHistoryChanged();
}

void DsaController::setMaxHold(bool on)
{
    if (on == m_maxHold)
        return;
    m_maxHold = on;
    emit maxHoldChanged();
    updateSpectrumDisplay(false);
}

void DsaController::setMinHold(bool on)
{
    if (on == m_minHold)
        return;
    m_minHold = on;
    emit minHoldChanged();
    updateSpectrumDisplay(false);
}

void DsaController::setLogFrequency(bool on)
{
    if (on == m_logFrequency)
        return;
    m_logFrequency = on;
    emit logFrequencyChanged();
    emit frequencyRangeChanged();
    updateCursors();
}

void DsaController::setOctaveAveraging(OctaveAveraging mode)
{
    if (mode == m_octaveAveraging || mode < SlowAveraging || mode > CustomAveraging)
        return;
    m_octaveAveraging = mode;
    emit octaveAveragingChanged();
    postLiveSettings();
}

void DsaController::setOctaveTimeConstant(double milliseconds)
{
    if (!std::isfinite(milliseconds))
        return;
    milliseconds = std::clamp(std::round(milliseconds), 0.0, 10000.0);
    if (milliseconds == m_octaveTimeConstant)
        return;
    m_octaveTimeConstant = milliseconds;
    emit octaveTimeConstantChanged();
    if (m_octaveAveraging == CustomAveraging)
        postLiveSettings();
}

void DsaController::setOctaveLowBand(int band)
{
    band = std::clamp(band, 0, m_octaveHighBand);   // Low Band must not exceed High Band
    if (band == m_octaveLowBand)
        return;
    m_octaveLowBand = band;
    emit octaveLowBandChanged();
    emit octaveRangeChanged();
    m_octaveAutoScale.reset();
    postLiveSettings();
    updateOctaveDisplay();
}

void DsaController::setOctaveHighBand(int band)
{
    band = std::clamp(band, m_octaveLowBand, int(octave::bands().size()) - 1);
    if (band == m_octaveHighBand)
        return;
    m_octaveHighBand = band;
    emit octaveHighBandChanged();
    emit octaveRangeChanged();
    m_octaveAutoScale.reset();
    updateOctaveDisplay();
}

void DsaController::setOctaveWeighting(OctaveWeighting weighting)
{
    if (weighting == m_octaveWeighting || weighting < ZWeighting || weighting > CWeighting)
        return;
    m_octaveWeighting = weighting;
    emit octaveWeightingChanged();
    m_octaveAutoScale.reset();
    updateOctaveDisplay();
}

// ---------------------------------------------------------------------------------------------
// Lists and derived values

QStringList DsaController::sourceLabels() const
{
    QStringList labels;
    for (mydaq::AiChannel channel : kChannels)
        labels.append(mydaq::toString(channel));
    return labels;
}

QStringList DsaController::rangeLabels() const
{
    return {u"+/- 10V"_s, u"+/- 2V"_s};
}

QStringList DsaController::resolutionLabels() const
{
    QStringList labels;
    for (int n : kLines)
        labels.append(QString::number(n));
    return labels;
}

QStringList DsaController::windowLabels() const
{
    QStringList labels;
    for (dsp::WindowType type : dsp::windowTypes())
        labels.append(dsp::windowName(type));
    return labels;
}

QStringList DsaController::octaveBandLabels() const
{
    QStringList labels;
    for (const octave::Band& band : octave::bands())
        labels.append(band.label);
    return labels;
}

bool DsaController::audioSource() const
{
    return mydaq::isAudio(kChannels[size_t(m_source)]);
}

QString DsaController::sourceName() const
{
    return mydaq::toString(kChannels[size_t(m_source)]);
}

double DsaController::maximumSpan() const
{
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    return 0.5 * (device ? device->maxAiRate() : kFallbackMaxRate);
}

int DsaController::lines() const
{
    return kLines[size_t(m_resolutionIndex)];
}

double DsaController::expectedSampleRate() const
{
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    const double requested = isOctave() ? octave::kSampleRate : std::min(2.0 * m_span, 2.0 * maximumSpan());
    return device ? device->coerceAiRate(requested) : requested;
}

double DsaController::lineSpacing() const
{
    return expectedSampleRate() / (2.0 * lines());
}

bool DsaController::weightingEnabled() const
{
    return m_averagingMode == VectorAveraging || m_averagingMode == RmsAveraging;
}

bool DsaController::restartEnabled() const
{
    return m_restartEnabled;
}

void DsaController::updateRestartEnabled()
{
    // ELVISmx: Restart is live only while running and Weighting is Linear (grayed with
    // Exponential); the modes without a weighting (None, Peak Hold) keep it off too.
    const bool enabled = isRunning() && (isOctave() || (weightingEnabled() && m_weighting == LinearWeighting));
    if (enabled != m_restartEnabled) {
        m_restartEnabled = enabled;
        emit restartEnabledChanged();
    }
}

dsp::SpectrumFormat DsaController::displayFormat() const
{
    dsp::SpectrumFormat format;
    format.unit = m_units == LinearUnits ? dsp::SpectrumUnit::Linear
                : m_units == DbmUnits    ? dsp::SpectrumUnit::Dbm
                                         : dsp::SpectrumUnit::Db;
    // Mode is grayed for dBm (ELVISmx): the stored choice is kept but does not apply.
    format.mode = m_magnitudeMode == PeakMagnitude && m_units != DbmUnits ? dsp::SpectrumMode::Peak
                                                                         : dsp::SpectrumMode::Rms;
    format.density = m_spectrumDisplay == PowerSpectralDensityDisplay;
    return format;
}

QString DsaController::magnitudeUnit() const
{
    return dsa::magnitudeUnit(displayFormat());
}

QString DsaController::levelUnit() const
{
    dsp::SpectrumFormat format = displayFormat();
    format.density = false;
    return dsa::magnitudeUnit(format);
}

QString DsaController::powerUnit() const
{
    return dsa::powerUnit(dsp::SpectrumUnit::Linear, displayFormat().mode);
}

double DsaController::frequencyMinimum() const
{
    if (!m_logFrequency)
        return 0.0;
    const double df = m_frame ? m_frame->averaged.info.df : lineSpacing();
    return std::max(df, 1e-3);
}

double DsaController::frequencyMaximum() const
{
    return m_span;
}

double DsaController::recordDuration() const
{
    if (isOctave())
        return (octave::kBlockSize - 1) / octave::kSampleRate;
    return (2.0 * lines() - 1.0) / expectedSampleRate();
}

QString DsaController::octaveWeightingSuffix() const
{
    static const std::array suffixes{u""_s, u"(A)"_s, u"(B)"_s, u"(C)"_s};
    return suffixes[size_t(m_octaveWeighting)];
}

// ---------------------------------------------------------------------------------------------
// Results

double DsaController::fundamentalFrequency() const
{
    return m_frame && m_frame->measurements.valid ? m_frame->measurements.fundamentalFrequency : 0.0;
}

double DsaController::fundamentalPower() const
{
    if (!m_frame || !m_frame->measurements.valid)
        return 0.0;
    // Always a linear power (V^2, doubled in Peak), whatever the Units (observed in ELVISmx).
    return dsa::powerValue(m_frame->measurements.fundamentalPower, dsp::SpectrumUnit::Linear, displayFormat().mode);
}

QString DsaController::fundamentalPowerText() const
{
    return dsa::levelText(fundamentalPower(), powerUnit(), false);
}

QString DsaController::fundamentalPowerValueText() const
{
    const double p = fundamentalPower();
    return (p == 0.0 || std::abs(p) >= 0.01) ? dsa::fixedText(p, 2) : QString::number(p, 'e', 2);
}

double DsaController::thd() const
{
    return m_frame ? m_frame->measurements.thdPercent : 0.0;
}

double DsaController::sinad() const
{
    return m_frame ? m_frame->measurements.sinadDb : 0.0;
}

double DsaController::snr() const
{
    return m_frame ? m_frame->measurements.snrDb : 0.0;
}

double DsaController::vpk() const
{
    return m_frame ? m_frame->measurements.vpk : 0.0;
}

double DsaController::resultLineSpacing() const
{
    return m_frame ? m_frame->averaged.info.df : lineSpacing();
}

double DsaController::resultSampleRate() const
{
    return m_frame ? m_frame->sampleRate : expectedSampleRate();
}

int DsaController::averagesDone() const
{
    return m_frame ? m_frame->count : 0;
}

int DsaController::averagesTarget() const
{
    return m_frame ? m_frame->target : dsa::recordsForSingle({{}, dspAveraging(), {}, m_averages});
}

QString DsaController::averagingText() const
{
    if (!m_frame)
        return {};
    const SpectrumFrame& f = *m_frame;
    switch (f.averaging.mode) {
    case dsp::AveragingMode::None:
        return tr("No averaging");
    case dsp::AveragingMode::PeakHold:
        return tr("Peak hold · %1").arg(f.count);
    case dsp::AveragingMode::Vector:
    case dsp::AveragingMode::Rms: {
        const QString mode = f.averaging.mode == dsp::AveragingMode::Rms ? tr("RMS") : tr("Vector");
        if (f.count < f.target)
            return tr("%1 %2/%3").arg(mode).arg(f.count).arg(f.target);
        return f.averaging.weighting == dsp::AveragingWeighting::Linear ? tr("%1 %2/%2 · complete").arg(mode).arg(f.target)
                                                                        : tr("%1 exponential · %2").arg(mode).arg(f.target);
    }
    }
    return {};
}

QString DsaController::statusFor(const SpectrumFrame& f) const
{
    switch (f.averaging.mode) {
    case dsp::AveragingMode::None:
        return tr("Running");
    case dsp::AveragingMode::PeakHold:
        return f.count < f.target ? tr("Averaging %1/%2").arg(f.count).arg(f.target)
                                  : tr("Peak hold · %1 spectra").arg(f.count);
    case dsp::AveragingMode::Vector:
    case dsp::AveragingMode::Rms:
        if (f.count < f.target)
            return tr("Averaging %1/%2").arg(f.count).arg(f.target);
        return f.complete && f.averaging.weighting == dsp::AveragingWeighting::Linear ? tr("Averaging complete")
                                                                                     : tr("Running");
    }
    return {};
}

double DsaController::octaveFrequencyMinimum() const
{
    const octave::Band& band = octave::bands()[size_t(m_octaveLowBand)];
    return band.lower / std::pow(10.0, 0.25 / 10.0);
}

double DsaController::octaveFrequencyMaximum() const
{
    const octave::Band& band = octave::bands()[size_t(m_octaveHighBand)];
    return band.upper * std::pow(10.0, 0.25 / 10.0);
}

double DsaController::octaveSettlingTime() const
{
    if (m_octaveFrame && size_t(m_octaveLowBand) < m_octaveFrame->settling.size())
        return m_octaveFrame->settling[size_t(m_octaveLowBand)];
    return octave::BandPassFilter(octave::bands()[size_t(m_octaveLowBand)].lower,
                                  octave::bands()[size_t(m_octaveLowBand)].upper,
                                  octave::bands()[size_t(m_octaveLowBand)].centre, octave::kSampleRate)
        .settlingTime();
}

dsp::AveragingMode DsaController::dspAveraging() const
{
    switch (m_averagingMode) {
    case NoAveraging: return dsp::AveragingMode::None;
    case VectorAveraging: return dsp::AveragingMode::Vector;
    case RmsAveraging: return dsp::AveragingMode::Rms;
    case PeakHoldAveraging: return dsp::AveragingMode::PeakHold;
    }
    return dsp::AveragingMode::Rms;
}

// ---------------------------------------------------------------------------------------------
// Worker

DsaController::Acquisition DsaController::acquisition() const
{
    Acquisition a;
    a.channel = kChannels[size_t(m_source)];
    a.range = kRanges[size_t(effectiveRangeIndex())];
    a.span = m_span;
    a.lines = lines();
    return a;
}

DsaController::LiveSettings DsaController::liveSettings() const
{
    LiveSettings s;
    s.averaging.window = dsp::windowTypes()[size_t(m_windowIndex)];
    s.averaging.mode = dspAveraging();
    s.averaging.weighting = m_weighting == LinearWeighting ? dsp::AveragingWeighting::Linear
                                                           : dsp::AveragingWeighting::Exponential;
    s.averaging.averages = m_averages;
    s.timeConstants = octave::timeConstants(octave::Averaging(int(m_octaveAveraging)), m_octaveTimeConstant);
    s.lowBand = m_octaveLowBand;
    return s;
}

void DsaController::postLiveSettings()
{
    if (isRunning())
        m_live->post(liveSettings());
}

void DsaController::restartAveraging()
{
    if (isRunning())
        m_restartRequests->fetch_add(1);
}

InstrumentController::WorkerFunction DsaController::createWorker()
{
    m_live->clear();
    return isOctave() ? createOctaveWorker() : createSpectrumWorker();
}

InstrumentController::WorkerFunction DsaController::createSpectrumWorker()
{
    const Acquisition acq = acquisition();
    const LiveSettings initial = liveSettings();
    return [this, acq, initial, live = m_live, restarts = m_restartRequests](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        const int64_t n = 2 * int64_t(acq.lines);
        mydaq::AiConfig config;
        config.channels = {{acq.channel, acq.range}};
        config.sampleRate = device.coerceAiRate(std::min(2.0 * acq.span, device.maxAiRate()));
        config.mode = mydaq::SampleMode::Continuous;
        config.samplesPerChannel = std::max<int64_t>(4 * n, int64_t(config.sampleRate));   // host buffer hint
        std::unique_ptr<mydaq::AiTask> task = device.createAiTask(config);
        const double fs = task->sampleRate();
        ctx.setSampleRate(fs);

        dsa::SpectrumProcessor processor(initial.averaging);
        unsigned restartsSeen = restarts->load();
        // Contiguous records read in chunks of at most 100 ms, so Stop acts quickly.
        const int64_t chunk = std::clamp<int64_t>(int64_t(fs * 0.1), 1, n);
        const int timeoutMs = int(1000.0 * double(chunk) / fs) + 2000;
        std::deque<Clock::time_point> overflows;
        std::vector<double> record;
        record.reserve(size_t(n));
        Clock::time_point lastPublish = Clock::now() - 1s;
        bool pending = false;

        auto publish = [&] {
            auto frame = std::make_shared<SpectrumFrame>();
            frame->averaged = processor.averaged();
            frame->maxHold = processor.maxHold();
            frame->minHold = processor.minHold();
            frame->time = dsa::scaledTimeDomainWindow(processor.lastRecord(), processor.settings().window);
            frame->sampleRate = fs;
            frame->span = acq.span;
            frame->lines = acq.lines;
            frame->averaging = processor.settings();
            frame->measurements = dsa::measure(frame->averaged, processor.lastRecord());
            frame->peaks = dsa::strongestPeaks(frame->averaged, kPeakCount);
            frame->count = processor.count();
            frame->target = processor.target();
            frame->complete = processor.averageComplete();
            frame->channel = acq.channel;
            frame->range = acq.range;
            ctx.publish([this, frame = std::shared_ptr<const SpectrumFrame>(std::move(frame))] {
                applySpectrumFrame(frame);
            });
            ctx.acquisitionDone();
            lastPublish = Clock::now();
            pending = false;
        };

        task->start();
        while (!ctx.stopRequested()) {
            if (std::optional<LiveSettings> settings = live->take())
                processor.configure(settings->averaging);
            if (const unsigned r = restarts->load(); r != restartsSeen) {
                restartsSeen = r;
                processor.restart();
            }
            try {
                while (int64_t(record.size()) < n && !ctx.stopRequested()) {
                    const int64_t want = std::min(chunk, n - int64_t(record.size()));
                    const std::vector<std::vector<double>> data = task->read(want, timeoutMs);
                    record.insert(record.end(), data[0].begin(), data[0].end());
                    ctx.addSamples(want);
                }
            } catch (const mydaq::DeviceError& error) {
                if (error.code() != mydaq::DeviceError::Overflow || ctx.stopRequested())
                    throw;
                countOverflow(overflows);
                if (overflows.size() >= 3)
                    throw;
                ctx.warn(tr("Samples were lost; averaging restarted."));
                task.reset();
                task = device.createAiTask(config);
                task->start();
                record.clear();
                processor.restart();
                continue;
            }
            if (ctx.stopRequested())
                break;
            processor.add(record, fs);
            record.clear();
            pending = true;
            const bool done = ctx.once() && processor.hasEnoughForSingle();
            if (done || Clock::now() - lastPublish >= kPublishInterval)
                publish();
            if (done)
                break;
        }
        if (pending && processor.count() > 0)
            publish();   // the display ends on the last processed record
    };
}

InstrumentController::WorkerFunction DsaController::createOctaveWorker()
{
    const Acquisition acq = acquisition();
    const LiveSettings initial = liveSettings();
    return [this, acq, initial, live = m_live, restarts = m_restartRequests](WorkerContext& ctx) {
        mydaq::Device& device = ctx.device();
        mydaq::AiConfig config;
        config.channels = {{acq.channel, acq.range}};
        config.sampleRate = device.coerceAiRate(octave::kSampleRate);
        config.mode = mydaq::SampleMode::Continuous;
        config.samplesPerChannel = int64_t(config.sampleRate);   // 1 s host buffer
        std::unique_ptr<mydaq::AiTask> task = device.createAiTask(config);
        const double fs = task->sampleRate();
        ctx.setSampleRate(fs);

        octave::OctaveFilterBank bank(fs);
        octave::TimeConstants constants = initial.timeConstants;
        int lowBand = initial.lowBand;
        bank.setTimeConstants(constants);
        std::vector<double> settling(bank.bandCount());
        for (size_t b = 0; b < settling.size(); ++b)
            settling[b] = bank.filter(b).settlingTime();
        unsigned restartsSeen = restarts->load();
        const int timeoutMs = int(1000.0 * octave::kBlockSize / fs) + 2000;
        std::deque<Clock::time_point> overflows;
        Clock::time_point lastPublish = Clock::now() - 1s;
        std::vector<double> block;
        bool pending = false;

        auto publish = [&] {
            auto frame = std::make_shared<OctaveFrame>();
            frame->meanSquare = bank.meanSquare();
            frame->settling = settling;
            frame->time = block;
            frame->sampleRate = fs;
            frame->elapsed = bank.elapsed();
            frame->channel = acq.channel;
            frame->range = acq.range;
            ctx.publish([this, frame = std::shared_ptr<const OctaveFrame>(std::move(frame))] {
                applyOctaveFrame(frame);
            });
            ctx.acquisitionDone();
            lastPublish = Clock::now();
            pending = false;
        };

        task->start();
        while (!ctx.stopRequested()) {
            if (std::optional<LiveSettings> settings = live->take()) {
                constants = settings->timeConstants;
                lowBand = settings->lowBand;
                bank.setTimeConstants(constants);
            }
            if (const unsigned r = restarts->load(); r != restartsSeen) {
                restartsSeen = r;
                bank.reset();
            }
            try {
                block = task->read(octave::kBlockSize, timeoutMs)[0];
            } catch (const mydaq::DeviceError& error) {
                if (error.code() != mydaq::DeviceError::Overflow || ctx.stopRequested())
                    throw;
                countOverflow(overflows);
                if (overflows.size() >= 3)
                    throw;
                ctx.warn(tr("Samples were lost; averaging restarted."));
                task.reset();
                task = device.createAiTask(config);
                task->start();
                bank.reset();
                continue;
            }
            ctx.addSamples(octave::kBlockSize);
            bank.process(block);
            pending = true;
            // Single: until the lowest band's filter has settled and the averager has had three
            // time constants to follow.
            const double settle = size_t(lowBand) < settling.size() ? settling[size_t(lowBand)] : 0.0;
            const bool done = ctx.once() && bank.elapsed() >= settle + 3.0 * constants.rise;
            if (done || Clock::now() - lastPublish >= kPublishInterval)
                publish();
            if (done)
                break;
        }
        if (pending)
            publish();
    };
}

void DsaController::onStarted()
{
    updateRestartEnabled();
}

void DsaController::onFinished()
{
    updateRestartEnabled();
}

// ---------------------------------------------------------------------------------------------
// Display (UI thread)

void DsaController::applySpectrumFrame(std::shared_ptr<const SpectrumFrame> frame)
{
    const bool dfChanged = !m_frame || m_frame->averaged.info.df != frame->averaged.info.df;
    m_frame = std::move(frame);
    m_frameStamp = QDateTime::currentDateTime();
    setStatusText(statusFor(*m_frame));
    m_timeTrace.setUniform(0.0, 1.0 / m_frame->sampleRate, m_frame->time);
    if (dfChanged)
        emit frequencyRangeChanged();
    updateSpectrumDisplay(true);
}

void DsaController::updateSpectrumDisplay(bool newData)
{
    if (!m_frame) {
        updateScale();
        return;
    }
    const SpectrumFrame& f = *m_frame;
    const dsp::SpectrumFormat format = displayFormat();
    const double df = f.averaged.info.df;
    m_display = dsa::formatSpectrum(f.averaged, format);
    m_spectrumTrace.setUniform(0.0, df, m_display);

    auto formatHold = [&](const std::vector<double>& powers) {
        dsp::PowerSpectrum p;
        p.info = f.averaged.info;
        p.power = powers;
        return dsa::formatSpectrum(p, format);
    };
    std::vector<double> maxDisplay, minDisplay;
    if (m_maxHold && f.maxHold.size() == f.averaged.power.size()) {
        maxDisplay = formatHold(f.maxHold);
        m_maxHoldTrace.setUniform(0.0, df, maxDisplay);
    } else {
        m_maxHoldTrace.clear();
    }
    if (m_minHold && f.minHold.size() == f.averaged.power.size()) {
        minDisplay = formatHold(f.minHold);
        m_minHoldTrace.setUniform(0.0, df, minDisplay);
    } else {
        m_minHoldTrace.clear();
    }

    // Auto scale over the lines inside the span (and the hold traces shown).
    if (m_scaleMode == AutoScale) {
        const size_t count = std::min(m_display.size(), size_t(std::floor(f.span / df)) + 1);
        std::vector<double> values(m_display.begin(), m_display.begin() + std::ptrdiff_t(count));
        if (!maxDisplay.empty())
            values.insert(values.end(), maxDisplay.begin(), maxDisplay.begin() + std::ptrdiff_t(count));
        if (!minDisplay.empty())
            values.insert(values.end(), minDisplay.begin(), minDisplay.begin() + std::ptrdiff_t(count));
        m_autoScale = dsa::autoRange(values, format.unit, m_autoScale);
    }
    updateScale();

    // Harmonics (F, H2..H10 inside the span) and the peak table, as tone levels.
    const dsa::Measurements& m = f.measurements;
    const double reference = m.valid && !m.harmonics.empty() ? m.harmonics.front().power : 0.0;
    // Noise floor: median line power inside the span. A component 10 dB above it is "significant"
    // (labelled on the plot); weaker harmonics only get an unlabelled marker.
    double floorPower = 0.0;
    {
        const size_t last = std::min(f.averaged.power.size(), size_t(std::floor(f.span / df)) + 1);
        const size_t first = std::min(last, size_t(std::max(1, f.averaged.info.lobeHalfWidth)) + 1);
        std::vector<double> lines(f.averaged.power.begin() + std::ptrdiff_t(first), f.averaged.power.begin() + std::ptrdiff_t(last));
        if (!lines.empty()) {
            std::nth_element(lines.begin(), lines.begin() + std::ptrdiff_t(lines.size() / 2), lines.end());
            floorPower = lines[lines.size() / 2] * f.averaged.info.enbw;
        }
    }
    const bool logarithmic = dsa::isLogarithmic(format.unit);
    const QString unit = levelUnit();
    auto entry = [&](double frequency, double power) {
        const double level = dsa::levelValue(power, format.unit, format.mode);
        const double dbc = dsa::relativeDb(power, reference);
        QVariantMap map;
        map[u"frequency"_s] = frequency;
        map[u"frequencyText"_s] = frequencyText(frequency);
        map[u"level"_s] = level;
        map[u"levelText"_s] = dsa::levelText(level, unit, logarithmic);
        map[u"dbc"_s] = dbc;
        map[u"dbcText"_s] = dsa::fixedText(dbc, 1) + u" dBc"_s;
        map[u"significant"_s] = power > 10.0 * floorPower;
        return map;
    };
    m_harmonicList.clear();
    if (m.valid) {
        for (const dsa::Harmonic& h : m.harmonics) {
            if (h.frequency > f.span)
                break;
            QVariantMap map = entry(h.frequency, h.power);
            map[u"order"_s] = h.order;
            map[u"name"_s] = h.order == 1 ? u"F"_s : u"H%1"_s.arg(h.order);
            m_harmonicList.append(map);
        }
    }
    m_peakList.clear();
    for (const dsa::SpectralPeak& p : f.peaks) {
        if (p.frequency <= f.span)
            m_peakList.append(entry(p.frequency, p.power));
    }

    // Spectrogram: one row per displayed spectrum, in the display units.
    if (newData && m_waterfallVisible && !m_display.empty()) {
        if (m_waterfallColumns != int(m_display.size()) || m_waterfallDf != df) {
            m_waterfallColumns = int(m_display.size());
            m_waterfallDf = df;
            m_waterfall.setColumns(0.0, df, m_waterfallColumns);
        }
        m_waterfall.appendRow(std::span<const double>(m_display));
    }

    updateCursors();
    emit resultsChanged();
}

void DsaController::updateScale()
{
    dsa::ValueRange range;
    if (m_scaleMode == ManualScale) {
        range = {std::min(m_scaleMinimum, m_scaleMaximum), std::max(m_scaleMinimum, m_scaleMaximum)};
        if (range.maximum - range.minimum <= 0.0)
            range.maximum = range.minimum + (logarithmicUnits() ? 1.0 : 1e-3);
    } else {
        range = m_autoScale.value_or(dsa::defaultManualRange(displayFormat().unit));
    }
    if (range != m_scale) {
        m_scale = range;
        emit scaleChanged();
    }
}

void DsaController::applyOctaveFrame(std::shared_ptr<const OctaveFrame> frame)
{
    m_octaveFrame = std::move(frame);
    m_octaveStamp = QDateTime::currentDateTime();
    m_timeTrace.setUniform(0.0, 1.0 / m_octaveFrame->sampleRate, m_octaveFrame->time);
    setStatusText(m_octaveFrame->elapsed >= octaveSettlingTime() ? tr("Running") : tr("Settling…"));
    updateOctaveDisplay();
}

void DsaController::updateOctaveDisplay()
{
    if (!m_octaveFrame)
        return;
    const OctaveFrame& f = *m_octaveFrame;
    const std::vector<octave::Band>& bands = octave::bands();
    const size_t available = std::min(f.meanSquare.size(), bands.size());
    const auto weighting = octave::Weighting(int(m_octaveWeighting));
    std::vector<double> xs, ys, levels;
    m_octaveLevelList.clear();
    double total = 0;
    double loudest = -std::numeric_limits<double>::infinity();
    QString loudestLabel;
    const double inset = std::pow(10.0, kBarInset / 20.0);   // bands are 1/10 decade wide
    for (size_t b = size_t(m_octaveLowBand); b <= size_t(m_octaveHighBand) && b < available; ++b) {
        const octave::Band& band = bands[b];
        const double gain = std::pow(10.0, octave::weightingDb(weighting, band.centre) / 10.0);
        const double power = f.meanSquare[b] * gain;
        const double level = power > 0.0 ? std::max(10.0 * std::log10(power), -400.0) : -400.0;
        total += power;
        levels.push_back(level);
        if (level > loudest) {
            loudest = level;
            loudestLabel = band.label;
        }
        const double lo = band.lower * inset, hi = band.upper / inset;
        for (auto [x, y] : {std::pair{lo, kBarBottom}, {lo, level}, {hi, level}, {hi, kBarBottom},
                            {hi, std::numeric_limits<double>::quiet_NaN()}}) {
            xs.push_back(x);
            ys.push_back(y);
        }
        QVariantMap map;
        map[u"label"_s] = band.label;
        map[u"centre"_s] = band.centre;
        map[u"level"_s] = level;
        map[u"levelText"_s] = dsa::fixedText(level, 1);
        m_octaveLevelList.append(map);
    }
    m_octaveTrace.setXY(xs, ys);
    m_octaveOverall = total > 0.0 ? std::max(10.0 * std::log10(total), -400.0) : 0.0;
    m_octaveLoudestBand = loudestLabel;
    m_octaveLoudestLevel = std::isfinite(loudest) ? loudest : 0.0;
    m_octaveSettled = f.elapsed >= octaveSettlingTime();
    m_octaveAutoScale = dsa::autoRange(levels, dsp::SpectrumUnit::Db, m_octaveAutoScale);
    m_octaveScale = *m_octaveAutoScale;
    emit octaveResultsChanged();
}

// ---------------------------------------------------------------------------------------------
// Cursors

double DsaController::cursorStep(int cursor) const
{
    if (m_cursorPlot[cursor] == TimePlot)
        return 1.0 / resultSampleRate();
    return resultLineSpacing();
}

double DsaController::cursorLimit(int cursor) const
{
    if (m_cursorPlot[cursor] == TimePlot) {
        if (m_frame && !m_frame->time.empty())
            return double(m_frame->time.size() - 1) / m_frame->sampleRate;
        return recordDuration();
    }
    return m_span;
}

void DsaController::resetCursor(int cursor)
{
    const double limit = cursorLimit(cursor);
    const double step = cursorStep(cursor);
    const double lower = m_cursorPlot[cursor] == SpectrumPlot && m_logFrequency ? frequencyMinimum() : 0.0;
    double x = lower + (cursor == 0 ? 0.25 : 0.75) * (limit - lower);
    if (step > 0.0)
        x = std::round(x / step) * step;
    m_cursorPosition[cursor] = std::clamp(x, 0.0, limit);
    m_cursorPlaced[cursor] = true;
}

void DsaController::moveCursor(int cursor, double position)
{
    if (cursor < 0 || cursor > 1 || !std::isfinite(position))
        return;
    const double step = cursorStep(cursor);
    if (step > 0.0)
        position = std::round(position / step) * step;
    m_cursorPosition[cursor] = std::clamp(position, 0.0, cursorLimit(cursor));
    m_cursorPlaced[cursor] = true;
    updateCursors();
}

void DsaController::stepCursor(int direction)
{
    const int c = m_cursorSelect;
    moveCursor(c, m_cursorPosition[c] + (direction < 0 ? -1.0 : 1.0) * cursorStep(c));
}

void DsaController::searchPeak(PeakAction action)
{
    if (!m_frame)
        return;
    const int c = m_cursorSelect;
    const dsa::PeakSearch search = action == HighestPeak ? dsa::PeakSearch::Highest
                                 : action == NextPeak    ? dsa::PeakSearch::NextLower
                                 : action == PeakLeft    ? dsa::PeakSearch::Left
                                                         : dsa::PeakSearch::Right;
    if (m_cursorPlot[c] == TimePlot) {
        const std::vector<double>& y = m_frame->time;
        if (y.size() < 3)
            return;
        const auto [lo, hi] = std::minmax_element(y.begin(), y.end());
        const double dt = 1.0 / m_frame->sampleRate;
        const size_t from = size_t(std::clamp(std::lround(m_cursorPosition[c] / dt), 0L, long(y.size()) - 1));
        if (auto index = dsa::searchPeak(y, search, from, std::max(0.05 * (*hi - *lo), 1e-6)))
            moveCursor(c, double(*index) * dt);
        return;
    }
    const std::vector<double> db = dsa::powersToDb(m_frame->averaged.power);
    const double df = m_frame->averaged.info.df;
    if (db.size() < 3 || !(df > 0.0))
        return;
    // Only lines inside the span, DC lobe excluded.
    const size_t lastLine = std::min(db.size(), size_t(std::floor(m_span / df)) + 1);
    const std::span<const double> inSpan(db.data(), lastLine);
    const size_t first = size_t(std::max(1, m_frame->averaged.info.lobeHalfWidth)) + 1;
    const size_t from = size_t(std::clamp(std::lround(m_cursorPosition[c] / df), 0L, long(lastLine) - 1));
    // Peaks must also stand 10 dB above the noise floor (median line), so noise ripples of an
    // averaged spectrum are skipped.
    const double minLevel = dsa::medianValue(inSpan, first) + kSpectrumPeakFloorMargin;
    if (auto index = dsa::searchPeak(inSpan, search, from, kSpectrumPeakProminence, first, minLevel))
        moveCursor(c, double(*index) * df);
}

void DsaController::updateCursors()
{
    QString text[2];
    double y[2] = {0, 0};
    bool valid[2] = {false, false};
    if (m_cursorsOn && !isOctave()) {
        for (int c = 0; c < 2; ++c) {
            if (!m_cursorPlaced[c])
                resetCursor(c);
            m_cursorPosition[c] = std::clamp(m_cursorPosition[c], 0.0, cursorLimit(c));
            const double x = m_cursorPosition[c];
            if (m_cursorPlot[c] == SpectrumPlot) {
                const double df = resultLineSpacing();
                if (!m_display.empty() && df > 0.0) {
                    const size_t bin = size_t(std::clamp(std::lround(x / df), 0L, long(m_display.size()) - 1));
                    y[c] = m_display[bin];
                    valid[c] = true;
                }
                text[c] = frequencyText(x) + u", "_s
                        + (valid[c] ? dsa::levelText(y[c], magnitudeUnit(), logarithmicUnits()) : u"—"_s);
            } else {
                if (m_frame && !m_frame->time.empty()) {
                    const double dt = 1.0 / m_frame->sampleRate;
                    const size_t i = size_t(std::clamp(std::lround(x / dt), 0L, long(m_frame->time.size()) - 1));
                    y[c] = m_frame->time[i];
                    valid[c] = true;
                }
                text[c] = engText(x, u"s"_s) + u", "_s + (valid[c] ? engText(y[c], u"V"_s) : u"—"_s);
            }
        }
    }
    QString delta;
    if (m_cursorsOn && !isOctave() && m_cursorPlot[0] == m_cursorPlot[1]) {
        const double dx = m_cursorPosition[1] - m_cursorPosition[0];
        const bool time = m_cursorPlot[0] == TimePlot;
        delta = u"Δx "_s + (time ? engText(dx, u"s"_s) : engText(dx, u"Hz"_s));
        if (valid[0] && valid[1]) {
            const double dy = y[1] - y[0];
            delta += u" · Δy "_s;
            if (time)
                delta += engText(dy, u"V"_s);
            else if (logarithmicUnits())
                delta += dsa::fixedText(dy, 2) + u" dB"_s;
            else
                delta += dsa::levelText(dy, magnitudeUnit(), false);
        }
        if (time && dx != 0.0)
            delta += u" · 1/Δx "_s + engText(1.0 / std::abs(dx), u"Hz"_s);
    }
    m_cursorText[0] = text[0];
    m_cursorText[1] = text[1];
    m_cursorDeltaText = delta;
    emit cursorsChanged();
}

// ---------------------------------------------------------------------------------------------
// Export (ELVISmx Log): dsa.md §7

namespace {

// Data for the ELVISmx-style text log writer; t0 = the block's start (arrival minus its length).
void setLogExtra(DataTable& table, const QDateTime& arrival, double duration, double dt, const QString& units)
{
    table.extra[QString::fromLatin1(dsa::kLogT0)] = arrival.addMSecs(-qint64(std::llround(duration * 1000.0)));
    table.extra[QString::fromLatin1(dsa::kLogDt)] = dt;
    table.extra[QString::fromLatin1(dsa::kLogUnits)] = units;
}

}  // namespace

DataTable DsaController::exportTable() const
{
    DataTable table;
    if (isOctave()) {
        if (!m_octaveFrame)
            return table;
        const OctaveFrame& f = *m_octaveFrame;
        table.addMetadata(tr("Source"), tr("%1, range ±%2 V, sample rate %3 S/s")
                                            .arg(mydaq::toString(f.channel)).arg(f.range).arg(f.sampleRate));
        static const std::array averagingNames{tr("Slow"), tr("Fast"), tr("Impulse"), tr("Custom")};
        static const std::array weightingNames{tr("Linear"), tr("A"), tr("B"), tr("C")};
        table.addMetadata(tr("Analysis"), tr("1/3 octave, bands %1 … %2")
                                              .arg(octave::bands()[size_t(m_octaveLowBand)].label,
                                                   octave::bands()[size_t(m_octaveHighBand)].label));
        table.addMetadata(tr("Averaging"), m_octaveAveraging == CustomAveraging
                                               ? tr("%1, time constant %2 ms").arg(averagingNames[3]).arg(m_octaveTimeConstant)
                                               : averagingNames[size_t(m_octaveAveraging)]);
        table.addMetadata(tr("Weighting"), weightingNames[size_t(m_octaveWeighting)]);
        table.addMetadata(tr("Overall"), tr("%1 dBV%2; settled: %3")
                                             .arg(dsa::fixedText(m_octaveOverall, 2), octaveWeightingSuffix(),
                                                  m_octaveSettled ? tr("yes") : tr("no")));
        std::vector<double> centres, levels;
        for (const QVariant& v : m_octaveLevelList) {
            const QVariantMap map = v.toMap();
            centres.push_back(map.value(u"centre"_s).toDouble());
            levels.push_back(map.value(u"level"_s).toDouble());
        }
        table.addColumn(tr("Band centre"), u"Hz"_s, std::move(centres));
        table.addColumn(tr("Level"), u"dBV"_s + octaveWeightingSuffix(), std::move(levels));
        std::vector<double> t(f.time.size());
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = double(i) / f.sampleRate;
        table.addColumn(tr("Time"), u"s"_s, std::move(t));
        table.addColumn(tr("Amplitude"), u"V"_s, f.time);
        setLogExtra(table, m_octaveStamp, f.time.size() / f.sampleRate, 1.0 / f.sampleRate, u"dBV"_s + octaveWeightingSuffix());
        return table;
    }

    if (!m_frame)
        return table;
    const SpectrumFrame& f = *m_frame;
    const dsa::Measurements& m = f.measurements;
    static const std::array modeNames{tr("None"), tr("Vector"), tr("RMS"), tr("Peak Hold")};
    table.addMetadata(tr("Source"), tr("%1, range ±%2 V, sample rate %3 S/s")
                                        .arg(mydaq::toString(f.channel)).arg(f.range).arg(f.sampleRate));
    table.addMetadata(tr("Frequency span"), tr("%1 Hz, lines: %2, window: %3")
                                                .arg(f.span).arg(f.lines).arg(dsp::windowName(f.averaging.window)));
    table.addMetadata(tr("Averaging"), tr("%1, %2, %3 averages (%4 done)")
                                           .arg(modeNames[size_t(f.averaging.mode)],
                                                f.averaging.weighting == dsp::AveragingWeighting::Linear ? tr("Linear")
                                                                                                         : tr("Exponential"))
                                           .arg(f.averaging.averages)
                                           .arg(f.count));
    table.addMetadata(tr("Display"), tr("%1, units %2")
                                         .arg(m_spectrumDisplay == PowerSpectrumDisplay ? tr("Power Spectrum")
                                                                                        : tr("Power Spectral Density"),
                                              magnitudeUnit()));
    table.addMetadata(tr("Fundamental"), tr("%1 Hz, power %2 %3; THD %4 %; SINAD %5 dB; Vpk %6 V")
                                             .arg(dsa::fixedText(fundamentalFrequency(), 2),
                                                  dsa::fixedText(fundamentalPower(), 6), powerUnit(),
                                                  dsa::fixedText(m.thdPercent, 2), dsa::fixedText(m.sinadDb, 2),
                                                  dsa::fixedText(m.vpk, 3)));
    const double df = f.averaged.info.df;
    std::vector<double> frequencies(m_display.size());
    for (size_t k = 0; k < frequencies.size(); ++k)
        frequencies[k] = double(k) * df;
    table.addColumn(tr("Frequency"), u"Hz"_s, std::move(frequencies));
    table.addColumn(tr("Magnitude"), magnitudeUnit(), m_display);
    const dsp::SpectrumFormat format = displayFormat();
    auto formatted = [&](const std::vector<double>& powers) {
        dsp::PowerSpectrum p;
        p.info = f.averaged.info;
        p.power = powers;
        return dsa::formatSpectrum(p, format);
    };
    if (m_maxHold && f.maxHold.size() == m_display.size())
        table.addColumn(tr("Max hold"), magnitudeUnit(), formatted(f.maxHold));
    if (m_minHold && f.minHold.size() == m_display.size())
        table.addColumn(tr("Min hold"), magnitudeUnit(), formatted(f.minHold));
    std::vector<double> t(f.time.size());
    for (size_t i = 0; i < t.size(); ++i)
        t[i] = double(i) / f.sampleRate;
    table.addColumn(tr("Time"), u"s"_s, std::move(t));
    table.addColumn(tr("Amplitude"), u"V"_s, f.time);
    setLogExtra(table, m_frameStamp, double(f.time.size()) / f.sampleRate, 1.0 / f.sampleRate, magnitudeUnit());
    table.extra[QString::fromLatin1(dsa::kLogLines)] = f.lines;
    return table;
}

void registerDsa(InstrumentRegistry& registry)
{
    static bool writerRegistered = false;
    if (!std::exchange(writerRegistered, true))
        DataExport::instance()->registerWriter(std::make_unique<dsa::LogWriter>(), u"dsa"_s);
    registry.registerInstrument({
        .id = u"dsa"_s,
        .title = QObject::tr("Spectrum Analyzer"),
        .shortTitle = QObject::tr("Spec"),
        .description = QObject::tr("FFT spectrum, THD, SINAD, averaging, 1/3 octave"),
        .module = u"MyBench.Instruments.Dsa"_s,
        .icon = u"instrument-dsa"_s,
        .order = 50,
        .group = QStringLiteral("measure"),
        .createController = [] { return new DsaController; },
    });
}

}  // namespace mybench::instruments
