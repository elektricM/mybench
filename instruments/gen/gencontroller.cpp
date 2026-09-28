#include "gencontroller.h"

#include "genfileio.h"

#include <mybench/devicehub.h>
#include <mybench/notifications.h>
#include <mybench/resourcearbiter.h>
#include <mybench/settingsjson.h>

#include <mydaq/dsp/units.h>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mybench::instruments {

namespace {

const QString kPresetsKey = u"instruments/gen/presets"_s;
const QString kRecentKey = u"instruments/gen/recentFiles"_s;
const QString kLastFolderKey = u"instruments/gen/lastWaveformFolder"_s;


// Adapts the worker context to the engine.
class WorkerHost final : public gen::EngineHost
{
public:
    WorkerHost(WorkerContext& ctx, std::shared_ptr<Mailbox<gen::EngineSettings>> live,
               std::function<void(const gen::EngineStatus&)> apply)
        : m_ctx(ctx), m_live(std::move(live)), m_apply(std::move(apply))
    {
    }
    bool stopRequested() const override { return m_ctx.stopRequested(); }
    bool sleepMs(int ms) override { return m_ctx.sleepMs(ms); }
    std::optional<gen::EngineSettings> takeSettings() override { return m_live->take(); }
    void publishStatus(const gen::EngineStatus& status) override
    {
        // In order (not coalesced): sweep ends and Done states must not be lost.
        m_ctx.post([apply = m_apply, status] { apply(status); });
        if (status.taskRate > 0)
            m_ctx.setSampleRate(status.taskRate);
    }
    void warn(const QString& message) override { m_ctx.warn(message); }
    void samplesWritten(int64_t samples) override { m_ctx.addSamples(samples); }

private:
    WorkerContext& m_ctx;
    std::shared_ptr<Mailbox<gen::EngineSettings>> m_live;
    std::function<void(const gen::EngineStatus&)> m_apply;
};

GenChannel::Source documentSource(const gen::Document& doc, const QString& path)
{
    GenChannel::Source source;
    source.kind = GenChannel::Source::Document;
    source.path = path;
    source.name = path.isEmpty() ? doc.name : QFileInfo(path).fileName();
    source.document = doc;
    source.sampleRate = doc.sampleRate;
    source.samples = std::make_shared<const std::vector<double>>(gen::render(doc).samples);
    return source;
}

// A ready-made library document: one segment of 10 ms at 100 kS/s, frequencies of 100 Hz (one
// cycle per segment) for the periodic functions, 100 Hz -> 1 kHz for the sweeps.
gen::Document libraryDocument(gen::LibraryFunction function)
{
    gen::Document doc;
    doc.name = gen::functionName(function);
    doc.sampleRate = 100000;
    gen::Component c = gen::Component::library(function);
    const gen::FunctionInfo& info = gen::functionInfo(function);
    for (size_t i = 0; i < info.params.size(); ++i) {
        const QLatin1StringView key(info.params[i].key);
        if (key == "frequency" || key == "startFrequency")
            c.params[i] = 100;
        else if (key == "stopFrequency")
            c.params[i] = 1000;
    }
    doc.segments = {gen::Segment{QString(), 0.01, 1.0, {c}}};
    return doc;
}

}  // namespace

GenController::GenController() : InstrumentController(u"gen"_s)
{
    setRequiredResources(ResourceArbiter::AnalogOutput);
    setSupportsSingle(true);   // Single: plays the Arbitrary channels once
    setHasData(true);          // the channel waveforms can always be exported

    for (int i = 0; i < 2; ++i) {
        m_channels[size_t(i)] = std::make_unique<GenChannel>(i);
        GenChannel* ch = m_channels[size_t(i)].get();
        connect(ch, &GenChannel::settingChanged, this, [this, i] { onChannelSettingChanged(i); });
        connect(ch, &GenChannel::modeAboutToChange, this, [this, i] {
            // Mode change while generating: the channel stops (0 V) and stays Ready.
            GenChannel* c = m_channels[size_t(i)].get();
            if (c->outputOn() || c->done())
                setOutput(i, false);
        });
        connect(ch, &GenChannel::updateRateEdited, this, [this, i](double rate) {
            if (!m_linkUpdateRates || m_syncingRates)
                return;
            m_syncingRates = true;
            m_channels[size_t(1 - i)]->setUpdateRate(rate);
            m_syncingRates = false;
        });
        connect(ch, &GenChannel::waveformChanged, this, &GenController::channelsChanged);
        connect(ch, &GenChannel::modeChanged, this, &GenController::channelsChanged);
        connect(ch, &GenChannel::stateChanged, this, &GenController::updateStatusText);
        ch->setEnginePreference(gen::EnginePreference::Auto);
        connect(ch, &GenChannel::readoutChanged, this, &GenController::updateStatusText);
    }
    // ELVISmx FGEN defaults on AO 0 (sine, 100 Hz, 1 Vpp, 0 V, 50 %); AO 1 Off.
    m_channels[0]->setMode(GenChannel::Function);

    connect(&m_editor, &WaveformEditor::useInChannelRequested, this, &GenController::onEditorUse);
    connect(&m_editor, &WaveformEditor::rendered, this, &GenController::onEditorRendered);
    connect(&m_editor, &WaveformEditor::message, this,
            [this](int level, const QString& title, const QString& text) { notify(level, title, text); });

    auto updateMaxRate = [this] {
        const double rate = DeviceHub::instance()->maxAoRate();
        for (auto& ch : m_channels)
            ch->setMaxRate(rate > 0 ? rate : 200000.0);
    };
    connect(DeviceHub::instance(), &DeviceHub::currentChanged, this, updateMaxRate);
    updateMaxRate();

    m_historyTimer.setInterval(100);
    connect(&m_historyTimer, &QTimer::timeout, this, [this] {
        const double t = double(m_runClock.elapsed()) / 1000.0;
        for (auto& ch : m_channels) {
            if (ch->mode() == GenChannel::Dc && ch->outputOn())
                ch->appendHistory(t);
        }
    });

    m_recentFiles = QSettings().value(kRecentKey).toStringList();
    connect(this, &GenController::channelsChanged, this, &GenController::updateHolds);
    connect(this, &InstrumentController::stateChanged, this, &GenController::updateHolds);
    addPaletteCommands();
    updateStatusText();
}

