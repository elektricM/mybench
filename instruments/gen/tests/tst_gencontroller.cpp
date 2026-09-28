// GenController and WaveformEditor against the simulator: run/stop per channel, strategies,
// sweep end, Run Once hold and release, pause/resume, persistence, editor edits and undo.
#include "gencontroller.h"

#include <mybench/framework.h>

#include <mydaq/device_manager.h>
#include <mydaq/sim_device.h>

#include <QFile>
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
        QTRY_COMPARE(m_gen->strategy(), GenController::RegeneratedBuffer);
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
        QTRY_COMPARE(m_gen->strategy(), GenController::RegeneratedBuffer);
        m_gen->setOutput(1, true);
        QTRY_COMPARE(m_gen->strategy(), GenController::Streaming);
        QTRY_VERIFY(m_gen->channel1()->generating());
        QTRY_COMPARE(m_gen->channel1()->readoutText(), u"100.0 kS/s"_s);
        m_gen->setOutput(1, false);
        QTest::qWait(150);
        QVERIFY(m_gen->isRunning());
        QVERIFY(m_gen->channel0()->generating());
        QCOMPARE(m_gen->strategy(), GenController::Streaming);   // no downgrade
        m_gen->setOutput(0, false);
        QTRY_VERIFY(m_gen->isIdle());
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
        QVERIFY(e->statusText().contains(u"max 10 000 000"_s));
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

private:
    QTemporaryDir m_settingsDir;
    std::unique_ptr<GenController> m_gen;
};

QTEST_MAIN(TestGenController)
#include "tst_gencontroller.moc"
