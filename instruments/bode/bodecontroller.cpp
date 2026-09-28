#include "bodecontroller.h"

#include "bodemath.h"
#include "bodesweep.h"

#include <mybench/dataexport.h>
#include <mybench/devicehub.h>

#include <mydaq/dsp/units.h>

#include <QDateTime>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QString hertz(double value)
{
    return mydaq::dsp::formatEng(value, u"Hz"_s, 4);
}

}  // namespace

BodeController::BodeController() : InstrumentController(u"bode"_s)
{
    // AO 0 (stimulus) and AI 0 + AI 1 for the whole sweep.
    setRequiredResources(ResourceArbiter::AnalogOutput | ResourceArbiter::AnalogInput);
    setSupportsSingle(false);   // Run = one sweep (ELVISmx)
    for (Reference& reference : m_references) {
        reference.gain = new plot::TraceBuffer(this);
        reference.phase = new plot::TraceBuffer(this);
    }
    m_maxGainDb = m_unityGain = m_phaseMargin = m_phaseCrossover = m_gainMargin = kNaN;
    updatePlan();
}

BodeController::~BodeController()
{
    shutdown();
}

// --- Settings -----------------------------------------------------------------------------------

void BodeController::setStartFrequency(double hertz)
{
    hertz = std::clamp(hertz, bode::kMinFrequency, bode::kMaxFrequency);
    if (hertz != m_start && std::isfinite(hertz)) {
        m_start = hertz;
        emit startFrequencyChanged();
        updatePlan();
    }
}

void BodeController::setStopFrequency(double hertz)
{
    hertz = std::clamp(hertz, bode::kMinFrequency, bode::kMaxFrequency);
    if (hertz != m_stop && std::isfinite(hertz)) {
        m_stop = hertz;
        emit stopFrequencyChanged();
        updatePlan();
    }
}

void BodeController::setStepsPerDecade(int steps)
{
    steps = std::clamp(steps, bode::kMinSteps, bode::kMaxSteps);
    if (steps != m_steps) {
        m_steps = steps;
        emit stepsPerDecadeChanged();
        updatePlan();
    }
}

void BodeController::setPeakAmplitude(double volts)
{
    volts = std::clamp(std::round(volts * 100.0) / 100.0, 0.0, bode::kMaxAmplitude);
    if (volts != m_amplitude && std::isfinite(volts)) {
        m_amplitude = volts;
        emit peakAmplitudeChanged();
    }
}

void BodeController::setPolarity(Polarity polarity)
{
    if (polarity != m_polarity) {
        m_polarity = polarity;
        emit polarityChanged();
        rebuildDisplay();   // "the inversion does not affect the acquired data"
    }
}

void BodeController::setMapping(Mapping mapping)
{
    if (mapping != m_mapping) {
        m_mapping = mapping;
        emit mappingChanged();
        rebuildDisplay();
        for (Reference& reference : m_references)
            rebuildReference(reference);
    }
}

void BodeController::setAutoscale(bool on)
{
    if (on != m_autoscale) {
        m_autoscale = on;
        emit autoscaleChanged();
    }
}

void BodeController::setCursorsOn(bool on)
{
    if (on != m_cursorsOn) {
        m_cursorsOn = on;
        // ELVISmx: the cursor starts on the first point.
        if (on)
            m_cursorIndex = 0;
        emit cursorsOnChanged();
        emit cursorChanged();
        emit readoutChanged();
    }
}

void BodeController::setSweepMode(SweepMode mode)
{
    if (mode != m_sweepMode) {
        m_sweepMode = mode;
        emit sweepModeChanged();
        updatePlan();
    }
}

void BodeController::setAveragesPerPoint(int averages)
{
    averages = std::clamp(averages, 1, bode::kMaxAveragesPerPoint);
    if (averages != m_averages) {
        m_averages = averages;
        emit averagesPerPointChanged();
        updatePlan();
    }
}