void GenController::addPaletteCommands()
{
    // The DC Level featured app from the palette: "Set AO 0 to … V" (featured-apps.md 2).
    for (int i = 0; i < 2; ++i) {
        addCommand({
            .id = u"dcLevel%1"_s.arg(i),
            .title = tr("Set AO %1 to … V").arg(i),
            .keywords = tr("dc level voltage output source"),
            .icon = u"wave-dc-line"_s,
            .argumentLabel = tr("AO %1 level (V, -10 to 10)").arg(i),
            .argumentUnit = u"V"_s,
            .run = [this, i](const QVariant& value) {
                if (m_pair == AudioOutputs)
                    setOutputPair(AnalogOutputs);   // DC needs the AO pair
                GenChannel* ch = m_channels[size_t(i)].get();
                ch->setMode(GenChannel::Dc);
                ch->setDcLevel(value.toDouble());
                if (!ch->outputOn())
                    setOutput(i, true);
            },
        });
    }
    addCommand({
        .id = u"sweep"_s,
        .title = tr("Sweep the Function channels"),
        .keywords = tr("frequency sweep"),
        .icon = u"sweep"_s,
        .run = [this](const QVariant&) { sweepAll(); },
        .enabled = [this] { return canSweep(); },
    });
}

void GenController::updateHolds()
{
    setSingleEnabled(canSingle());
    bool holding = false;
    for (const auto& ch : m_channels)
        holding = holding || (ch->done() && ch->outputOn());
    setHoldsOutputs(holding && !isRunning());
}

GenController::~GenController()
{
    shutdown();
}

GenChannel* GenController::channel(int index)
{
    return index >= 0 && index < 2 ? m_channels[size_t(index)].get() : nullptr;
}

void GenController::notify(int level, const QString& title, const QString& message)
{
    Notifications::instance()->notify(level, title, message);
}

// --- Settings ------------------------------------------------------------------------------------

void GenController::setOutputPair(OutputPair pair)
{
    if (m_pair == pair)
        return;
    m_pair = pair;
    for (auto& ch : m_channels)
        ch->setPair(pair == AudioOutputs ? gen::OutputPair::AudioOutputs : gen::OutputPair::AnalogOutputs);
    // DC levels make no sense on the AC-coupled audio outputs.
    if (pair == AudioOutputs) {
        for (auto& ch : m_channels) {
            if (ch->mode() == GenChannel::Dc)
                ch->setMode(GenChannel::Off);
        }
    }
    emit outputPairChanged();
    if (isRunning()) {
        restartEngine();
        notify(Notifications::Info, tr("Output moved to %1").arg(pair == AudioOutputs ? tr("AudioOutput Left / Right")
                                                                                       : tr("AO 0 / AO 1")));
    }
}

void GenController::setEngineMode(EngineMode mode)
{
    if (m_engineMode == mode)
        return;
    m_engineMode = mode;
    for (auto& ch : m_channels)
        ch->setEnginePreference(mode == Regenerate ? gen::EnginePreference::Regenerate
                                : mode == Stream   ? gen::EnginePreference::Stream
                                                   : gen::EnginePreference::Auto);
    emit engineModeChanged();
    if (isRunning())
        restartEngine();
}

void GenController::setLinkUpdateRates(bool link)
{
    if (m_linkUpdateRates == link)
        return;
    m_linkUpdateRates = link;
    if (link)
        m_channels[1]->setUpdateRate(m_channels[0]->updateRate());
    emit linkUpdateRatesChanged();
}

void GenController::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode == mode)
        return;
    m_displayMode = mode;
    for (auto& ch : m_channels)
        ch->setSpectrumVisible(mode == Frequency);
    emit displayModeChanged();
}

void GenController::setActiveTab(int tab)
{
    tab = std::clamp(tab, 0, 1);
    if (m_activeTab == tab)
        return;
    m_activeTab = tab;
    emit activeTabChanged();
}

void GenController::onChannelSettingChanged(int index)
{
    markSettingsChanged();
    emit channelsChanged();
    GenChannel* ch = m_channels[size_t(index)].get();
    if (ch->mode() == GenChannel::Arbitrary && ch->hasWaveform())
        ch->setErrorText({});
    if (isRunning()) {
        postLive();
        if (ch->outputOn())
            warnClipping(index);
    }
}

void GenController::restartEngine()
{
    if (!isRunning())
        return;
    m_restarting = true;
    restart();
}

// --- Channel control -----------------------------------------------------------------------------

bool GenController::validateChannel(int index, bool notifyUser)
{
    GenChannel* ch = m_channels[size_t(index)].get();
    if (ch->mode() == GenChannel::Arbitrary && !ch->hasWaveform()) {
        ch->setErrorText(tr("Load a waveform first (Open, Library or the Waveform Editor)."));
        if (notifyUser)
            notify(Notifications::Warning, tr("%1: no waveform loaded").arg(ch->name()));
        return false;
    }
    if (ch->mode() == GenChannel::Off)
        return false;
    ch->setErrorText({});
    return true;
}

