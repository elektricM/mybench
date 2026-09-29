// DioController: Digital I/O (docs/instruments/dio.md; ELVISmx behaviour in
// docs/instruments/algorithms/dio.md) — the ELVISmx Digital Reader and Digital Writer merged.
//
// Every line is In (read), Out (driven) or unused. The ELVISmx group presets (0 - 3, 4 - 7,
// 0 - 7) remain the main way to choose them; a line is never read and driven at once. One worker
// polls all eight lines every sample interval (LEDs, input value, timeline, output readback
// check) and drives the OUT lines: the manual pattern (written at once on every change) or a
// predefined sequence (Ramp, Alternating 1/0's, Walking 1's) stepped every step interval.
// Only the lines actually used are declared to the ResourceArbiter; lines declared by other
// instruments (Counter/Timer terminals, FREQOUT on DIO 4) are reported per line.
//
// Outputs keep driving their last value after Stop (ELVISmx); they go back to inputs when their
// direction changes, when another instrument takes the line (pause), on Release outputs and when
// the application quits.
#pragma once

#include "diologic.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/logicbuffer.h>

#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

namespace mybench::instruments {

class DioController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings (bit i = DIO i; inputs before outputs) --------------------------------------
    Q_PROPERTY(int inputMask READ inputMask WRITE setInputMask NOTIFY directionsChanged)
    Q_PROPERTY(int outputMask READ outputMask WRITE setOutputMask NOTIFY directionsChanged)
    Q_PROPERTY(Radix inputRadix READ inputRadix WRITE setInputRadix NOTIFY inputRadixChanged)
    Q_PROPERTY(Radix outputRadix READ outputRadix WRITE setOutputRadix NOTIFY outputRadixChanged)
    Q_PROPERTY(WriterPattern pattern READ pattern WRITE setPattern NOTIFY patternChanged)
    // Manual pattern number, right-justified over the OUT lines (0 ... outputMax).
    Q_PROPERTY(int manualValue READ manualValue WRITE setManualValue NOTIFY manualValueChanged)
    Q_PROPERTY(ShiftDirection shiftDirection READ shiftDirection WRITE setShiftDirection NOTIFY shiftDirectionChanged)
    Q_PROPERTY(int stepInterval READ stepInterval WRITE setStepInterval NOTIFY stepIntervalChanged)       // ms
    Q_PROPERTY(int sampleInterval READ sampleInterval WRITE setSampleInterval NOTIFY sampleIntervalChanged) // ms
    Q_PROPERTY(int timelineWindow READ timelineWindow WRITE setTimelineWindow NOTIFY timelineWindowChanged) // 0..3: 5/10/30/60 s

    // --- View state (not settings) -------------------------------------------------------------
    Q_PROPERTY(bool timelinePaused READ timelinePaused WRITE setTimelinePaused NOTIFY timelinePausedChanged STORED false)

    // --- Derived --------------------------------------------------------------------------------
    Q_PROPERTY(int readPreset READ readPreset NOTIFY directionsChanged)     // dio::Preset
    Q_PROPERTY(int writePreset READ writePreset NOTIFY directionsChanged)
    Q_PROPERTY(int inputWidth READ inputWidth NOTIFY directionsChanged)
    Q_PROPERTY(int outputWidth READ outputWidth NOTIFY directionsChanged)
    Q_PROPERTY(int outputMax READ outputMax NOTIFY directionsChanged)
    Q_PROPERTY(QStringList patternNames READ patternNames NOTIFY directionsChanged)
    Q_PROPERTY(QString readLinesLabel READ readLinesLabel NOTIFY directionsChanged)
    Q_PROPERTY(QString writeLinesLabel READ writeLinesLabel NOTIFY directionsChanged)
    Q_PROPERTY(double timelineSeconds READ timelineSeconds NOTIFY timelineWindowChanged)
    Q_PROPERTY(QStringList lineLabels READ lineLabels NOTIFY lineLabelsChanged)

    // --- Results --------------------------------------------------------------------------------
    Q_PROPERTY(int lineStates READ lineStates NOTIFY lineStatesChanged)       // last dioRead(), all lines
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY lineStatesChanged)
    Q_PROPERTY(int inputValue READ inputValue NOTIFY lineStatesChanged)       // right-justified IN value
    Q_PROPERTY(int writtenLevels READ writtenLevels NOTIFY writtenChanged)    // levels last written (line bits)
    Q_PROPERTY(int writtenValue READ writtenValue NOTIFY writtenChanged)      // right-justified, 0 until the first write
    Q_PROPERTY(bool hasWritten READ hasWritten NOTIFY writtenChanged)
    Q_PROPERTY(int readbackErrors READ readbackErrors NOTIFY lineStatesChanged) // OUT lines reading back wrong
    Q_PROPERTY(int drivenMask READ drivenMask NOTIFY drivenMaskChanged)       // lines configured as outputs by us
    Q_PROPERTY(int sequenceStep READ sequenceStep NOTIFY writtenChanged)      // index in the running sequence

    // Lines declared by other instruments (Counter/Timer, FREQOUT): per line { owner, ownerId,
    // active (running or paused), text }; `foreignMask` = lines declared by anyone else,
    // `conflictMask` = our lines that another *active* instrument holds (Run pauses it).
    Q_PROPERTY(QVariantList lineOwners READ lineOwners NOTIFY ownersChanged)
    Q_PROPERTY(int foreignMask READ foreignMask NOTIFY ownersChanged)
    Q_PROPERTY(int conflictMask READ conflictMask NOTIFY ownersChanged)

    // Timeline of the 8 lines (x = seconds since the recording started).
    Q_PROPERTY(mybench::plot::LogicBuffer* timeline READ timeline CONSTANT)
    Q_PROPERTY(double timelineEnd READ timelineEnd NOTIFY timelineAdvanced)