void BodeController::setLogFrequencyAxis(bool on)
{
    if (on != m_logFrequencyAxis) {
        m_logFrequencyAxis = on;
        emit logFrequencyAxisChanged();
    }
}

void BodeController::setElvisColors(bool on)
{
    if (on != m_elvisColors) {
        m_elvisColors = on;
        emit elvisColorsChanged();
    }
}

void BodeController::setUnwrapPhase(bool on)
{
    if (on != m_unwrapPhase) {
        m_unwrapPhase = on;
        emit unwrapPhaseChanged();
        rebuildDisplay();
        for (Reference& reference : m_references)
            rebuildReference(reference);
    }
}

void BodeController::setShowCutoffMarkers(bool on)
{
    if (on != m_showCutoff) {
        m_showCutoff = on;
        emit markerOptionsChanged();
    }
}

void BodeController::setShowUnityGainMarker(bool on)
{
    if (on != m_showUnityGain) {
        m_showUnityGain = on;
        emit markerOptionsChanged();
    }
}

void BodeController::setShowMarginMarkers(bool on)
{
    if (on != m_showMargins) {
        m_showMargins = on;
        emit markerOptionsChanged();
    }
}

void BodeController::setCursorIndex(int index)
{
    index = m_points.empty() ? -1 : std::clamp(index, 0, pointCount() - 1);
    if (index != m_cursorIndex) {
        m_cursorIndex = index;
        emit cursorChanged();
        emit readoutChanged();
    }
}

void BodeController::updatePlan()
{
    const std::vector<double> grid = bode::sweepFrequencies(m_start, m_stop, m_steps);
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    const double maxAiRate = device ? device->maxAiRate() : 200000.0;
    double seconds = 0;
    for (double f : grid)
        seconds += bode::estimatedPointSeconds(f, maxAiRate, m_sweepMode == Standard, m_averages);
    if (int(grid.size()) != m_plannedPoints || seconds != m_plannedSeconds) {
        m_plannedPoints = int(grid.size());
        m_plannedSeconds = seconds;
    }
    emit planChanged();
}

BodeController::SweepKey BodeController::sweepKey() const
{
    return {m_start, m_stop, m_amplitude, m_steps, m_averages, m_sweepMode};
}

// --- Results ------------------------------------------------------------------------------------

double BodeController::progress() const
{
    return m_total > 0 ? double(m_points.size()) / double(m_total) : 0.0;
}

int BodeController::clippedCount() const
{
    return int(std::count_if(m_points.begin(), m_points.end(), [](const Point& p) { return p.clipped; }));
}

double BodeController::dataMinFrequency() const
{
    return m_points.empty() ? kNaN : m_points.front().frequency;
}

double BodeController::dataMaxFrequency() const
{
    return m_points.empty() ? kNaN : m_points.back().frequency;
}

int BodeController::readoutPoint() const
{
    if (m_points.empty())
        return -1;
    if (m_cursorsOn && m_cursorIndex >= 0 && m_cursorIndex < pointCount())
        return m_cursorIndex;
    return m_lastIndex >= 0 && m_lastIndex < pointCount() ? m_lastIndex : pointCount() - 1;
}

double BodeController::readoutFrequency() const
{
    const int i = readoutPoint();
    return i >= 0 ? m_points[size_t(i)].frequency : 0.0;
}

double BodeController::readoutGain() const
{
    const int i = readoutPoint();
    return i >= 0 ? bode::pointValues(m_points[size_t(i)].response, m_polarity == Inverted).gain : 0.0;
}

double BodeController::readoutGainDb() const
{
    const int i = readoutPoint();
    return i >= 0 ? bode::pointValues(m_points[size_t(i)].response, m_polarity == Inverted).gainDb
                  : -std::numeric_limits<double>::infinity();
}