void GenController::setOutput(int index, bool on)
{
    if (index < 0 || index > 1)
        return;
    GenChannel* ch = m_channels[size_t(index)].get();
    if (on) {
        if (!validateChannel(index, true))
            return;
        m_forceOnce[size_t(index)] = false;
        ch->setOutput(true, false, ++m_startCounter);
        warnClipping(index);
        if (isRunning())
            postLive();
        else if (isPaused())
            resume();
        else
            run();
        return;
    }
    const bool wasDone = ch->done();
    const bool wasOn = ch->outputOn();
    ch->setOutput(false, false, 0);
    m_forceOnce[size_t(index)] = false;
    if (isRunning()) {
        bool other = false;
        for (auto& c : m_channels)
            other = other || c->outputOn();
        if (!other)
            stop();
        else
            postLive();
    } else if (isPaused()) {
        bool other = false;
        for (auto& c : m_channels)
            other = other || c->outputOn();
        if (!other)
            stop();
    } else if (wasDone && wasOn) {
        releaseDone(uint8_t(1u << index));
    }
}

void GenController::releaseDone(uint8_t mask)
{
    for (int i = 0; i < 2; ++i) {
        GenChannel* ch = m_channels[size_t(i)].get();
        if ((mask & (1u << i)) && (ch->done() || ch->outputOn()))
            ch->setOutput(false, false, 0);
    }
    updateHolds();
    // Idle, holding the last sample of a Run Once: write 0 V statically, off the UI thread (AO pair
    // only; the audio outputs are AC coupled). When another instrument holds AO now, it owns the
    // outputs and nothing is written.
    InstrumentController* holder = ResourceArbiter::instance()->holder(ResourceArbiter::AnalogOutput);
    if (m_pair != AnalogOutputs || isRunning() || (holder && holder != this))
        return;
    const mydaq::DevicePtr device = DeviceHub::instance()->device();
    if (!device || !device->isConnected())
        return;
    QThreadPool::globalInstance()->start([device, mask] {
        for (int i = 0; i < 2; ++i) {
            if (!(mask & (1u << i)))
                continue;
            try {
                device->aoWriteStatic(i == 0 ? mydaq::AoChannel::AO0 : mydaq::AoChannel::AO1, 0.0);
            } catch (const mydaq::DeviceError&) {
                // Unplugged or taken meanwhile: its new owner sets the output.
            }
        }
    });
}

void GenController::applySafeState()
{
    uint8_t mask = 0;
    for (int i = 0; i < 2; ++i) {
        if (m_channels[size_t(i)]->done())
            mask |= uint8_t(1u << i);
    }
    if (mask)
        releaseDone(mask);
    setHoldsOutputs(false);
}

void GenController::startSweep(int index)
{
    if (index < 0 || index > 1)
        return;
    GenChannel* ch = m_channels[size_t(index)].get();
    if (ch->mode() != GenChannel::Function || ch->outputOn())
        return;   // Sweep starts from a channel that is not generating (gen.md 4.1.2)
    if (ch->sweepStep() <= 0 && ch->sweepStart() != ch->sweepStop()) {
        notify(Notifications::Warning, tr("%1: Step must be greater than 0 Hz").arg(ch->name()));
        return;
    }
    ch->setOutput(true, true, ++m_startCounter);
    if (isRunning())
        postLive();
    else if (isPaused())
        resume();
    else
        run();
}

void GenController::sweepAll()
{
    bool any = false;
    for (int i = 0; i < 2; ++i) {
        GenChannel* ch = m_channels[size_t(i)].get();
        if (ch->mode() == GenChannel::Function && !ch->outputOn()) {
            if (ch->sweepStep() <= 0 && ch->sweepStart() != ch->sweepStop()) {
                notify(Notifications::Warning, tr("%1: Step must be greater than 0 Hz").arg(ch->name()));
                continue;
            }
            ch->setOutput(true, true, ++m_startCounter);
            any = true;
        }
    }
    if (!any)
        return;
    if (isRunning())
        postLive();
    else
        run();
}

void GenController::alignPhases()
{
    ++m_alignCounter;
    if (isRunning())
        postLive();
}

gen::EngineSettings GenController::engineSettings() const
{
    gen::EngineSettings s;
    s.pair = m_pair == AudioOutputs ? gen::OutputPair::AudioOutputs : gen::OutputPair::AnalogOutputs;
    s.preference = m_engineMode == Regenerate ? gen::EnginePreference::Regenerate
                   : m_engineMode == Stream   ? gen::EnginePreference::Stream
                                              : gen::EnginePreference::Auto;
    s.alignId = m_alignCounter;
    for (int i = 0; i < 2; ++i) {
        s.channels[size_t(i)] = m_channels[size_t(i)]->engineSettings();
        if (m_forceOnce[size_t(i)])
            s.channels[size_t(i)].arb.once = true;
        if (m_channels[size_t(i)]->done())
            s.channels[size_t(i)].active = false;   // holding: not part of a new run
    }
    return s;
}

void GenController::postLive()
{
    m_live->post(engineSettings());
}

void GenController::warnClipping(int index)
{
    GenChannel* ch = m_channels[size_t(index)].get();
    if (ch->mode() != GenChannel::Arbitrary || ch->clippedSamples() == 0) {
        m_clipWarned[size_t(index)] = {};
        return;
    }
    const auto key = std::make_pair(ch->gain(), static_cast<const void*>(ch->source().samples.get()));
    if (m_clipWarned[size_t(index)] == key)
        return;
    m_clipWarned[size_t(index)] = key;
    notify(Notifications::Warning,
           tr("%1 exceeds the ±%2 V output range after gain").arg(ch->name()).arg(ch->outputRange()),
           tr("%n samples are clipped.", nullptr, ch->clippedSamples()));
}