public:
    enum Radix { Decimal, Hex, Octal, Binary, SiNotation };
    Q_ENUM(Radix)
    enum WriterPattern { Manual, Ramp, Alternating, WalkingOnes };
    Q_ENUM(WriterPattern)
    enum ShiftDirection { Left, Right };
    Q_ENUM(ShiftDirection)
    enum LineDirection { In, Out, Unused };
    Q_ENUM(LineDirection)
    enum WriterAction { Toggle, Rotate, Shift };
    Q_ENUM(WriterAction)

    DioController();
    ~DioController() override;

    int inputMask() const { return m_inputMask; }
    void setInputMask(int mask);
    int outputMask() const { return m_outputMask; }
    void setOutputMask(int mask);
    Radix inputRadix() const { return m_inputRadix; }
    void setInputRadix(Radix radix);
    Radix outputRadix() const { return m_outputRadix; }
    void setOutputRadix(Radix radix);
    WriterPattern pattern() const { return m_pattern; }
    void setPattern(WriterPattern pattern);
    int manualValue() const { return m_manualValue; }
    void setManualValue(int value);
    ShiftDirection shiftDirection() const { return m_shiftDirection; }
    void setShiftDirection(ShiftDirection direction);
    int stepInterval() const { return m_stepInterval; }
    void setStepInterval(int milliseconds);
    int sampleInterval() const { return m_sampleInterval; }
    void setSampleInterval(int milliseconds);
    int timelineWindow() const { return m_timelineWindow; }
    void setTimelineWindow(int window);
    bool timelinePaused() const { return m_timelinePaused; }
    void setTimelinePaused(bool paused);

    int readPreset() const { return dio::presetOf(uint8_t(m_inputMask)); }
    int writePreset() const { return dio::presetOf(uint8_t(m_outputMask)); }
    int inputWidth() const { return dio::lineCount(uint8_t(m_inputMask)); }
    int outputWidth() const { return dio::lineCount(uint8_t(m_outputMask)); }
    int outputMax() const { return dio::maxValue(outputWidth()); }
    QStringList patternNames() const;
    QString readLinesLabel() const { return dio::linesLabel(uint8_t(m_inputMask)); }
    QString writeLinesLabel() const { return dio::linesLabel(uint8_t(m_outputMask)); }
    double timelineSeconds() const;
    QStringList lineLabels() const { return m_lineLabels; }

    int lineStates() const { return m_lineStates; }
    bool hasReading() const { return m_hasReading; }
    int inputValue() const { return dio::gather(uint8_t(m_lineStates), uint8_t(m_inputMask)); }
    int writtenLevels() const { return m_writtenLevels; }
    // Right-justified value last written; 0 before the first write (ELVISmx shows `x 0` at launch).
    int writtenValue() const { return std::max(0, m_writtenValue); }
    bool hasWritten() const { return m_writtenValue >= 0; }
    int readbackErrors() const { return m_readbackErrors; }
    int drivenMask() const { return m_drivenMask; }
    int sequenceStep() const { return m_sequenceStep; }
    QVariantList lineOwners() const { return m_lineOwners; }
    int foreignMask() const { return m_foreignMask; }
    int conflictMask() const { return m_conflictMask; }
    mybench::plot::LogicBuffer* timeline() { return &m_timeline; }
    double timelineEnd() const { return m_timelineEnd; }

    // ELVISmx "Lines to Read" / "Lines to Write" (dio::Preset: 0 - 3, 4 - 7, 0 - 7, None).
    // Lines taken from the other function move to this one (toast).
    Q_INVOKABLE void setReadPreset(int preset);
    Q_INVOKABLE void setWritePreset(int preset);
    Q_INVOKABLE void setLineDirection(int line, LineDirection direction);
    Q_INVOKABLE LineDirection lineDirection(int line) const;
    // Manual pattern: flips the bit of an OUT line; applies an action (Toggle/Rotate/Shift).
    Q_INVOKABLE void toggleLine(int line);
    Q_INVOKABLE void applyAction(WriterAction action);
    Q_INVOKABLE void setLineLabel(int line, const QString& label);
    Q_INVOKABLE QString lineName(int line) const;   // "DIO 3" or "DIO 3 · LED"
    Q_INVOKABLE QString formatValue(int value, Radix radix, int width) const;
    // Numeric Value indicator text: unpadded (`b 1101`, `x D`), `0` before the first sample/write.
    Q_INVOKABLE QString numericValueText(bool output) const;
    // Pattern Number entry: the value of `text` in `radix`, -1 when malformed or above the maximum.
    Q_INVOKABLE int parseEntry(const QString& text, Radix radix) const;
    // Digital Waveform Viewer cursor: the line values at time `t` in `radix` ("In x 5 · Out x A").
    Q_INVOKABLE QString valuesAtTime(double t, Radix radix) const;
    Q_INVOKABLE static QString radixLetter(Radix radix);
    Q_INVOKABLE void clearTimeline();
    // Idle: lines still driven since the last run go back to inputs.
    Q_INVOKABLE void releaseOutputs();