double BodeController::readoutPhase() const
{
    const int i = readoutPoint();
    if (i < 0)
        return 0.0;
    if (m_unwrapPhase && size_t(i) < m_displayPhase.size())
        return m_displayPhase[size_t(i)];
    return bode::pointValues(m_points[size_t(i)].response, m_polarity == Inverted).phase;
}

bool BodeController::readoutClipped() const
{
    const int i = readoutPoint();
    return i >= 0 && m_points[size_t(i)].clipped;
}

double BodeController::cursorFrequency() const
{
    return m_cursorIndex >= 0 && m_cursorIndex < pointCount() ? m_points[size_t(m_cursorIndex)].frequency : kNaN;
}

void BodeController::cursorLeft()
{
    if (m_cursorIndex > 0)
        setCursorIndex(m_cursorIndex - 1);
}

void BodeController::cursorRight()
{
    if (m_cursorIndex < pointCount() - 1)
        setCursorIndex(m_cursorIndex + 1);
}

void BodeController::setCursorFrequency(double hertz)
{
    if (m_points.empty() || !(hertz > 0))
        return;
    int best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < pointCount(); ++i) {
        const double d = std::abs(std::log(m_points[size_t(i)].frequency / hertz));
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    setCursorIndex(best);
}

void BodeController::remeasureCursorPoint()
{
    if (!isIdle() || isPaused() || m_cursorIndex < 0 || m_cursorIndex >= pointCount())
        return;
    m_remeasureIndex = m_cursorIndex;
    run();
    if (!isRunning())
        m_remeasureIndex = -1;   // refused
}

void BodeController::clearSweep()
{
    if (isRunning())
        return;
    m_points.clear();
    m_sweepFrequencies.clear();
    m_total = 0;
    m_cursorIndex = -1;
    m_lastIndex = -1;
    m_remainingSeconds = 0;
    setHasData(false);
    setStatusText({});
    rebuildDisplay();
    emit pointsChanged();
    emit cursorChanged();
}

// --- Sweep --------------------------------------------------------------------------------------

InstrumentController::WorkerFunction BodeController::createWorker()
{
    if (m_start > m_stop)
        throw InvalidInput(tr("Start Frequency must not be higher than Stop Frequency."));
    if (!(m_amplitude > 0))
        throw InvalidInput(tr("Peak Amplitude must be greater than 0 V."));

    struct Job {
        int index;
        double frequency;
    };
    std::vector<Job> jobs;
    const bool remeasure = m_remeasureIndex >= 0 && m_remeasureIndex < pointCount();
    const bool resume = !remeasure && isPaused() && m_sweepKey == sweepKey() && !m_sweepFrequencies.empty()
                        && m_points.size() < m_sweepFrequencies.size();
    if (remeasure) {
        const Point& p = m_points[size_t(m_remeasureIndex)];
        jobs.push_back({m_remeasureIndex, p.requested});
    } else {
        if (!resume) {
            m_sweepFrequencies = bode::sweepFrequencies(m_start, m_stop, m_steps);
            m_sweepKey = sweepKey();
            m_points.clear();   // the cursor keeps its index: it moves along as the new points arrive
            m_lastIndex = -1;
            m_clipWarned = false;
            setHasData(false);
            rebuildDisplay();
            emit cursorChanged();
        }
        for (size_t i = m_points.size(); i < m_sweepFrequencies.size(); ++i)
            jobs.push_back({int(i), m_sweepFrequencies[i]});
    }
    m_remeasureIndex = -1;
    m_remeasuring = remeasure;
    m_total = int(m_sweepFrequencies.size());
    m_firstJob = jobs.empty() ? 0 : jobs.front().index;
    m_sweepClock.start();
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    const double maxAiRate = device ? device->maxAiRate() : 200000.0;
    m_remainingSeconds = 0;
    for (const Job& job : jobs)
        m_remainingSeconds += bode::estimatedPointSeconds(job.frequency, maxAiRate, m_sweepMode == Standard, m_averages);
    emit pointsChanged();

    bode::SweepOptions options;
    options.amplitude = m_amplitude;
    options.rangeProbe = m_sweepMode == Standard;
    options.averages = m_averages;
    const int total = m_total;
    setStatusText(remeasure ? tr("Re-measuring %1").arg(hertz(jobs.front().frequency))
                            : tr("Point %1/%2").arg(m_firstJob + 1).arg(total));

    return [this, jobs, options, total, remeasure](WorkerContext& ctx) {
        bode::BodeSweeper sweeper(ctx.device(), [&ctx] { return ctx.stopRequested(); });
        for (const Job& job : jobs) {
            if (ctx.stopRequested())
                break;
            ctx.setStatus(remeasure ? tr("Re-measuring %1").arg(hertz(job.frequency))
                                    : tr("Point %1/%2 · %3").arg(job.index + 1).arg(total).arg(hertz(job.frequency)));
            const std::optional<bode::MeasuredPoint> point = sweeper.measure(job.frequency, options);
            if (!point)
                break;
            ctx.setSampleRate(point->sampleRate);
            ctx.addSamples(qint64(2) * bode::kAiSamples * options.averages);
            ctx.post([this, index = job.index, requested = job.frequency, measured = *point] {
                applyPoint(index, measured, requested);
            });
            ctx.acquisitionDone();
        }
        sweeper.finish();   // AO 0 back to 0 V before the resources are released
    };
}