// --- Worker ----------------------------------------------------------------------------------------

InstrumentController::WorkerFunction GenController::createWorker()
{
    // A new run replaces the values held by finished Run Once channels.
    for (auto& ch : m_channels) {
        if (ch->done())
            ch->setOutput(false, false, 0);
    }
    const bool single = startingRunMode() == Once;
    bool anyOn = false;
    for (auto& ch : m_channels)
        anyOn = anyOn || (ch->outputOn() && !ch->done());

    if (single) {
        // Single: every Arbitrary channel with a waveform plays once; Function channels do not start.
        bool any = false;
        for (int i = 0; i < 2; ++i) {
            GenChannel* ch = m_channels[size_t(i)].get();
            if (ch->mode() == GenChannel::Arbitrary && ch->hasWaveform()) {
                m_forceOnce[size_t(i)] = true;
                ch->setOutput(true, false, ++m_startCounter);
                any = true;
            } else if (!anyOn || ch->done()) {
                ch->setOutput(false, false, 0);
            }
        }
        if (!any)
            throw InvalidInput(tr("Single plays the Arbitrary channels once: load a waveform first."));
    } else if (!anyOn) {
        // Run: every channel whose Mode is not Off starts (as with its Output switch).
        bool any = false;
        QStringList refused;
        for (int i = 0; i < 2; ++i) {
            GenChannel* ch = m_channels[size_t(i)].get();
            if (ch->mode() == GenChannel::Off)
                continue;
            if (!validateChannel(i, false)) {
                ch->setOutput(false, false, 0);
                refused << tr("%1: no waveform loaded").arg(ch->name());
                continue;
            }
            m_forceOnce[size_t(i)] = false;
            ch->setOutput(true, false, ++m_startCounter);
            warnClipping(i);
            any = true;
        }
        if (!any && refused.isEmpty())
            throw InvalidInput(tr("Set %1 or %2 to Function, Arbitrary or DC.").arg(m_channels[0]->name(), m_channels[1]->name()));
        if (!any)
            throw InvalidInput(refused.join(u"; "_s) + u"."_s);
        for (const QString& text : std::as_const(refused))
            notify(Notifications::Warning, text);   // the other channel runs
    } else {
        // Toggle, resume or restart: the channels whose output is on start again from phase 0.
        for (int i = 0; i < 2; ++i) {
            GenChannel* ch = m_channels[size_t(i)].get();
            if (ch->outputOn() && !ch->done())
                ch->setOutput(true, ch->sweeping(), ++m_startCounter);
        }
    }

    m_live->clear();
    m_lastResult.reset();
    const gen::EngineSettings settings = engineSettings();
    auto live = m_live;
    return [this, settings, live](WorkerContext& ctx) {
        WorkerHost host(ctx, live, [this](const gen::EngineStatus& status) { applyEngineStatus(status); });
        gen::Engine engine(ctx.device(), host);
        gen::EngineResult result;
        try {
            result = engine.run(settings);
        } catch (const mydaq::DeviceError& error) {
            throw gen::userFacingError(error);   // keeps the engine's underflow wording (gen.md 7)
        }
        ctx.post([this, result] { m_lastResult = result; });
    };
}

void GenController::applyEngineStatus(const gen::EngineStatus& status)
{
    for (auto& ch : m_channels)
        ch->setTaskRate(status.taskRate);
    for (int i = 0; i < 2; ++i) {
        GenChannel* ch = m_channels[size_t(i)].get();
        const gen::ChannelStatus& cs = status.channels[size_t(i)];
        if (cs.startId != ch->startId())
            continue;
        ch->applyStatus(cs, status.strategy);
        if (cs.sweepFinished && ch->outputOn() && ch->sweeping()) {
            // Single sweep over: the channel stops (0 V); the display shows OFF.
            ch->setOutput(false, false, 0);
            notify(Notifications::Info, tr("%1: sweep finished").arg(ch->name()),
                   tr("Last frequency %1").arg(formatFrequencyDisplay(cs.frequency)));
            if (isRunning())
                postLive();
        } else if (cs.finished && ch->outputOn() && !ch->sweeping()) {
            // Run Once over with "Return to 0 V": the channel is back at 0 V and OFF (ELVISmx).
            ch->setOutput(false, false, 0);
            m_forceOnce[size_t(i)] = false;
            if (isRunning())
                postLive();
        } else if (cs.state == gen::ChannelState::Done && ch->outputOn() && !ch->done()) {
            ch->setDone(true, cs.level);
        }
    }
    if (status.strategy != gen::StrategyKind::None || status.taskRate > 0)
        updateStrategy(status.strategy, status.taskRate, status.taskChannels);
    updateStatusText();
}

void GenController::updateStrategy(gen::StrategyKind kind, double rate, int channels)
{
    Strategy s = NoStrategy;
    QString text;
    const QString r = mydaq::dsp::formatEng(rate, u"S/s"_s, 4);
    switch (kind) {
    case gen::StrategyKind::Static:
        s = StaticWrites;
        text = tr("On-demand writes (DC)");
        break;
    case gen::StrategyKind::Regenerate:
        s = RegeneratedBuffer;
        text = tr("Regenerated buffer · %1%2").arg(r, channels > 1 ? u" × 2"_s : QString());
        break;
    case gen::StrategyKind::Stream:
        s = Streaming;
        text = tr("Streaming · %1 × 2").arg(r);
        break;
    case gen::StrategyKind::None: break;
    }
    if (s == m_strategy && text == m_strategyText && rate == m_taskRate)
        return;
    m_strategy = s;
    m_strategyText = text;
    m_taskRate = rate;
    setSummary(text.isEmpty() ? QString() : u"AO "_s + text);
    emit strategyChanged();
}

