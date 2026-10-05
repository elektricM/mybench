// GenController and WaveformEditor against the simulator: run/stop per channel, strategies,
// sweep end, Run Once hold and release, pause/resume, persistence, editor edits and undo.
#include "gencontroller.h"
#include "genfileio.h"

#include <mybench/framework.h>

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QClipboard>
#include <QFile>
#include <QJsonObject>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace mybench;
using namespace mybench::instruments;
using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<mydaq::SimDevice> sim()
{
    return std::dynamic_pointer_cast<mydaq::SimDevice>(mydaq::DeviceManager::instance().device(u"sim:0"_s));
}

double ai0()
{
    return sim()->aiReadStatic(mydaq::AiChannel::AI0, 10);
}

}  // namespace

class TestGenController : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QCoreApplication::setOrganizationName(u"MYBENCH-test"_s);
        QCoreApplication::setApplicationName(u"tst_gencontroller"_s);
        InstrumentController::setRestoreSavedSettings(false);
        QVERIFY(DeviceHub::instance()->isConnected());
        mydaq::SimTestSignal silent;
        silent.amplitude = 0;
        silent.harmonics.clear();
        sim()->setTestSignal(mydaq::AiChannel::AI0, silent);
        m_gen = std::make_unique<GenController>();
        m_gen->initialize();
    }
    void cleanupTestCase() { m_gen.reset(); }

    void init() { m_gen->resetSettings(); }
    void cleanup()
    {
        m_gen->stop();
        QTRY_VERIFY(m_gen->isIdle());
    }

    void defaults()
    {
        GenChannel* a = m_gen->channel0();
        QCOMPARE(a->mode(), GenChannel::Function);
        QCOMPARE(a->shape(), GenChannel::Sine);
        QCOMPARE(a->frequency(), 100.0);
        QCOMPARE(a->amplitude(), 1.0);
        QCOMPARE(a->offset(), 0.0);
        QCOMPARE(a->dutyCycle(), 50.0);
        QCOMPARE(a->sweepStart(), 100.0);
        QCOMPARE(a->sweepStop(), 1000.0);
        QCOMPARE(a->sweepStep(), 100.0);
        QCOMPARE(a->sweepInterval(), 1000.0);
        QCOMPARE(a->updateRate(), 1000.0);
        QCOMPARE(a->gain(), 1.0);
        QCOMPARE(m_gen->channel1()->mode(), GenChannel::Off);
        QCOMPARE(a->readoutText(), u"OFF"_s);
        QCOMPARE(a->waveformName(), u"< empty >"_s);
        QCOMPARE(formatFrequencyDisplay(100), u"100.0000 Hz"_s);
        QCOMPARE(formatFrequencyDisplay(0.2), u"200.0000 mHz"_s);
        QCOMPARE(formatFrequencyDisplay(20000), u"20.0000 kHz"_s);
        // Limits (ELVISmx): duty 0..100 %, step interval 1..300 000 ms.
        a->setDutyCycle(0);
        QCOMPARE(a->dutyCycle(), 0.0);
        a->setSweepInterval(500000);
        QCOMPARE(a->sweepInterval(), 300000.0);
        a->setFrequency(50000);
        QCOMPARE(a->frequency(), 20000.0);
    }

    void runAndStop()
    {
        m_gen->run();
        QTRY_VERIFY(m_gen->channel0()->generating());
        QVERIFY(m_gen->channel0()->outputOn());
        // Default engine: the phase-continuous stream (audit gen #1).
        QCOMPARE(m_gen->engineMode(), GenController::Auto);
        QTRY_COMPARE(m_gen->strategy(), GenController::Streaming);
        QCOMPARE(m_gen->channel0()->readoutText(), u"100.0000 Hz"_s);
        QCOMPARE(m_gen->taskRate(), 200000.0);
        m_gen->stop();
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY(!m_gen->channel0()->outputOn());
        QCOMPARE(m_gen->channel0()->readoutText(), u"OFF"_s);
        QVERIFY(std::abs(ai0()) < 0.01);
    }

    void arbitraryWithoutWaveformIsRefused()
    {
        m_gen->channel1()->setMode(GenChannel::Arbitrary);
        m_gen->setOutput(1, true);
        QVERIFY(!m_gen->channel1()->outputOn());
        QVERIFY(!m_gen->channel1()->errorText().isEmpty());
        QVERIFY(m_gen->isIdle());
    }

    void twoChannelsStreamAndStopOne()
    {
        m_gen->channel1()->setMode(GenChannel::Arbitrary);
        m_gen->loadLibraryWaveform(1, 1);   // Sine document
        QCOMPARE(m_gen->channel1()->waveformPoints(), 1000);
        QCOMPARE(m_gen->channel1()->updateRate(), 100000.0);   // adopted from the document
        QCOMPARE(m_gen->channel0()->updateRate(), 100000.0);   // linked rates
        m_gen->setOutput(0, true);
        QTRY_COMPARE(m_gen->strategy(), GenController::Streaming);
        m_gen->setOutput(1, true);
        QTRY_COMPARE(m_gen->strategy(), GenController::Streaming);
        QTRY_VERIFY(m_gen->channel1()->generating());
        QTRY_COMPARE(m_gen->channel1()->readoutText(), u"100.00 kS/s"_s);   // 2 decimals (audit gen #10)
        m_gen->setOutput(1, false);
        QTest::qWait(150);
        QVERIFY(m_gen->isRunning());
        QVERIFY(m_gen->channel0()->generating());
        QCOMPARE(m_gen->strategy(), GenController::Streaming);   // no downgrade
        m_gen->setOutput(0, false);
        QTRY_VERIFY(m_gen->isIdle());
    }

    void regenerateShowsRequestedFrequency()
    {
        // ELVISmx look-alike engine: the readout shows the requested frequency (audit gen #2),
        // the caption the looped buffer and its real frequency.
        m_gen->setEngineMode(GenController::Regenerate);
        GenChannel* a = m_gen->channel0();
        a->setFrequency(1234.5);
        QVERIFY(std::abs(a->expectedFrequency() - 200000.0 / 162) < 1e-6);
        m_gen->run();
        QTRY_VERIFY(a->generating());
        QTRY_COMPARE(m_gen->strategy(), GenController::RegeneratedBuffer);
        QTRY_COMPARE(a->readoutText(), u"1.2345 kHz"_s);
        QVERIFY2(a->readoutCaption().contains(u"162 samples"_s), qPrintable(a->readoutCaption()));
        QVERIFY(a->readoutCaption().contains(u"1.2346 kHz"_s));
        m_gen->stop();
        QTRY_VERIFY(m_gen->isIdle());
        m_gen->setEngineMode(GenController::Auto);
        QCOMPARE(a->expectedFrequency(), 1234.5);
    }

    void limits()
    {
        GenChannel* a = m_gen->channel0();
        a->setUpdateRate(0.1);   // ELVISmx minimum (audit gen #3)
        QCOMPARE(a->updateRate(), 0.1);
        a->setUpdateRate(0.01);
        QCOMPARE(a->updateRate(), 0.1);
        a->setGain(20);          // gen.md 4.2.2: -10 ... +10 (audit gen #13)
        QCOMPARE(a->gain(), 10.0);
        a->setGain(-20);
        QCOMPARE(a->gain(), -10.0);
        QCOMPARE(formatRateDisplay(1000), u"1.00 kS/s"_s);
        QCOMPARE(formatRateDisplay(200000), u"200.00 kS/s"_s);
        QCOMPARE(formatRateDisplay(0.1), u"100.00 mS/s"_s);
        QVERIFY(!a->sweepBlockTiming());   // exact Step Interval by default (audit gen #9)
    }

    void runOnceReturnsToZero()
    {
        // "Return to 0 V after Run Once" (ELVISmx behaviour, audit gen #4).
        QTemporaryDir dir;
        const QString path = dir.filePath(u"level.csv"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# sampleRate=1000\n1.5\n1.5\n1.5\n1.5\n");
        f.close();
        GenChannel* a = m_gen->channel0();
        a->setMode(GenChannel::Arbitrary);
        QVERIFY(m_gen->loadWaveform(0, QUrl::fromLocalFile(path)));
        a->setZeroAfterRunOnce(true);
        a->setGenerationMode(GenChannel::RunOnce);
        QVERIFY(a->readoutCaption().contains(u"then 0 V"_s));
        m_gen->setOutput(0, true);
        QTRY_VERIFY_WITH_TIMEOUT(m_gen->isIdle(), 3000);
        QVERIFY(!a->done());
        QVERIFY(!a->outputOn());
        QCOMPARE(a->readoutText(), u"OFF"_s);
        QVERIFY(std::abs(ai0()) < 0.01);
        // Persisted with the channel settings.
        const QJsonObject saved = m_gen->saveSettings();
        m_gen->resetSettings();
        QVERIFY(!m_gen->channel0()->zeroAfterRunOnce());
        m_gen->restoreSettings(saved);
        QVERIFY(m_gen->channel0()->zeroAfterRunOnce());
        QCOMPARE(QUrl::fromLocalFile(dir.path()), m_gen->lastWaveformFolder());   // audit gen #20
    }

    void refusalsAreWarningsNotDeviceErrors()
    {
        // Run with both channels Off, Single without a waveform (audit gen #6, #7).
        m_gen->channel0()->setMode(GenChannel::Off);
        m_gen->run();
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY2(!m_gen->errorText().contains(u"device rejected"_s), qPrintable(m_gen->errorText()));
        QVERIFY(!m_gen->hasError());
        QVERIFY2(m_gen->inputWarning().contains(u"Function, Arbitrary or DC"_s), qPrintable(m_gen->inputWarning()));
        m_gen->clearInputWarning();
        m_gen->channel0()->setMode(GenChannel::Function);
        // Single is greyed without an Arbitrary waveform (framework singleEnabled).
        QVERIFY(!m_gen->canSingle());
        QVERIFY(!m_gen->singleEnabled());
        m_gen->runOnce();
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY(!m_gen->hasError());
        QVERIFY(!m_gen->channel0()->outputOn());
        m_gen->channel1()->setMode(GenChannel::Arbitrary);
        m_gen->loadLibraryWaveform(1, int(gen::LibraryFunction::Sine));
        QVERIFY(m_gen->singleEnabled());
        m_gen->channel1()->clearSource();
        QVERIFY(!m_gen->singleEnabled());
        // Run with an Arbitrary channel without waveform only: a warning, not a device error.
        m_gen->channel0()->setMode(GenChannel::Off);
        m_gen->run();
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY(!m_gen->hasError());
        QVERIFY(m_gen->inputWarning().contains(u"no waveform loaded"_s));
    }

    void sweepOnlyFromAStoppedChannel()
    {
        // Sweep is grayed while the channel generates (audit gen #8).
        QVERIFY(m_gen->canSweep());
        m_gen->run();
        QTRY_VERIFY(m_gen->channel0()->generating());
        QVERIFY(!m_gen->canSweep());
        m_gen->startSweep(0);
        m_gen->sweepAll();
        QTest::qWait(50);
        QVERIFY(!m_gen->channel0()->sweeping());
        QVERIFY(m_gen->channel0()->generating());
    }

    void editChannelAsksBeforeDiscarding()
    {
        // Edit on a channel card must not silently replace a modified editor document (editor #19).
        WaveformEditor* e = m_gen->editor();
        m_gen->channel1()->setMode(GenChannel::Arbitrary);
        m_gen->loadLibraryWaveform(1, int(gen::LibraryFunction::Square));
        e->newDocument();
        e->newSegment();
        QVERIFY(e->modified());
        QSignalSpy ask(m_gen.get(), &GenController::editChannelNeedsConfirmation);
        m_gen->editChannel(1);
        QCOMPARE(ask.count(), 1);
        QCOMPARE(ask.first().first().toInt(), 1);
        QCOMPARE(e->segmentCount(), 2);   // untouched
        QVERIFY(e->modified());
        m_gen->editChannel(1, true);      // Discard
        QCOMPARE(e->segmentCount(), 1);
        QCOMPARE(e->documentName(), u"Square"_s);
        QVERIFY(!e->modified());
        // Editing the channel whose document the editor already holds keeps the editor state.
        e->setSegmentDuration(0.02);
        e->useInChannel(1);
        QVERIFY(e->modified());
        m_gen->editChannel(1);
        QCOMPARE(ask.count(), 1);
        QVERIFY(e->canUndo());
    }

    void sweepEnds()
    {
        GenChannel* a = m_gen->channel0();
        a->setSweepStart(100);
        a->setSweepStop(300);
        a->setSweepStep(100);
        a->setSweepInterval(60);
        a->setSweepBlockTiming(false);
        m_gen->startSweep(0);
        QTRY_VERIFY(a->sweeping());
        QTRY_VERIFY_WITH_TIMEOUT(m_gen->isIdle(), 3000);
        QVERIFY(!a->outputOn());
        QCOMPARE(a->readoutText(), u"OFF"_s);
        QVERIFY(std::abs(ai0()) < 0.01);
    }

    void restoreIgnoresHiddenOptions()
    {
        // Round 2 UI: no Engine combo, no "Whole 250 ms steps" switch; old sessions that set
        // them are restored with the defaults.
        m_gen->setEngineMode(GenController::Regenerate);
        m_gen->channel0()->setSweepBlockTiming(true);
        m_gen->channel1()->setSweepBlockTiming(true);
        m_gen->channel0()->setFrequency(1234);
        const QJsonObject saved = m_gen->saveSettings();
        m_gen->resetSettings();
        m_gen->restoreSettings(saved);
        QCOMPARE(m_gen->engineMode(), GenController::Auto);
        QVERIFY(!m_gen->channel0()->sweepBlockTiming());
        QVERIFY(!m_gen->channel1()->sweepBlockTiming());
        QCOMPARE(m_gen->channel0()->frequency(), 1234.0);   // everything else is restored
    }

    // Presets (built-in and saved, also ones saved by older versions) never bring back the hidden
    // "whole 250 ms sweep steps" option nor an engine choice: the panel offers neither.
    void presetsIgnoreHiddenOptions()
    {
        m_gen->setEngineMode(GenController::Regenerate);
        m_gen->loadBuiltInPreset(5);   // ELVISmx defaults (sweep 100 Hz to 1 kHz)
        QVERIFY(!m_gen->channel0()->sweepBlockTiming());
        QCOMPARE(m_gen->engineMode(), GenController::Auto);

        m_gen->channel0()->setFrequency(4321);
        m_gen->savePreset(u"old"_s);
        // An old preset: stored with the 250 ms option on.
        QJsonObject all = QJsonDocument::fromJson(QSettings().value(u"instruments/gen/presets"_s).toByteArray()).object();
        QVERIFY(all.contains(u"old"_s));
        QJsonObject preset = all.value(u"old"_s).toObject();
        QJsonArray channels = preset.value(u"channels"_s).toArray();
        QJsonObject c0 = channels[0].toObject();
        c0[u"sweepBlockTiming"_s] = true;
        channels[0] = c0;
        preset[u"channels"_s] = channels;
        preset[u"engineMode"_s] = u"Regenerate"_s;
        all[u"old"_s] = preset;
        QSettings().setValue(u"instruments/gen/presets"_s, QJsonDocument(all).toJson(QJsonDocument::Compact));

        m_gen->channel0()->setFrequency(100);
        m_gen->setEngineMode(GenController::Stream);
        m_gen->loadPreset(u"old"_s);
        QCOMPARE(m_gen->channel0()->frequency(), 4321.0);   // the preset itself is applied
        QVERIFY(!m_gen->channel0()->sweepBlockTiming());
        QVERIFY(!m_gen->channel1()->sweepBlockTiming());
        QCOMPARE(m_gen->engineMode(), GenController::Auto);
        m_gen->deletePreset(u"old"_s);
    }

    void singleHoldsThenReleases()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"ramp.csv"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# sampleRate=1000\n0.25\n0.5\n0.75\n1\n");
        f.close();
        m_gen->channel1()->setMode(GenChannel::Arbitrary);
        QVERIFY(m_gen->loadWaveform(1, QUrl::fromLocalFile(path)));
        QCOMPARE(m_gen->channel1()->updateRate(), 1000.0);
        m_gen->channel0()->setMode(GenChannel::Arbitrary);
        QVERIFY(m_gen->loadWaveform(0, QUrl::fromLocalFile(path)));
        m_gen->channel0()->setGain(2.0);
        QVERIFY(m_gen->canSingle());
        m_gen->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_gen->isIdle() && m_gen->channel0()->done(), 3000);
        QCOMPARE(m_gen->channel0()->state(), GenChannel::Done);
        QCOMPARE(m_gen->channel0()->heldLevel(), 2.0);
        QVERIFY(std::abs(ai0() - 2.0) < 0.01);   // holds its last sample
        m_gen->setOutput(0, false);                // release: 0 V
        QTRY_VERIFY(std::abs(ai0()) < 0.01);
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY(!m_gen->channel0()->done());
        // Run bar Stop / Stop all while idle and holding (audit gen #14): releases to 0 V.
        m_gen->runOnce();
        QTRY_VERIFY_WITH_TIMEOUT(m_gen->isIdle() && m_gen->channel0()->done(), 3000);
        QTRY_VERIFY(m_gen->holdsOutputs());
        QVERIFY(std::abs(ai0() - 2.0) < 0.01);
        m_gen->stop();
        QTRY_VERIFY(std::abs(ai0()) < 0.01);
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY(!m_gen->holdsOutputs());
        QVERIFY(!m_gen->channel0()->done());
        QCOMPARE(m_gen->channel0()->readoutText(), u"OFF"_s);
    }

    void safeStateOnSessionResetAndPanelClose()
    {
        // A channel holding the end of a Run Once returns to 0 V when a session is reset/loaded
        // or the panel closes (framework returnToSafeState -> applySafeState).
        QTemporaryDir dir;
        const QString path = dir.filePath(u"level.csv"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("# sampleRate=1000\n1.5\n1.5\n1.5\n1.5\n");
        f.close();
        for (int pass = 0; pass < 2; ++pass) {
            m_gen->resetSettings();
            m_gen->channel0()->setMode(GenChannel::Arbitrary);
            QVERIFY(m_gen->loadWaveform(0, QUrl::fromLocalFile(path)));
            const QJsonObject saved = m_gen->saveSettings();
            m_gen->runOnce();
            QTRY_VERIFY_WITH_TIMEOUT(m_gen->isIdle() && m_gen->channel0()->done(), 3000);
            QTRY_VERIFY(std::abs(ai0() - 1.5) < 0.01);
            if (pass == 0) {
                m_gen->resetForSession();
                m_gen->restoreSettings(saved);   // the loaded session, right after the reset
            } else {
                m_gen->closePanel();
            }
            QTRY_VERIFY(std::abs(ai0()) < 0.01);
            QTRY_VERIFY(m_gen->isIdle());
            QVERIFY(!m_gen->channel0()->done());
            QVERIFY(!m_gen->holdsOutputs());
        }
    }

    void dcLevel()
    {
        m_gen->channel0()->setMode(GenChannel::Dc);
        m_gen->channel0()->setDcLevel(3.3);
        m_gen->run();
        QTRY_COMPARE(m_gen->strategy(), GenController::StaticWrites);
        QTRY_VERIFY(std::abs(ai0() - 3.3) < 0.01);
        m_gen->channel0()->setDcLevel(-2);
        QTRY_VERIFY(std::abs(ai0() + 2) < 0.01);
        m_gen->stop();
        QTRY_VERIFY(m_gen->isIdle());
        QVERIFY(std::abs(ai0()) < 0.01);
    }

    void pauseAndResume()
    {
        m_gen->channel0()->setSweepInterval(1000);
        m_gen->run();
        QTRY_VERIFY(m_gen->channel0()->generating());
        m_gen->pause(u"test"_s);
        QTRY_VERIFY(m_gen->isPaused());
        QCOMPARE(m_gen->channel0()->state(), GenChannel::Paused);
        m_gen->resume();
        QTRY_VERIFY(m_gen->channel0()->generating());
    }

    void modeChangeStopsChannel()
    {
        m_gen->run();
        QTRY_VERIFY(m_gen->channel0()->generating());
        m_gen->channel0()->setMode(GenChannel::Dc);
        QVERIFY(!m_gen->channel0()->outputOn());
        QTRY_VERIFY(m_gen->isIdle());
    }

    void underflowMessageIsTheEngines()
    {
        // Second underflow: the engine's text reaches the banner unchanged (audit gen #17).
        const QString text = u"The output ran out of data because the computer could not keep up. "
                             u"The generator was stopped."_s;
        const mydaq::DeviceError shown = gen::userFacingError(mydaq::DeviceError(mydaq::DeviceError::Underflow, text));
        QCOMPARE(m_gen->describeError(shown), text);
        const mydaq::DeviceError other(mydaq::DeviceError::Timeout, u"x"_s);
        QCOMPARE(gen::userFacingError(other).code(), mydaq::DeviceError::Timeout);
    }

    void paletteDcLevel()
    {
        // Command palette "Set AO 0 to … V" (featured-apps.md 2, audit gen #16).
        bool found = false;
        for (const QVariant& c : m_gen->commands())
            found = found || c.toMap().value(u"id"_s) == u"dcLevel0"_s;
        QVERIFY(found);
        m_gen->setOutputPair(GenController::AudioOutputs);
        m_gen->invokeCommand(u"dcLevel0"_s, 2.5);
        QCOMPARE(m_gen->outputPair(), GenController::AnalogOutputs);
        QCOMPARE(m_gen->channel0()->mode(), GenChannel::Dc);
        QCOMPARE(m_gen->channel0()->dcLevel(), 2.5);
        QTRY_VERIFY(std::abs(ai0() - 2.5) < 0.01);
        m_gen->invokeCommand(u"dcLevel0"_s, -1.0);   // live while running
        QTRY_VERIFY(std::abs(ai0() + 1.0) < 0.01);
    }

    void persistence()
    {
        m_gen->channel0()->setFrequency(1234);
        m_gen->channel1()->setMode(GenChannel::Arbitrary);
        m_gen->loadLibraryWaveform(1, int(gen::LibraryFunction::Trapezoid));
        m_gen->setOutputPair(GenController::AudioOutputs);
        QCOMPARE(m_gen->channel0()->name(), u"AudioOutput Left"_s);
        const QJsonObject saved = m_gen->saveSettings();
        m_gen->resetSettings();
        QCOMPARE(m_gen->channel0()->frequency(), 100.0);
        QVERIFY(!m_gen->channel1()->hasWaveform());
        QCOMPARE(m_gen->channel0()->name(), u"AO 0"_s);
        m_gen->restoreSettings(saved);
        QCOMPARE(m_gen->channel0()->frequency(), 1234.0);
        QCOMPARE(m_gen->channel1()->mode(), GenChannel::Arbitrary);
        QCOMPARE(m_gen->channel1()->waveformPoints(), 1000);
        QCOMPARE(m_gen->outputPair(), GenController::AudioOutputs);
        QCOMPARE(m_gen->channel1()->name(), u"AudioOutput Right"_s);
    }

    void editorEditsUndoAndUse()
    {
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        QCOMPARE(e->segmentCount(), 1);
        QCOMPARE(e->totalSamples(), 100000);
        QVERIFY(!e->modified());
        e->setSegmentDuration(0.01);
        e->setSampleRate(100000);
        e->newComponent();
        QCOMPARE(e->selectionKind(), 2);
        QCOMPARE(e->libraryFunction(), int(gen::LibraryFunction::Sine));
        e->setParam(2, 1000);   // Freq
        QCOMPARE(e->document().segments[0].components[0].param("frequency"), 1000.0);
        QVERIFY(e->modified());
        QCOMPARE(e->rows()->rowCount(), 2);
        e->newSegment();
        QCOMPARE(e->segmentCount(), 2);
        e->undo();
        QCOMPARE(e->segmentCount(), 1);
        e->redo();
        QCOMPARE(e->segmentCount(), 2);
        e->undo();
        // Expression errors
        e->selectRow(1);
        e->setComponentType(1);
        e->setExpression(u"sin(2*pi*x"_s);
        QCOMPARE(e->expressionError(), u"Missing ')' (11)"_s);
        e->undo();
        e->undo();
        QCOMPARE(e->componentType(), 0);
        // Too many samples refused
        e->selectSegment(0);
        e->setSegmentDuration(1000);   // 1e8 samples at 100 kS/s
        QCOMPARE(e->segmentDuration(), 0.01);
        QVERIFY(e->statusText().contains(u"max 500 000"_s));
        // Use in AO 1: Arbitrary, the document's samples and rate.
        e->useInChannel(1);
        QCOMPARE(m_gen->channel1()->mode(), GenChannel::Arbitrary);
        QCOMPARE(m_gen->channel1()->waveformPoints(), 1000);
        QCOMPARE(m_gen->channel1()->updateRate(), 100000.0);
        QVERIFY(m_gen->channel1()->waveformIsDocument());
        // Edit from the channel: the editor gets the channel's document.
        e->newDocument();
        m_gen->editChannel(1);
        QCOMPARE(m_gen->activeTab(), 1);
        QCOMPARE(e->document().segments[0].duration, 0.01);
        QCOMPARE(e->linkedChannel(), 1);
    }

    void editorSketchDrawing()
    {
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->setSegmentDuration(1.0);
        e->newComponent();
        e->setComponentType(2);
        e->setSketchSamples(11);
        e->setDrawMode(true);
        QVERIFY(e->drawMode());
        QCOMPARE(e->view(), 2);
        QCOMPARE(e->drawSpan(), 1.0);
        e->drawBegin(0.0, 1.0);
        e->drawTo(1.0, -1.0);
        e->drawEnd();
        const auto& samples = e->document().segments[0].components[0].samples;
        QCOMPARE(samples.front(), 1.0);
        QCOMPARE(samples.back(), -1.0);
        QVERIFY(std::abs(samples[5]) < 1e-12);   // linear fill between pointer positions
        e->undo();
        QCOMPARE(e->document().segments[0].components[0].samples[0], 0.0);
        e->setDrawTool(1);
        e->addVertex(0.5, 2.0);
        QCOMPARE(e->vertices().size(), 3);
        QCOMPARE(e->document().segments[0].components[0].samples[5], 2.0);
        e->removeVertex(1);
        QCOMPARE(e->vertices().size(), 2);
        e->setDrawMode(false);
        QCOMPARE(e->view(), 0);
    }

    void editorScaleAndClipboard()
    {
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->newComponent();
        e->selectSegment(0);
        e->scaleSelection(0.5, 2.0);
        QCOMPARE(e->segmentDuration(), 5.0);
        QCOMPARE(e->segmentGain(), 2.0);
        QCOMPARE(e->document().segments[0].components[0].param("frequency"), 2.0);
        e->copy();
        QVERIFY(e->canPaste());
        e->paste();
        QCOMPARE(e->segmentCount(), 2);
        QCOMPARE(e->segmentIndex(), 1);
        e->selectAll();
        e->deleteSelection();
        QCOMPARE(e->segmentCount(), 1);
        QCOMPARE(e->segmentDuration(), 10.0);
    }

    void editorMaxPoints()
    {
        // Max Points (Settings): 500 000 by default as ELVISmx, adjustable up to 10 M (editor #2).
        WaveformEditor* e = m_gen->editor();
        e->resetPreferences();
        QCOMPARE(e->maxPoints(), 500000);
        e->newDocument();                  // 10 s at 10 kS/s = 100 000 samples
        e->setSampleRate(100000);          // 1 000 000 samples: refused
        QCOMPARE(e->sampleRate(), 10000.0);
        QVERIFY2(e->statusText().contains(u"max 500 000"_s), qPrintable(e->statusText()));
        e->setMaxPoints(2000000);
        e->setSampleRate(100000);
        QCOMPARE(e->sampleRate(), 100000.0);
        e->setMaxPoints(50);               // coerced to the minimum
        QCOMPARE(e->maxPoints(), 1000);
        e->setMaxPoints(20000000);
        QCOMPARE(e->maxPoints(), 10000000);
        QCOMPARE(QSettings().value(u"instruments/gen/editor/maxPoints"_s).toInt(), 10000000);
        // A document already above Max Points stays editable (no dead end): only growing it is refused.
        e->setMaxPoints(1000);
        e->newDocument();                  // 100 000 samples
        QVERIFY(e->statusText().contains(u"above Max Points"_s));
        e->setSegmentDuration(5);          // shorter: accepted
        QCOMPARE(e->segmentDuration(), 5.0);
        e->setSegmentDuration(20);         // longer than both: refused
        QCOMPARE(e->segmentDuration(), 5.0);
        e->resetPreferences();
    }

    void editorSettingsColors()
    {
        // Settings dialog values (editor #1): colors (invalid = theme), Animate Graphs, persisted.
        WaveformEditor* e = m_gen->editor();
        e->resetPreferences();
        QCOMPARE(e->plotColor().alpha(), 0);   // theme color
        QVERIFY(!e->animateGraphs());
        e->setPlotColor(QColor(u"#0000ff"_s));
        e->setSelectionColor(QColor(u"#ffff99"_s));
        e->setGridColor(QColor(u"#cccccc"_s));
        e->setCursorColor(QColor(u"#000000"_s));
        e->setBackgroundColor(QColor(u"#ffffff"_s));
        e->setAnimateGraphs(true);
        WaveformEditor other;   // a new editor reads the saved preferences
        QCOMPARE(other.plotColor(), QColor(u"#0000ff"_s));
        QCOMPARE(other.selectionColor(), QColor(u"#ffff99"_s));
        QCOMPARE(other.backgroundColor(), QColor(u"#ffffff"_s));
        QVERIFY(other.animateGraphs());
        e->setGridColor(Qt::transparent);      // back to the theme
        QCOMPARE(e->gridColor().alpha(), 0);
        e->resetPreferences();
        QCOMPARE(e->plotColor().alpha(), 0);
    }

    void editorUnitsReinterpret()
    {
        // Units = the unit system of the document (verified in ELVISmx): the numbers stay, their
        // meaning changes (Hz/s, kHz/ms, MHz/us); sample counts are unchanged (editor #3).
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->newComponent();                 // Sine, Freq 1 Hz
        QCOMPARE(e->timeUnit(), u"s"_s);
        QCOMPARE(e->rateUnit(), u"S/s"_s);
        e->setUnits(1);
        QCOMPARE(e->unitName(), u"kHz"_s);
        QCOMPARE(e->timeUnit(), u"ms"_s);
        QCOMPARE(e->rateUnit(), u"kS/s"_s);
        QCOMPARE(e->displayScale(), 1000.0);
        QCOMPARE(e->sampleRate(), 1e7);    // "10000.0" now means 10 000 kS/s
        QCOMPARE(e->document().segments[0].duration, 0.01);   // "10" means 10 ms
        QCOMPARE(e->totalSamples(), 100000);
        QCOMPARE(e->document().segments[0].components[0].param("frequency"), 1000.0);
        QCOMPARE(e->params().at(2).toMap().value(u"value"_s).toDouble(), 1.0);   // Freq (kHz) 1
        QVERIFY2(e->rows()->rows().front().text.contains(u"(10 ms)"_s), qPrintable(e->rows()->rows().front().text));
        e->selectSegment(0);
        e->setSegmentDuration(0.005);      // 5 ms
        QCOMPARE(e->totalSamples(), 50000);
        e->undo();
        e->setUnits(2);
        QCOMPARE(e->timeUnit(), u"µs"_s);
        QCOMPARE(e->sampleRate(), 1e10);
        e->setUnits(0);
        QCOMPARE(e->sampleRate(), 10000.0);
        QCOMPARE(e->document().segments[0].duration, 10.0);
        QCOMPARE(e->document().segments[0].components[0].param("frequency"), 1.0);
        e->undo();                         // units change is undoable
        QCOMPARE(e->units(), 2);
    }

    void editorNewComponentRemembersFunction()
    {
        // New Component uses the function last chosen in the combo (editor #8).
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->newComponent();
        QCOMPARE(e->libraryFunction(), int(gen::LibraryFunction::Sine));
        e->setLibraryFunction(int(gen::LibraryFunction::Pulse));
        e->newComponent();
        QCOMPARE(e->libraryFunction(), int(gen::LibraryFunction::Pulse));
        QCOMPARE(e->document().segments[0].components[1].param("delay"), 20.0);
        e->setComponentType(1);
        e->setComponentType(0);
        QCOMPARE(e->libraryFunction(), int(gen::LibraryFunction::Pulse));
    }

    void editorDrawModeKeepsXAxis()
    {
        // Leaving Draw restores the View and the X Axis (editor #17).
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->newComponent();
        e->setComponentType(2);
        e->setXAxis(1);
        e->setView(1);
        e->setDrawMode(true);
        QCOMPARE(e->view(), 2);
        QCOMPARE(e->xAxis(), 0);
        e->setDrawMode(false);
        QCOMPARE(e->view(), 1);
        QCOMPARE(e->xAxis(), 1);
    }

    void editorExpressionTypingIsOneUndoStep()
    {
        // The expression is applied while typing (live error text), one undo step (editor #18).
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->newComponent();
        e->setComponentType(1);
        const int steps = e->undoStack()->count();
        for (const QString& text : {u"s"_s, u"si"_s, u"sin"_s, u"sin("_s, u"sin(x"_s, u"sin(x)"_s})
            e->setExpression(text);
        QCOMPARE(e->expression(), u"sin(x)"_s);
        QCOMPARE(e->undoStack()->count(), steps + 1);
        e->undo();
        QCOMPARE(e->expression(), u"sin(2*pi*x)"_s);
    }

    void editorFilesWdtAndWfm()
    {
        // Export .wdt (editor #21), refuse unknown extensions, open NI .wfm documents.
        QTemporaryDir dir;
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->setSegmentDuration(0.1);
        e->newComponent();
        const QString wdt = dir.filePath(u"out.wdt"_s);
        QVERIFY(e->exportFile(QUrl::fromLocalFile(wdt)));
        QString error;
        const auto back = gen::loadWaveformFile(wdt, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->samples.size(), size_t(1000));
        QCOMPARE(back->sampleRate, 10000.0);
        QVERIFY(!e->exportFile(QUrl::fromLocalFile(dir.filePath(u"out.xyz"_s))));
        QVERIFY(!QFile::exists(dir.filePath(u"out.xyz"_s)));

        gen::Document doc;
        doc.units = gen::FrequencyUnit::kHz;
        doc.sampleRate = 1e7;
        gen::Component sine = gen::Component::library(gen::LibraryFunction::Sine);
        sine.setParam("frequency", 2000);   // 2 kHz
        gen::Component expr = gen::Component::makeExpression(u"x^2"_s);
        expr.op = gen::Operator::Multiply;
        doc.segments = {gen::Segment{u"burst"_s, 0.001, 1.0, {sine, expr}}};
        const QString wfm = dir.filePath(u"burst.wfm"_s);
        QFile f(wfm);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(gen::encodeWfm(doc));
        f.close();
        QVERIFY(e->openFile(QUrl::fromLocalFile(wfm)));
        QCOMPARE(e->units(), 1);
        QCOMPARE(e->segmentCount(), 1);
        QCOMPARE(e->document().segments[0].name, u"burst"_s);
        QCOMPARE(e->document().segments[0].components.size(), size_t(2));
        QCOMPARE(e->document().segments[0].components[0].param("frequency"), 2000.0);
        QCOMPARE(e->document().segments[0].components[1].op, gen::Operator::Multiply);
        QCOMPARE(e->documentName(), u"burst"_s);
        QVERIFY(!gen::parseWfm(QByteArray("garbage"), &error));
    }

    void editorExportAndCopyData()
    {
        // Plot "Export data" on the editor tab exports the rendered waveform; Copy data is TSV.
        WaveformEditor* e = m_gen->editor();
        e->newDocument();
        e->setSegmentDuration(0.01);   // 100 samples
        e->newComponent();
        QTRY_VERIFY(e->renderedSamples() && e->renderedSamples()->size() == 100);
        m_gen->setActiveTab(1);
        QTemporaryDir dir;
        const QString csv = dir.filePath(u"editor.csv"_s);
        QVERIFY(m_gen->exportData(QUrl::fromLocalFile(csv)));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.count('\n') >= 100);
        m_gen->setActiveTab(0);
        QVERIFY(e->copyData());
        const QString text = QGuiApplication::clipboard()->text();
        QVERIFY(text.startsWith(u"Time (s)\tAmplitude (V)\n"_s));
        QCOMPARE(text.count(u'\n'), 101);
    }

    void editorTraceStylePersists()
    {
        // Plot Style legend: the trace style is a preference; the quick controls map into it.
        WaveformEditor* e = m_gen->editor();
        e->resetPreferences();
        e->setPlotStyle(2);            // Points
        QCOMPARE(e->traceStyle()->lineWidth(), 0.0);
        QCOMPARE(e->plotStyle(), 2);
        e->setPlotStyle(0);
        QCOMPARE(e->traceStyle()->lineWidth(), 1.5);
        e->setStepPlot(true);
        e->setPlotColor(QColor(u"#ff0000"_s));
        QCOMPARE(e->traceStyle()->lineColor(), QColor(u"#ff0000"_s));
        WaveformEditor other;
        QVERIFY(other.stepPlot());
        QCOMPARE(other.plotColor(), QColor(u"#ff0000"_s));
        e->resetPreferences();
        QVERIFY(!e->stepPlot());
    }

private:
    QTemporaryDir m_settingsDir;
    std::unique_ptr<GenController> m_gen;
};

QTEST_MAIN(TestGenController)
#include "tst_gencontroller.moc"