void BodeController::applyPoint(int index, const bode::MeasuredPoint& measured, double requested)
{
    Point point;
    point.requested = requested;
    point.frequency = measured.frequency;
    point.response = measured.response;
    point.clipped = measured.clipped;
    if (index < pointCount())
        m_points[size_t(index)] = point;
    else
        m_points.push_back(point);
    m_lastIndex = index;

    if (point.clipped && !m_clipWarned) {
        m_clipWarned = true;
        reportWarning(tr("Response clipped at %1 — reduce the Peak Amplitude.").arg(hertz(point.frequency)));
    }

    // Remaining time: the per-point model scaled by how long the points actually took.
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    const double maxAiRate = device ? device->maxAiRate() : 200000.0;
    const bool probe = m_sweepMode == Standard;
    if (m_remeasuring) {
        m_remainingSeconds = 0;
    } else {
        double done = 0, left = 0;
        for (int i = m_firstJob; i <= index && i < int(m_sweepFrequencies.size()); ++i)
            done += bode::estimatedPointSeconds(m_sweepFrequencies[size_t(i)], maxAiRate, probe, m_averages);
        for (size_t i = size_t(index) + 1; i < m_sweepFrequencies.size(); ++i)
            left += bode::estimatedPointSeconds(m_sweepFrequencies[i], maxAiRate, probe, m_averages);
        const double elapsed = m_sweepClock.elapsed() / 1000.0;
        m_remainingSeconds = done > 0 ? left * std::clamp(elapsed / done, 0.5, 3.0) : left;
    }
    rebuildDisplay();
    emit pointsChanged();
}

void BodeController::onFinished()
{
    const bool complete = !m_sweepFrequencies.empty() && m_points.size() >= m_sweepFrequencies.size();
    m_remainingSeconds = 0;
    if (m_remeasuring)
        setStatusText(hasError() ? QString() : tr("Point re-measured"));
    else if (isPaused())
        setStatusText({});
    else if (complete)
        setStatusText(tr("Done · %1 points").arg(pointCount()));
    else if (!m_points.empty())
        setStatusText(tr("Stopped · %1/%2 points").arg(pointCount()).arg(m_total));
    else
        setStatusText({});
    m_remeasuring = false;
    if (m_cursorsOn && (m_cursorIndex < 0 || m_cursorIndex >= pointCount()) && !m_points.empty()) {
        m_cursorIndex = 0;   // ELVISmx: the cursor starts on the first point
        emit cursorChanged();
        emit readoutChanged();
    }
    emit pointsChanged();
}