void GenController::onStarted()
{
    m_runClock.start();
    m_historyTimer.start();
    updateStatusText();
}

void GenController::onFinished()
{
    m_historyTimer.stop();
    const bool restarting = std::exchange(m_restarting, false);
    const std::optional<gen::EngineResult> result = std::exchange(m_lastResult, std::nullopt);
    if (isPaused()) {
        // Preempted: channels wait for Resume; sweeps are aborted (not resumed).
        for (auto& ch : m_channels) {
            if (ch->sweeping()) {
                ch->setOutput(false, false, 0);
                notify(Notifications::Warning, tr("%1: sweep aborted").arg(ch->name()));
            }
            if (ch->outputOn())
                ch->setPaused(true);
        }
    } else if (!restarting) {
        for (int i = 0; i < 2; ++i) {
            GenChannel* ch = m_channels[size_t(i)].get();
            const bool done = result && result->done[size_t(i)] && result->startId[size_t(i)] == ch->startId();
            if (done)
                ch->setDone(true, result->held[size_t(i)]);
            else if (!ch->done())
                ch->setOutput(false, false, 0);
        }
        m_forceOnce = {};
    }
    for (auto& ch : m_channels)
        ch->setTaskRate(0);
    updateStrategy(gen::StrategyKind::None, 0, 0);
    updateStatusText();
    updateHolds();
}

void GenController::updateStatusText()
{
    QStringList parts;
    for (auto& ch : m_channels) {
        QString state;
        switch (ch->state()) {
        case GenChannel::Inactive:
        case GenChannel::Ready: state = tr("off"); break;
        case GenChannel::Paused: state = tr("paused"); break;
        case GenChannel::Done: state = tr("done"); break;
        case GenChannel::Running:
        case GenChannel::Sweeping: state = ch->readoutText(); break;
        }
        parts << u"%1: %2"_s.arg(ch->name(), state);
    }
    if (isRunning())
        setStatusText(parts.join(u" · "_s));
    emit channelsChanged();
}

// --- Waveforms -------------------------------------------------------------------------------------

void GenController::setChannelSource(int index, GenChannel::Source source, bool adoptRate)
{
    GenChannel* ch = m_channels[size_t(index)].get();
    const double rate = source.sampleRate;
    const QString name = source.name;
    ch->setSource(std::move(source));
    if (adoptRate && rate > 0) {
        const double coerced = std::clamp(rate, gen::kMinUpdateRate,
                                          DeviceHub::instance()->maxAoRate() > 0 ? DeviceHub::instance()->maxAoRate() : 200000.0);
        if (std::abs(coerced - ch->updateRate()) > 1e-9) {
            ch->setUpdateRate(coerced);
            QString text = tr("Update Rate set to %1 from %2").arg(mydaq::dsp::formatEng(coerced, u"S/s"_s, 4), name);
            GenChannel* other = m_channels[size_t(1 - index)].get();
            if (m_linkUpdateRates && other->hasWaveform() && other->source().sampleRate > 0
                && std::abs(other->source().sampleRate - rate) > 1e-9)
                text += u"\n"_s + tr("%1 and %2 were made for different sample rates; both play at the Update Rate.")
                                      .arg(m_channels[0]->name(), m_channels[1]->name());
            notify(Notifications::Info, text);
        }
    }
    if (ch->outputOn())
        warnClipping(index);
}

