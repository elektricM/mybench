// GenController: the Waveform Generator (docs/instruments/gen.md) — ELVISmx Function Generator,
// Arbitrary Waveform Generator, Waveform Editor and the "DC Level" application merged. AO 0 and
// AO 1 (or AudioOutput Left/Right) are two GenChannels, each Off / Function / Arbitrary / DC, that
// run together through the generation engine (genengine.h), which picks a regenerated buffer
// (ELVISmx behaviour), a DDS stream or on-demand writes.
#pragma once

#include "genchannel.h"
#include "genengine.h"
#include "waveformeditor.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>

#include <QElapsedTimer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>

namespace mybench::instruments {

class GenController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // Settings (persisted)
    Q_PROPERTY(OutputPair outputPair READ outputPair WRITE setOutputPair NOTIFY outputPairChanged)
    Q_PROPERTY(EngineMode engineMode READ engineMode WRITE setEngineMode NOTIFY engineModeChanged)
    Q_PROPERTY(bool linkUpdateRates READ linkUpdateRates WRITE setLinkUpdateRates NOTIFY linkUpdateRatesChanged)
    Q_PROPERTY(DisplayMode displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(int activeTab READ activeTab WRITE setActiveTab NOTIFY activeTabChanged)   // 0 generator, 1 editor

    // Channels, editor
    Q_PROPERTY(mybench::instruments::GenChannel* channel0 READ channel0 CONSTANT)
    Q_PROPERTY(mybench::instruments::GenChannel* channel1 READ channel1 CONSTANT)
    Q_PROPERTY(mybench::instruments::WaveformEditor* editor READ editor CONSTANT)

    // Indicators
    Q_PROPERTY(QString strategyText READ strategyText NOTIFY strategyChanged)   // "Streaming · 200 kS/s × 2"
    Q_PROPERTY(Strategy strategy READ strategy NOTIFY strategyChanged)
    Q_PROPERTY(double taskRate READ taskRate NOTIFY strategyChanged)
    Q_PROPERTY(bool canSingle READ canSingle NOTIFY channelsChanged)   // an Arbitrary channel has a waveform
    Q_PROPERTY(bool anyFunction READ anyFunction NOTIFY channelsChanged)
    Q_PROPERTY(bool canSweep READ canSweep NOTIFY channelsChanged)     // a Function channel is not generating
    Q_PROPERTY(QUrl lastWaveformFolder READ lastWaveformFolder NOTIFY lastWaveformFolderChanged)
    Q_PROPERTY(QString outputSummary READ outputSummary NOTIFY channelsChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
    Q_PROPERTY(QStringList builtInPresets READ builtInPresets CONSTANT)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QStringList libraryNames READ libraryNames CONSTANT)

public:
    enum OutputPair { AnalogOutputs, AudioOutputs };
    Q_ENUM(OutputPair)
    enum EngineMode { Auto, Regenerate, Stream };
    Q_ENUM(EngineMode)
    enum DisplayMode { Time, Frequency };
    Q_ENUM(DisplayMode)
    enum Strategy { NoStrategy, StaticWrites, RegeneratedBuffer, Streaming };
    Q_ENUM(Strategy)

    GenController();
    ~GenController() override;

    OutputPair outputPair() const { return m_pair; }
    void setOutputPair(OutputPair pair);
    EngineMode engineMode() const { return m_engineMode; }
    void setEngineMode(EngineMode mode);
    bool linkUpdateRates() const { return m_linkUpdateRates; }
    void setLinkUpdateRates(bool link);
    DisplayMode displayMode() const { return m_displayMode; }
    void setDisplayMode(DisplayMode mode);
    int activeTab() const { return m_activeTab; }
    void setActiveTab(int tab);

    GenChannel* channel0() { return m_channels[0].get(); }
    GenChannel* channel1() { return m_channels[1].get(); }
    WaveformEditor* editor() { return &m_editor; }

    QString strategyText() const { return m_strategyText; }
    Strategy strategy() const { return m_strategy; }
    double taskRate() const { return m_taskRate; }
    bool canSingle() const;
    bool anyFunction() const;
    bool canSweep() const;
    QUrl lastWaveformFolder() const;
    QString outputSummary() const;
    QStringList presetNames() const;
    QStringList builtInPresets() const;
    QStringList recentFiles() const { return m_recentFiles; }
    QStringList libraryNames() const;

    Q_INVOKABLE mybench::instruments::GenChannel* channel(int index);
    // Per-channel Output switch (the channel's Run/Stop).
    Q_INVOKABLE void setOutput(int channel, bool on);
    Q_INVOKABLE void startSweep(int channel);
    Q_INVOKABLE void sweepAll();                 // run bar Sweep: every Function channel
    Q_INVOKABLE void alignPhases();              // restart both Function phases together (Stream)
    Q_INVOKABLE bool loadWaveform(int channel, const QUrl& file);
    Q_INVOKABLE bool loadWaveformPath(int channel, const QString& path) { return loadWaveform(channel, QUrl::fromLocalFile(path)); }
    Q_INVOKABLE void loadLibraryWaveform(int channel, int function);
    Q_INVOKABLE void clearWaveform(int channel);
    // Opens the editor on the channel's waveform. When the editor holds another, modified document,
    // emits editChannelNeedsConfirmation() instead (unless `discard`), so the view can ask first.
    Q_INVOKABLE void editChannel(int channel, bool discard = false);
    Q_INVOKABLE bool exportChannel(int channel, const QUrl& file);
    Q_INVOKABLE void savePreset(const QString& name);
    Q_INVOKABLE void loadPreset(const QString& name);
    Q_INVOKABLE void deletePreset(const QString& name);
    Q_INVOKABLE void loadBuiltInPreset(int index);
    Q_INVOKABLE void clearRecentFiles();
    // Screenshot scenes of the dev executable (MYBENCH_GEN_SCENE=settings|draw|unsaved|help ...),
    // "" otherwise: the panel opens that dialog/state once loaded (documentation, visual checks).
    Q_INVOKABLE QString devScene() const { return qEnvironmentVariable("MYBENCH_GEN_SCENE"); }

signals:
    void outputPairChanged();
    void engineModeChanged();
    void linkUpdateRatesChanged();
    void displayModeChanged();
    void activeTabChanged();
    void strategyChanged();
    void channelsChanged();
    void presetsChanged();
    void recentFilesChanged();
    void lastWaveformFolderChanged();
    void editChannelNeedsConfirmation(int channel);

protected:
    WorkerFunction createWorker() override;
    void onStarted() override;
    void onFinished() override;
    void applySafeState() override;   // Stop / Stop all / panel close while idle: Done channels to 0 V
    void saveExtraSettings(QJsonObject& settings) const override;
    void restoreExtraSettings(const QJsonObject& settings) override;
    DataTable exportTable() const override;

private:
    gen::EngineSettings engineSettings() const;
    void postLive();
    void applyEngineStatus(const gen::EngineStatus& status);
    void onChannelSettingChanged(int index);
    void restartEngine();
    bool validateChannel(int index, bool notify);
    void warnClipping(int index);
    void setChannelSource(int index, GenChannel::Source source, bool adoptRate);
    void addRecentFile(const QString& path);
    QJsonObject channelsJson() const;
    void applyChannelsJson(const QJsonObject& json);
    void updateStatusText();
    void updateStrategy(gen::StrategyKind kind, double rate, int channels);
    void onEditorUse(int channel);
    void onEditorRendered();
    void notify(int level, const QString& title, const QString& message = {});
    void releaseDone(uint8_t mask);   // Done (holding) channels of `mask` back to 0 V
    void updateHolds();               // singleEnabled / holdsOutputs from the channel states
    void addPaletteCommands();

    std::array<std::unique_ptr<GenChannel>, 2> m_channels;
    WaveformEditor m_editor;
    OutputPair m_pair = AnalogOutputs;
    EngineMode m_engineMode = Auto;
    bool m_linkUpdateRates = true;
    DisplayMode m_displayMode = Time;
    int m_activeTab = 0;

    std::shared_ptr<Mailbox<gen::EngineSettings>> m_live = std::make_shared<Mailbox<gen::EngineSettings>>();
    uint64_t m_startCounter = 0;
    uint64_t m_alignCounter = 0;
    std::array<bool, 2> m_forceOnce{};
    std::array<bool, 2> m_editorLinked{};
    std::array<std::pair<double, const void*>, 2> m_clipWarned{};
    bool m_restarting = false;
    std::optional<gen::EngineResult> m_lastResult;

    QString m_strategyText;
    Strategy m_strategy = NoStrategy;
    double m_taskRate = 0;
    QStringList m_recentFiles;
    QTimer m_historyTimer;
    QElapsedTimer m_runClock;
    bool m_syncingRates = false;
};

void registerGen(InstrumentRegistry& registry);

}  // namespace mybench::instruments