// --- Display ------------------------------------------------------------------------------------

void BodeController::fillTraces(const std::vector<Point>& points, Polarity polarity, plot::TraceBuffer* gain,
                                plot::TraceBuffer* phase, plot::TraceBuffer* clippedGain,
                                plot::TraceBuffer* clippedPhase) const
{
    std::vector<double> f, g, ph, cf, cg, cph;
    f.reserve(points.size());
    for (const Point& p : points) {
        const bode::PointValues v = bode::pointValues(p.response, polarity == Inverted);
        f.push_back(p.frequency);
        g.push_back(m_mapping == Logarithmic ? v.gainDb : v.gain);
        ph.push_back(v.phase);
    }
    if (m_unwrapPhase)
        ph = bode::unwrapDegrees(ph);
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].clipped) {
            cf.push_back(f[i]);
            cg.push_back(g[i]);
            cph.push_back(ph[i]);
        }
    }
    gain->setXY(f, g);
    phase->setXY(f, ph);
    if (clippedGain)
        clippedGain->setXY(cf, cg);
    if (clippedPhase)
        clippedPhase->setXY(cf, cph);
}

void BodeController::rebuildDisplay()
{
    fillTraces(m_points, m_polarity, &m_gainTrace, &m_phaseTrace, &m_clippedGainTrace, &m_clippedPhaseTrace);

    std::vector<double> f, gdb, ph;
    for (const Point& p : m_points) {
        const bode::PointValues v = bode::pointValues(p.response, m_polarity == Inverted);
        f.push_back(p.frequency);
        gdb.push_back(v.gainDb);
        ph.push_back(v.phase);
    }
    m_displayPhase = m_unwrapPhase ? bode::unwrapDegrees(ph) : ph;
    // The sweep grid is sorted, but a re-measured point may move a little: sort for the markers.
    std::vector<size_t> order(f.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&f](size_t a, size_t b) { return f[a] < f[b]; });
    std::vector<double> sf, sg, sp;
    for (size_t i : order) {
        sf.push_back(f[i]);
        sg.push_back(gdb[i]);
        sp.push_back(ph[i]);
    }
    const bode::Markers markers = bode::computeMarkers(sf, sg, sp);
    m_maxGainDb = markers.maxGainDb;
    m_cutoff = QList<double>(markers.cutoff.begin(), markers.cutoff.end());
    m_unityGain = markers.unityGainFrequency;
    m_phaseMargin = markers.phaseMargin;
    m_phaseCrossover = markers.phaseCrossoverFrequency;
    m_gainMargin = markers.gainMargin;
    emit markersChanged();
    emit readoutChanged();
}

void BodeController::rebuildReference(Reference& reference)
{
    if (!reference.used) {
        reference.gain->clear();
        reference.phase->clear();
        return;
    }
    fillTraces(reference.points, reference.polarity, reference.gain, reference.phase, nullptr, nullptr);
}

// --- References ---------------------------------------------------------------------------------

plot::TraceBuffer* BodeController::referenceGain(int slot)
{
    return slot >= 0 && slot < kMaxReferences ? m_references[size_t(slot)].gain : nullptr;
}

plot::TraceBuffer* BodeController::referencePhase(int slot)
{
    return slot >= 0 && slot < kMaxReferences ? m_references[size_t(slot)].phase : nullptr;
}

QVariantList BodeController::references() const
{
    QVariantList list;
    for (int i = 0; i < kMaxReferences; ++i) {
        const Reference& r = m_references[size_t(i)];
        if (!r.used)
            continue;
        list.append(QVariantMap{
            {u"slot"_s, i},
            {u"name"_s, r.name},
            {u"colorIndex"_s, r.colorIndex},
            {u"visible"_s, r.visible},
            {u"points"_s, int(r.points.size())},
            {u"inverted"_s, r.polarity == Inverted},
        });
    }
    return list;
}