bool GenController::loadWaveform(int index, const QUrl& file)
{
    if (index < 0 || index > 1)
        return false;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString error;
    auto loaded = gen::loadWaveformFile(path, &error);
    if (!loaded) {
        notify(Notifications::Error, tr("Could not load %1: %2.").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    GenChannel::Source source;
    source.kind = loaded->document ? GenChannel::Source::Document : GenChannel::Source::File;
    source.path = path;
    source.name = loaded->name;
    source.document = loaded->document;
    source.sampleRate = loaded->sampleRate;
    source.samples = std::make_shared<const std::vector<double>>(std::move(loaded->samples));
    for (const QString& notice : loaded->notices)
        notify(Notifications::Warning, loaded->name, notice);
    m_editorLinked[size_t(index)] = false;
    setChannelSource(index, std::move(source), true);
    addRecentFile(path);
    const QString folder = QFileInfo(path).absolutePath();
    if (QSettings().value(kLastFolderKey).toString() != folder) {
        QSettings().setValue(kLastFolderKey, folder);
        emit lastWaveformFolderChanged();
    }
    return true;
}

void GenController::loadLibraryWaveform(int index, int function)
{
    if (index < 0 || index > 1 || function < 0 || function >= gen::kLibraryFunctionCount)
        return;
    m_editorLinked[size_t(index)] = false;
    setChannelSource(index, documentSource(libraryDocument(gen::LibraryFunction(function)), {}), true);
}

void GenController::clearWaveform(int index)
{
    if (index < 0 || index > 1)
        return;
    GenChannel* ch = m_channels[size_t(index)].get();
    if (ch->outputOn())
        setOutput(index, false);
    m_editorLinked[size_t(index)] = false;
    ch->clearSource();
}

void GenController::editChannel(int index, bool discard)
{
    if (index < 0 || index > 1)
        return;
    GenChannel* ch = m_channels[size_t(index)].get();
    const GenChannel::Source& source = ch->source();
    // The editor already shows this channel's document (e.g. live-linked): keep it and its undo.
    if (source.document && *source.document == m_editor.document()) {
        m_editor.setLinkedChannel(index);
        m_editorLinked[size_t(index)] = true;
        setActiveTab(1);
        return;
    }
    if (m_editor.modified() && !discard) {
        setActiveTab(1);
        emit editChannelNeedsConfirmation(index);   // Save / Discard / Cancel, then editChannel(i, true)
        return;
    }
    if (source.document) {
        m_editor.setDocument(*source.document, source.path.endsWith(u".mbw"_s, Qt::CaseInsensitive) ? source.path : QString());
    } else if (ch->hasWaveform()) {
        const double rate = source.sampleRate > 0 ? source.sampleRate : ch->updateRate();
        m_editor.setDocument(gen::documentFromSamples(*source.samples, rate, QFileInfo(source.name).completeBaseName()));
    } else {
        m_editor.setDocument(gen::Document{});
    }
    m_editor.setLinkedChannel(index);
    m_editorLinked[size_t(index)] = true;
    setActiveTab(1);
}

void GenController::onEditorUse(int index)
{
    if (index < 0 || index > 1)
        return;
    const gen::Document& doc = m_editor.document();
    const QString path = !m_editor.modified() ? m_editor.filePath() : QString();
    GenChannel::Source source = documentSource(doc, path);
    if (!source.samples || source.samples->empty()) {
        notify(Notifications::Warning, tr("The waveform is empty"));
        return;
    }
    m_editorLinked[size_t(index)] = true;
    GenChannel* ch = m_channels[size_t(index)].get();
    if (ch->mode() != GenChannel::Arbitrary)
        ch->setMode(GenChannel::Arbitrary);
    setChannelSource(index, std::move(source), true);
    notify(Notifications::Success, tr("%1 loaded into %2").arg(doc.name, ch->name()));
}

void GenController::onEditorRendered()
{
    const int index = m_editor.linkedChannel();
    if (index < 0 || !m_editor.liveUpdate() || !m_editorLinked[size_t(index)])
        return;
    GenChannel* ch = m_channels[size_t(index)].get();
    const auto samples = m_editor.renderedSamples();
    if (!samples || samples->empty() || ch->mode() != GenChannel::Arbitrary || samples == ch->source().samples)
        return;
    if (ch->source().document && *ch->source().document == m_editor.document())
        return;
    GenChannel::Source source;
    source.kind = GenChannel::Source::Document;
    source.name = m_editor.document().name;
    source.document = m_editor.document();
    source.sampleRate = m_editor.document().sampleRate;
    source.samples = samples;
    setChannelSource(index, std::move(source), true);
}

bool GenController::exportChannel(int index, const QUrl& file)
{
    if (index < 0 || index > 1)
        return false;
    const DataTable table = exportTable();
    GenChannel* ch = m_channels[size_t(index)].get();
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    for (const DataColumn& column : table.columns) {
        if (column.name != ch->name())
            continue;
        const double rate = ch->mode() == GenChannel::Arbitrary ? ch->updateRate() : 0;
        double r = rate;
        if (r <= 0) {
            for (const DataColumn& t : table.columns)
                if (t.name == tr("Time %1").arg(ch->name()) && t.values.size() > 1)
                    r = 1.0 / (t.values[1] - t.values[0]);
        }
        QString error;
        size_t clipped = 0;
        if (!gen::saveWaveformFile(path, column.values, r, &error, &clipped)) {
            notify(Notifications::Error, tr("Could not export %1").arg(QFileInfo(path).fileName()), error);
            return false;
        }
        notify(Notifications::Success, tr("Exported %1").arg(QFileInfo(path).fileName()),
               clipped ? tr("%n samples beyond the file's full scale were clipped.", nullptr, int(clipped)) : QString());
        return true;
    }
    notify(Notifications::Warning, tr("%1 has nothing to export").arg(ch->name()));
    return false;
}

void GenController::addRecentFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 10)
        m_recentFiles.removeLast();
    QSettings().setValue(kRecentKey, m_recentFiles);
    emit recentFilesChanged();
}

void GenController::clearRecentFiles()
{
    m_recentFiles.clear();
    QSettings().remove(kRecentKey);
    emit recentFilesChanged();
}

QStringList GenController::libraryNames() const
{
    QStringList names;
    for (int i = 0; i < gen::kLibraryFunctionCount; ++i)
        names << gen::functionName(gen::LibraryFunction(i));
    return names;
}

bool GenController::canSingle() const
{
    for (const auto& ch : m_channels) {
        if (ch->mode() == GenChannel::Arbitrary && ch->hasWaveform())
            return true;
    }
    return false;
}

bool GenController::canSweep() const
{
    for (const auto& ch : m_channels) {
        if (ch->mode() == GenChannel::Function && !ch->outputOn())
            return true;
    }
    return false;
}

QUrl GenController::lastWaveformFolder() const
{
    const QString folder = QSettings().value(kLastFolderKey).toString();
    return folder.isEmpty() || !QFileInfo(folder).isDir() ? QUrl() : QUrl::fromLocalFile(folder);
}

bool GenController::anyFunction() const
{
    for (const auto& ch : m_channels) {
        if (ch->mode() == GenChannel::Function)
            return true;
    }
    return false;
}

QString GenController::outputSummary() const
{
    QStringList parts;
    for (const auto& ch : m_channels)
        parts << u"%1 %2"_s.arg(ch->name(), ch->summary());
    return parts.join(u" · "_s);
}

// --- Presets ---------------------------------------------------------------------------------------

QStringList GenController::builtInPresets() const
{
    return {tr("1 kHz sine, 1 Vpp"), tr("TTL clock 1 kHz (0 to 5 V square)"), tr("Quadrature 1 kHz (AO 1 at +90°)"),
            tr("Sweep 100 Hz to 10 kHz"), tr("DC reference ±5 V"), tr("ELVISmx defaults")};
}