signals:
    void directionsChanged();
    void inputRadixChanged();
    void outputRadixChanged();
    void patternChanged();
    void manualValueChanged();
    void shiftDirectionChanged();
    void stepIntervalChanged();
    void sampleIntervalChanged();
    void timelineWindowChanged();
    void timelinePausedChanged();
    void lineLabelsChanged();
    void lineStatesChanged();
    void writtenChanged();
    void drivenMaskChanged();
    void ownersChanged();
    void timelineAdvanced();

protected:
    WorkerFunction createWorker() override;
    void onFinished() override;
    void applySafeState() override;   // panel closed / new session / Stop while idle: lines to inputs
    void saveExtraSettings(QJsonObject& settings) const override;
    void restoreExtraSettings(const QJsonObject& settings) override;
    DataTable exportTable() const override;

private:
    void forgetDrivenLines();   // device reset / unplugged: nothing is driven any more
    // Values the running worker picks up at its next iteration.
    struct Live {
        dio::Pattern pattern = dio::Manual;
        int manualValue = 0;
        dio::Direction direction = dio::Left;
        int stepMs = dio::kDefaultIntervalMs;
        int sampleMs = dio::kDefaultIntervalMs;
    };
    // Timeline samples kept for Export data (thread-safe, bounded).
    struct SampleLog {
        std::mutex mutex;
        std::deque<std::pair<double, uint8_t>> samples;   // (t, all 8 levels)
        void add(double t, uint8_t levels);
        void clear();
    };

    Live liveSnapshot() const;
    void postLive();
    void applyDirections(uint8_t inputs, uint8_t outputs, const QString& movedMessage);
    void directionsEdited();
    void updateResources();
    void updateOwners();
    void watchController(InstrumentController* controller);
    void releaseLines(uint8_t mask, bool synchronous);

    // Settings
    int m_inputMask = 0xF0;    // Lines to Read 4 - 7 (ELVISmx Digital Reader default)
    int m_outputMask = 0x0F;   // Lines to Write 0 - 3 (ELVISmx Digital Writer default)
    Radix m_inputRadix = Hex;
    Radix m_outputRadix = Hex;
    WriterPattern m_pattern = Manual;
    int m_manualValue = 0;
    ShiftDirection m_shiftDirection = Left;
    int m_stepInterval = dio::kDefaultIntervalMs;
    int m_sampleInterval = dio::kDefaultIntervalMs;
    int m_timelineWindow = 1;   // 10 s
    QStringList m_lineLabels = QStringList(dio::kLines, QString());

    bool m_timelinePaused = false;
    bool m_restarting = false;   // createWorker() for a live restart (keeps the timeline)

    // Results
    int m_lineStates = 0;
    bool m_hasReading = false;
    int m_writtenLevels = 0;
    int m_writtenValue = -1;
    int m_readbackErrors = 0;
    int m_drivenMask = 0;
    int m_sequenceStep = -1;
    QVariantList m_lineOwners;
    int m_foreignMask = 0;
    int m_conflictMask = 0;
    double m_timelineEnd = 0;

    std::shared_ptr<Mailbox<Live>> m_live = std::make_shared<Mailbox<Live>>();
    std::shared_ptr<std::atomic<bool>> m_releaseOnExit = std::make_shared<std::atomic<bool>>(false);
    // Lines the newest run drives (UI thread writes, release jobs read): a queued release never
    // undrives lines a later run has claimed.
    std::shared_ptr<std::atomic<uint8_t>> m_claimed = std::make_shared<std::atomic<uint8_t>>(0);
    // Asynchronous releases still queued on the thread pool; the destructor waits for them so a
    // late release of this controller cannot undrive the lines of the next DIO run.
    std::shared_ptr<std::atomic<int>> m_pendingReleases = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::mutex> m_dioMutex = std::make_shared<std::mutex>();   // direction changes
    std::shared_ptr<SampleLog> m_samples = std::make_shared<SampleLog>();
    std::chrono::steady_clock::time_point m_origin = std::chrono::steady_clock::now();
    mybench::plot::LogicBuffer m_timeline;
    QSet<InstrumentController*> m_watched;
};

void registerDio(InstrumentRegistry& registry);

}  // namespace mybench::instruments