bool BodeController::keepAsReference(const QString& name)
{
    if (m_points.empty())
        return false;
    int slot = -1;
    std::array<bool, kMaxReferences> colorUsed{};
    for (int i = 0; i < kMaxReferences; ++i) {
        if (!m_references[size_t(i)].used) {
            if (slot < 0)
                slot = i;
        } else {
            colorUsed[size_t(m_references[size_t(i)].colorIndex % kMaxReferences)] = true;
        }
    }
    if (slot < 0) {
        reportWarning(tr("At most %1 references can be kept; delete one first.").arg(kMaxReferences));
        return false;
    }
    Reference& r = m_references[size_t(slot)];
    r.used = true;
    r.visible = true;
    r.points = m_points;
    r.polarity = m_polarity;
    r.colorIndex = 0;
    while (r.colorIndex < kMaxReferences - 1 && colorUsed[size_t(r.colorIndex)])
        ++r.colorIndex;
    int number = 1;
    for (const Reference& other : m_references)
        if (other.used)
            ++number;
    r.name = name.trimmed().isEmpty() ? tr("Reference %1").arg(number - 1) : name.trimmed();
    rebuildReference(r);
    emit referencesChanged();
    markSettingsChanged();
    return true;
}

void BodeController::renameReference(int slot, const QString& name)
{
    if (slot < 0 || slot >= kMaxReferences || !m_references[size_t(slot)].used || name.trimmed().isEmpty())
        return;
    m_references[size_t(slot)].name = name.trimmed();
    emit referencesChanged();
    markSettingsChanged();
}

void BodeController::setReferenceVisible(int slot, bool visible)
{
    if (slot < 0 || slot >= kMaxReferences || !m_references[size_t(slot)].used)
        return;
    m_references[size_t(slot)].visible = visible;
    emit referencesChanged();
    markSettingsChanged();
}

void BodeController::removeReference(int slot)
{
    if (slot < 0 || slot >= kMaxReferences || !m_references[size_t(slot)].used)
        return;
    Reference& r = m_references[size_t(slot)];
    r.used = false;
    r.points.clear();
    rebuildReference(r);
    emit referencesChanged();
    markSettingsChanged();
}

void BodeController::clearReferences()
{
    for (int i = 0; i < kMaxReferences; ++i) {
        Reference& r = m_references[size_t(i)];
        r.used = false;
        r.points.clear();
        rebuildReference(r);
    }
    emit referencesChanged();
    markSettingsChanged();
}

namespace {

void addPointColumns(DataTable& table, const std::vector<double>& f, const std::vector<double>& gain,
                     const std::vector<double>& gainDb, const std::vector<double>& phase,
                     const std::vector<double>& clipped)
{
    table.addColumn(QObject::tr("Frequency"), u"Hz"_s, f);
    table.addColumn(QObject::tr("Gain"), {}, gain);
    table.addColumn(QObject::tr("Gain"), u"dB"_s, gainDb);
    table.addColumn(QObject::tr("Phase"), u"deg"_s, phase);
    table.addColumn(QObject::tr("Clipped"), {}, clipped);
}

}  // namespace

bool BodeController::exportReference(int slot, const QUrl& file)
{
    if (slot < 0 || slot >= kMaxReferences || !m_references[size_t(slot)].used)
        return false;
    const Reference& r = m_references[size_t(slot)];
    DataTable table;
    table.title = title();
    table.instrumentId = instrumentId();
    table.addMetadata(tr("Reference"), r.name);
    table.addMetadata(tr("Op-amp signal polarity"), r.polarity == Inverted ? tr("Inverted") : tr("Normal"));
    std::vector<double> f, g, gdb, ph, clipped;
    for (const Point& p : r.points) {
        const bode::PointValues v = bode::pointValues(p.response, r.polarity == Inverted);
        f.push_back(p.frequency);
        g.push_back(v.gain);
        gdb.push_back(v.gainDb);
        ph.push_back(v.phase);
        clipped.push_back(p.clipped ? 1 : 0);
    }
    addPointColumns(table, f, g, gdb, ph, clipped);
    QString error;
    const QString path = DataExport::localPath(file);
    if (!DataExport::instance()->write(path, table, &error)) {
        reportError(tr("Reference not exported: %1").arg(error));
        return false;
    }
    return true;
}