void GenController::loadBuiltInPreset(int index)
{
    GenChannel* a = m_channels[0].get();
    GenChannel* b = m_channels[1].get();
    auto resetFunction = [](GenChannel* c) {
        c->setMode(GenChannel::Function);
        c->setShape(GenChannel::Sine);
        c->setFrequency(100);
        c->setAmplitude(1);
        c->setOffset(0);
        c->setDutyCycle(50);
        c->setPhase(0);
    };
    switch (index) {
    case 0:
        resetFunction(a);
        a->setFrequency(1000);
        b->setMode(GenChannel::Off);
        break;
    case 1:
        resetFunction(a);
        a->setShape(GenChannel::Square);
        a->setFrequency(1000);
        a->setAmplitude(5);
        a->setOffset(2.5);
        b->setMode(GenChannel::Off);
        break;
    case 2:
        resetFunction(a);
        resetFunction(b);
        a->setFrequency(1000);
        b->setFrequency(1000);
        b->setPhase(90);
        a->setAmplitude(2);
        b->setAmplitude(2);
        break;
    case 3:
        resetFunction(a);
        a->setSweepStart(100);
        a->setSweepStop(10000);
        a->setSweepStep(900);
        a->setSweepInterval(500);
        b->setMode(GenChannel::Off);
        break;
    case 4:
        if (m_pair == AudioOutputs)
            setOutputPair(AnalogOutputs);
        a->setMode(GenChannel::Dc);
        b->setMode(GenChannel::Dc);
        a->setDcLevel(5);
        b->setDcLevel(-5);
        break;
    case 5:
        resetFunction(a);
        a->setSweepStart(100);
        a->setSweepStop(1000);
        a->setSweepStep(100);
        a->setSweepInterval(1000);
        a->setSweepBlockTiming(true);
        b->setMode(GenChannel::Off);
        break;
    default: return;
    }
    notify(Notifications::Info, tr("Preset loaded"), builtInPresets().value(index));
}