// --- Export and persistence ---------------------------------------------------------------------

DataTable BodeController::exportTable() const
{
    DataTable table;
    const SweepKey key = m_sweepFrequencies.empty() ? sweepKey() : m_sweepKey;
    table.addMetadata(tr("Stimulus"), tr("AI 0 (AO 0), Response: AI 1"));
    table.addMetadata(tr("Start"), tr("%1 Hz, Stop: %2 Hz, Steps per decade: %3, Points: %4/%5")
                                       .arg(DataExport::formatNumber(key.start), DataExport::formatNumber(key.stop))
                                       .arg(key.steps)
                                       .arg(pointCount())
                                       .arg(m_total));
    table.addMetadata(tr("Peak amplitude"), tr("%1 V, Op-amp signal polarity: %2")
                                                .arg(QString::number(key.amplitude, 'f', 2),
                                                     m_polarity == Inverted ? tr("Inverted") : tr("Normal")));
    table.addMetadata(tr("Graph mapping"), m_mapping == Linear ? tr("Linear") : tr("Logarithmic"));
    table.addMetadata(tr("Cursors"), m_cursorsOn && m_cursorIndex >= 0 && m_cursorIndex < pointCount()
                                         ? tr("On, at %1 Hz").arg(DataExport::formatNumber(cursorFrequency()))
                                         : tr("Off"));
    table.addMetadata(tr("Sweep mode"), key.mode == Standard ? tr("Standard (range pre-measurement per point)")
                                                             : tr("Fast (MYBENCH, no pre-measurement)"));
    table.addMetadata(tr("Averages per point"), QString::number(key.averages));
    std::vector<double> f, g, gdb, ph, clipped;
    for (size_t i = 0; i < m_points.size(); ++i) {
        const Point& p = m_points[i];
        const bode::PointValues v = bode::pointValues(p.response, m_polarity == Inverted);
        f.push_back(p.frequency);
        g.push_back(v.gain);
        gdb.push_back(v.gainDb);
        ph.push_back(m_unwrapPhase && i < m_displayPhase.size() ? m_displayPhase[i] : v.phase);
        clipped.push_back(p.clipped ? 1 : 0);
    }
    addPointColumns(table, f, g, gdb, ph, clipped);
    table.extra.insert(u"amplitude"_s, key.amplitude);
    table.extra.insert(u"linear"_s, m_mapping == Linear);
    return table;
}

void BodeController::saveExtraSettings(QJsonObject& settings) const
{
    QJsonArray references;
    for (const Reference& r : m_references) {
        if (!r.used)
            continue;
        QJsonArray points;
        for (const Point& p : r.points)
            points.append(QJsonArray{p.frequency, p.response.real(), p.response.imag(), p.clipped ? 1 : 0});
        references.append(QJsonObject{
            {u"name"_s, r.name},
            {u"color"_s, r.colorIndex},
            {u"visible"_s, r.visible},
            {u"inverted"_s, r.polarity == Inverted},
            {u"points"_s, points},
        });
    }
    settings.insert(u"references"_s, references);
}