QStringList GenController::presetNames() const
{
    const QJsonObject all = QJsonDocument::fromJson(QSettings().value(kPresetsKey).toByteArray()).object();
    QStringList names = all.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void GenController::savePreset(const QString& name)
{
    const QString key = name.trimmed();
    if (key.isEmpty())
        return;
    QJsonObject all = QJsonDocument::fromJson(QSettings().value(kPresetsKey).toByteArray()).object();
    QJsonObject preset = channelsJson();
    preset[u"outputPair"_s] = m_pair == AudioOutputs ? u"AudioOutputs"_s : u"AnalogOutputs"_s;
    all[key] = preset;
    QSettings().setValue(kPresetsKey, QJsonDocument(all).toJson(QJsonDocument::Compact));
    emit presetsChanged();
    notify(Notifications::Success, tr("Preset saved"), key);
}

void GenController::loadPreset(const QString& name)
{
    const QJsonObject all = QJsonDocument::fromJson(QSettings().value(kPresetsKey).toByteArray()).object();
    if (!all.contains(name))
        return;
    const QJsonObject preset = all.value(name).toObject();
    setOutputPair(preset.value(u"outputPair"_s).toString() == u"AudioOutputs" ? AudioOutputs : AnalogOutputs);
    applyChannelsJson(preset);
    notify(Notifications::Info, tr("Preset loaded"), name);
}

void GenController::deletePreset(const QString& name)
{
    QJsonObject all = QJsonDocument::fromJson(QSettings().value(kPresetsKey).toByteArray()).object();
    all.remove(name);
    QSettings().setValue(kPresetsKey, QJsonDocument(all).toJson(QJsonDocument::Compact));
    emit presetsChanged();
}

// --- Persistence -----------------------------------------------------------------------------------

QJsonObject GenController::channelsJson() const
{
    QJsonArray channels;
    const QList<QMetaProperty> props = GenChannel::settingProperties();
    for (int i = 0; i < 2; ++i) {
        const GenChannel* ch = m_channels[size_t(i)].get();
        QJsonObject o = json::readProperties(ch, props);
        const GenChannel::Source& s = ch->source();
        if (s.kind != GenChannel::Source::None) {
            QJsonObject source;
            source[u"kind"_s] = s.kind == GenChannel::Source::File ? u"file"_s : u"document"_s;
            source[u"path"_s] = s.path;
            source[u"name"_s] = s.name;
            source[u"sampleRate"_s] = s.sampleRate;
            // Documents are stored inline (editor documents need no file); plain files by path.
            if (s.document)
                source[u"document"_s] = gen::toJson(*s.document);
            o[u"source"_s] = source;
        }
        channels.append(o);
    }
    return QJsonObject{{u"channels"_s, channels}};
}

void GenController::applyChannelsJson(const QJsonObject& json)
{
    const QJsonArray channels = json.value(u"channels"_s).toArray();
    const QList<QMetaProperty> props = GenChannel::settingProperties();
    for (int i = 0; i < 2 && i < channels.size(); ++i) {
        GenChannel* ch = m_channels[size_t(i)].get();
        const QJsonObject o = channels[i].toObject();
        m_syncingRates = true;
        json::writeProperties(ch, props, o);
        m_syncingRates = false;
        const QJsonObject source = o.value(u"source"_s).toObject();
        if (source.isEmpty()) {
            if (ch->hasWaveform())
                ch->clearSource();
            continue;
        }
        if (source.contains(u"document"_s)) {
            if (auto doc = gen::fromJson(source.value(u"document"_s).toObject())) {
                GenChannel::Source s = documentSource(*doc, source.value(u"path"_s).toString());
                s.name = source.value(u"name"_s).toString(s.name);
                ch->setSource(std::move(s));
            }
        } else {
            const QString path = source.value(u"path"_s).toString();
            QString error;
            if (auto loaded = gen::loadWaveformFile(path, &error)) {
                GenChannel::Source s;
                s.kind = GenChannel::Source::File;
                s.path = path;
                s.name = loaded->name;
                s.sampleRate = loaded->sampleRate;
                s.samples = std::make_shared<const std::vector<double>>(std::move(loaded->samples));
                ch->setSource(std::move(s));
            } else {
                ch->clearSource();
                ch->setErrorText(tr("Could not reload %1: %2").arg(QFileInfo(path).fileName(), error));
            }
        }
    }
}

void GenController::saveExtraSettings(QJsonObject& settings) const
{
    const QJsonObject channels = channelsJson();
    settings[u"channels"_s] = channels.value(u"channels"_s);
    settings[u"editor"_s] = m_editor.saveState();
}

void GenController::restoreExtraSettings(const QJsonObject& settings)
{
    // Settings change while running: stop first (outputs never change behind the user's back).
    if (isRunning())
        stop();
    for (int i = 0; i < 2; ++i) {
        if (m_channels[size_t(i)]->done())
            m_channels[size_t(i)]->setOutput(false, false, 0);
    }
    for (auto& ch : m_channels)
        ch->setPair(m_pair == AudioOutputs ? gen::OutputPair::AudioOutputs : gen::OutputPair::AnalogOutputs);
    if (settings.contains(u"channels"_s))
        applyChannelsJson(settings);
    if (settings.contains(u"editor"_s))
        m_editor.restoreState(settings.value(u"editor"_s).toObject());
    for (auto& ch : m_channels)
        ch->setSpectrumVisible(m_displayMode == Frequency);
}

// --- Export ------------------------------------------------------------------------------------------

DataTable GenController::exportTable() const
{
    DataTable table;
    if (m_activeTab == 1) {
        // Waveform Editor tab: the rendered document (plot "Export data", panel export).
        const gen::Document& doc = m_editor.document();
        const auto samples = m_editor.renderedSamples();
        table.addMetadata(tr("Waveform"), doc.name);
        table.addMetadata(tr("Sample rate"), mydaq::dsp::formatEng(doc.sampleRate, u"S/s"_s, 6));
        if (samples && !samples->empty()) {
            std::vector<double> t(samples->size());
            for (size_t i = 0; i < t.size(); ++i)
                t[i] = double(i) / doc.sampleRate;
            table.addColumn(tr("Time"), u"s"_s, std::move(t));
            table.addColumn(doc.name, u"V"_s, std::vector<double>(*samples));
        }
        return table;
    }
    table.addMetadata(tr("Output"), m_pair == AudioOutputs ? tr("AudioOutput Left / Right") : tr("AO 0 / AO 1"));
    for (const auto& ch : m_channels) {
        const double range = ch->outputRange();
        std::vector<double> y;
        double rate = 0;
        QString settings;
        switch (ch->mode()) {
        case GenChannel::Off: settings = tr("Off"); break;
        case GenChannel::Function: {
            // One period of the ELVISmx buffer at its rate.
            const double maxRate = DeviceHub::instance()->maxAoRate() > 0 ? DeviceHub::instance()->maxAoRate() : 200000.0;
            const gen::FgenPlan plan = gen::planFgenBuffer(ch->frequency(), maxRate, [maxRate](double r) {
                return 100e6 / std::max(1.0, std::round(100e6 / std::min(r, maxRate)));
            });
            y = gen::functionPeriod(ch->engineSettings().function, plan.length, range);
            rate = plan.rate;
            static const char* shapes[] = {"Sine", "Triangle", "Square"};
            settings = tr("Function %1, %2 (actual %3), %4 Vpp, offset %5 V, duty %6 %")
                           .arg(QLatin1StringView(shapes[int(ch->shape())]), mydaq::dsp::formatEng(ch->frequency(), u"Hz"_s, 6),
                                formatFrequencyDisplay(plan.frequency), QString::number(ch->amplitude(), 'f', 2),
                                QString::number(ch->offset(), 'f', 2), QString::number(ch->dutyCycle(), 'f', 0));
            break;
        }
        case GenChannel::Arbitrary:
            if (ch->hasWaveform()) {
                for (double v : *ch->source().samples)
                    y.push_back(std::clamp(ch->gain() * v, -range, range));
                rate = ch->updateRate();
            }
            settings = tr("Arbitrary %1, gain %2, %3, %4").arg(ch->waveformName(), QString::number(ch->gain(), 'f', 2),
                                                              mydaq::dsp::formatEng(ch->updateRate(), u"S/s"_s, 4),
                                                              ch->generationMode() == GenChannel::RunOnce ? tr("Run Once")
                                                                                                          : tr("Run Continuously"));
            break;
        case GenChannel::Dc:
            y = {ch->dcLevel(), ch->dcLevel()};
            rate = 1.0;
            settings = tr("DC Level %1 V").arg(QString::number(ch->dcLevel(), 'f', 2));
            break;
        }
        table.addMetadata(ch->name(), settings);
        if (y.empty())
            continue;
        std::vector<double> t(y.size());
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = double(i) / rate;
        table.addColumn(tr("Time %1").arg(ch->name()), u"s"_s, std::move(t));
        table.addColumn(ch->name(), u"V"_s, std::move(y));
    }
    return table;
}

void registerGen(InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"gen"_s,
        .title = QObject::tr("Waveform Generator"),
        .shortTitle = QObject::tr("Gen"),
        .description = QObject::tr("Function, arbitrary and DC outputs on AO 0 / AO 1, with a waveform editor"),
        .module = u"MyBench.Instruments.Gen"_s,
        .icon = u"instrument-gen"_s,
        .order = 30,
        .createController = [] { return new GenController; },
    });
}

}  // namespace mybench::instruments