void BodeController::restoreExtraSettings(const QJsonObject& settings)
{
    for (Reference& r : m_references) {
        r.used = false;
        r.points.clear();
    }
    const QJsonArray references = settings.value(u"references"_s).toArray();
    int slot = 0;
    for (const QJsonValue& value : references) {
        if (slot >= kMaxReferences)
            break;
        const QJsonObject o = value.toObject();
        Reference& r = m_references[size_t(slot)];
        for (const QJsonValue& pv : o.value(u"points"_s).toArray()) {
            const QJsonArray a = pv.toArray();
            if (a.size() < 3)
                continue;
            Point p;
            p.frequency = p.requested = a.at(0).toDouble();
            p.response = {a.at(1).toDouble(), a.at(2).toDouble()};
            p.clipped = a.size() > 3 && a.at(3).toInt() != 0;
            if (p.frequency > 0)
                r.points.push_back(p);
        }
        if (r.points.empty())
            continue;
        r.used = true;
        r.name = o.value(u"name"_s).toString(tr("Reference %1").arg(slot + 1));
        r.colorIndex = std::clamp(o.value(u"color"_s).toInt(), 0, kMaxReferences - 1);
        r.visible = o.value(u"visible"_s).toBool(true);
        r.polarity = o.value(u"inverted"_s).toBool() ? Inverted : Normal;
        ++slot;
    }
    for (Reference& r : m_references)
        rebuildReference(r);
    emit referencesChanged();
}

namespace {

// ELVISmx text log (bode.md 7): date and time, `Amplitude: 2.00 V`, a header per Mapping and one
// tab-separated row per measured point with 3 decimals.
class BodeTxtWriter : public TableWriter
{
public:
    QString name() const override { return QObject::tr("ELVISmx text log"); }
    QStringList extensions() const override { return {u"txt"_s}; }
    bool write(const DataTable& table, const QStringList&, QIODevice& out, QString* error) const override
    {
        const DataColumn* freq = nullptr;
        const DataColumn* gain = nullptr;
        const DataColumn* gainDb = nullptr;
        const DataColumn* phase = nullptr;
        for (const DataColumn& column : table.columns) {
            if (column.name == QObject::tr("Frequency"))
                freq = &column;
            else if (column.name == QObject::tr("Gain") && column.unit.isEmpty())
                gain = &column;
            else if (column.name == QObject::tr("Gain") && column.unit == u"dB"_s)
                gainDb = &column;
            else if (column.name == QObject::tr("Phase"))
                phase = &column;
        }
        const bool linear = table.extra.value(u"linear"_s).toBool();
        const DataColumn* g = linear ? gain : gainDb;
        if (!freq || !g || !phase) {
            if (error)
                *error = QObject::tr("No sweep data to write.");
            return false;
        }
        const QDateTime now = QDateTime::currentDateTime();
        QString text = now.date().toString(Qt::ISODate) + u'\t' + now.time().toString(u"HH:mm:ss"_s) + u'\n';
        text += u"Amplitude: %1 V\n"_s.arg(table.extra.value(u"amplitude"_s).toDouble(), 0, 'f', 2);
        text += u"Freq (Hz)\t%1\tPhase (deg)\n"_s.arg(linear ? u"Gain (Linear)"_s : u"Gain (dB)"_s);
        for (size_t i = 0; i < freq->values.size(); ++i) {
            const auto cell = [i](const DataColumn* c) {
                return i < c->values.size() ? QString::number(c->values[i], 'f', 3) : QString();
            };
            text += cell(freq) + u'\t' + cell(g) + u'\t' + cell(phase) + u'\n';
        }
        return out.write(text.toUtf8()) >= 0;
    }
};

}  // namespace

void registerBode(InstrumentRegistry& registry)
{
    DataExport::instance()->registerWriter(std::make_unique<BodeTxtWriter>(), u"bode"_s);
    registry.registerInstrument({
        .id = u"bode"_s,
        .title = QObject::tr("Bode Analyzer"),
        .shortTitle = QObject::tr("Bode"),
        .description = QObject::tr("Gain and phase sweeps, AO 0 → AI 0 / AI 1"),
        .module = u"MyBench.Instruments.Bode"_s,
        .icon = u"instrument-bode"_s,
        .order = 40,
        .createController = [] { return new BodeController; },
    });
}

}  // namespace mybench::instruments
