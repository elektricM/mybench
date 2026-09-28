// CtrController: the Counter/Timer (docs/instruments/ctr.md, help docs/help/ctr.md). No ELVISmx
// equivalent: it exposes the myDAQ counter ctr0 and the FREQOUT generator.
//
// Two independent functions share one run:
//   - ctr0 (`counterFunction`): Measure (frequency, period, pulse width, semi-period, two-edge
//     separation, edge counting, linear/angular encoders; on demand or buffered), Pulse Train
//     (continuous or finite, live frequency/duty) or Off;
//   - FREQOUT (`freqOutEnabled`): square wave of 20 MHz, 10 MHz or 100 kHz divided by 1..16.
// Their signals use DIO lines (default terminals of mydaq/device.h); the controller declares
// exactly those lines (plus Counter / FrequencyOutput) to the ResourceArbiter, and publishes a
// pin map with the lines other instruments hold.
#pragma once

#include "countermath.h"

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>
#include <mybench/plot/tracebuffer.h>
#include <mybench/worker.h>

#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

namespace mybench::instruments {

class CtrController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")

    // --- Settings (declaration order = restore order) --------------------------------------
    Q_PROPERTY(CounterFunction counterFunction READ counterFunction WRITE setCounterFunction NOTIFY counterFunctionChanged)
    Q_PROPERTY(Measurement measurement READ measurement WRITE setMeasurement NOTIFY measurementChanged)
    Q_PROPERTY(Slope activeEdge READ activeEdge WRITE setActiveEdge NOTIFY inputSettingsChanged)
    Q_PROPERTY(Slope secondEdge READ secondEdge WRITE setSecondEdge NOTIFY inputSettingsChanged)
    Q_PROPERTY(CountDirection countDirection READ countDirection WRITE setCountDirection NOTIFY inputSettingsChanged)
    Q_PROPERTY(double initialCount READ initialCount WRITE setInitialCount NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool resetOnDio1 READ resetOnDio1 WRITE setResetOnDio1 NOTIFY inputSettingsChanged)
    Q_PROPERTY(double frequencyMin READ frequencyMin WRITE setFrequencyMin NOTIFY rangeChanged)
    Q_PROPERTY(double frequencyMax READ frequencyMax WRITE setFrequencyMax NOTIFY rangeChanged)
    Q_PROPERTY(double timeMin READ timeMin WRITE setTimeMin NOTIFY rangeChanged)
    Q_PROPERTY(double timeMax READ timeMax WRITE setTimeMax NOTIFY rangeChanged)
    Q_PROPERTY(Decoding decoding READ decoding WRITE setDecoding NOTIFY inputSettingsChanged)
    Q_PROPERTY(double distancePerPulse READ distancePerPulse WRITE setDistancePerPulse NOTIFY inputSettingsChanged)
    Q_PROPERTY(QString distanceUnit READ distanceUnit WRITE setDistanceUnit NOTIFY inputSettingsChanged)
    Q_PROPERTY(int pulsesPerRevolution READ pulsesPerRevolution WRITE setPulsesPerRevolution NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool zIndexEnabled READ zIndexEnabled WRITE setZIndexEnabled NOTIFY inputSettingsChanged)
    Q_PROPERTY(double zIndexValue READ zIndexValue WRITE setZIndexValue NOTIFY inputSettingsChanged)
    Q_PROPERTY(Sampling sampling READ sampling WRITE setSampling NOTIFY samplingChanged)
    Q_PROPERTY(double updateInterval READ updateInterval WRITE setUpdateInterval NOTIFY samplingChanged)
    Q_PROPERTY(int bufferedSamples READ bufferedSamples WRITE setBufferedSamples NOTIFY samplingChanged)
    Q_PROPERTY(PulseDefinition pulseDefinition READ pulseDefinition WRITE setPulseDefinition NOTIFY pulseChanged)
    Q_PROPERTY(double pulseFrequency READ pulseFrequency WRITE setPulseFrequency NOTIFY pulseChanged)
    Q_PROPERTY(double dutyCycle READ dutyCycle WRITE setDutyCycle NOTIFY pulseChanged)
    Q_PROPERTY(bool idleHigh READ idleHigh WRITE setIdleHigh NOTIFY pulseChanged)
    Q_PROPERTY(double initialDelay READ initialDelay WRITE setInitialDelay NOTIFY pulseChanged)
    Q_PROPERTY(Generation pulseGeneration READ pulseGeneration WRITE setPulseGeneration NOTIFY pulseChanged)
    Q_PROPERTY(int pulseCount READ pulseCount WRITE setPulseCount NOTIFY pulseChanged)
    Q_PROPERTY(bool freqOutEnabled READ freqOutEnabled WRITE setFreqOutEnabled NOTIFY freqOutChanged)
    Q_PROPERTY(double freqOutFrequency READ freqOutFrequency WRITE setFreqOutFrequency NOTIFY freqOutChanged)

    // --- Derived from the settings -----------------------------------------------------------
    Q_PROPERTY(QStringList measurementNames READ measurementNames CONSTANT)
    Q_PROPERTY(bool intervalMeasurement READ intervalMeasurement NOTIFY configChanged)
    Q_PROPERTY(bool encoderMeasurement READ encoderMeasurement NOTIFY configChanged)
    Q_PROPERTY(bool bufferedAvailable READ bufferedAvailable NOTIFY configChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY configChanged)             // value unit ("Hz", "s", "°", "mm", "")
    Q_PROPERTY(QString functionTitle READ displayName NOTIFY configChanged)     // "Frequency", "Pulse Train", "FREQOUT"
    Q_PROPERTY(double timebase READ timebase NOTIFY configChanged)      // Hz, interval measurements
    Q_PROPERTY(QString timebaseText READ timebaseText NOTIFY configChanged)
    Q_PROPERTY(int plannedLines READ plannedLines NOTIFY configChanged) // DIO mask of the next/current run
    Q_PROPERTY(QString terminals READ terminals NOTIFY configChanged)   // "DIO 1, 4"
    // One entry per DIO line: { line, role, signal, owner: "input"|"pulse"|"freqout"|"",
    // heldBy: other instrument's title or "", conflict: bool }.
    Q_PROPERTY(QVariantList pins READ pins NOTIFY pinsChanged)
    Q_PROPERTY(QString conflictText READ conflictText NOTIFY pinsChanged)
    Q_PROPERTY(double coercedPulseFrequency READ coercedPulseFrequency NOTIFY pulseChanged)
    Q_PROPERTY(double coercedDutyCycle READ coercedDutyCycle NOTIFY pulseChanged)
    Q_PROPERTY(double highTime READ highTime NOTIFY pulseChanged)
    Q_PROPERTY(double lowTime READ lowTime NOTIFY pulseChanged)
    Q_PROPERTY(double minimumPulseFrequency READ minimumPulseFrequency CONSTANT)
    Q_PROPERTY(double maximumPulseFrequency READ maximumPulseFrequency CONSTANT)
    Q_PROPERTY(double coercedFreqOut READ coercedFreqOut NOTIFY freqOutChanged)
    Q_PROPERTY(QString freqOutDivider READ freqOutDivider NOTIFY freqOutChanged)   // "20 MHz ÷ 2"
    Q_PROPERTY(QStringList freqOutChoices READ freqOutChoices CONSTANT)            // table labels
    Q_PROPERTY(int freqOutIndex READ freqOutIndex NOTIFY freqOutChanged)

    // --- Results -----------------------------------------------------------------------------
    Q_PROPERTY(double value READ value NOTIFY resultsChanged)
    Q_PROPERTY(QString valueText READ valueText NOTIFY resultsChanged)   // formatted with unit
    Q_PROPERTY(QString secondaryText READ secondaryText NOTIFY resultsChanged)   // "Period 1.000 ms"
    Q_PROPERTY(double statMinimum READ statMinimum NOTIFY resultsChanged)
    Q_PROPERTY(double statMaximum READ statMaximum NOTIFY resultsChanged)
    Q_PROPERTY(double statMean READ statMean NOTIFY resultsChanged)
    Q_PROPERTY(double statDeviation READ statDeviation NOTIFY resultsChanged)
    Q_PROPERTY(double statCount READ statCount NOTIFY resultsChanged)
    Q_PROPERTY(double outputFrequency READ outputFrequency NOTIFY outputChanged)   // pulse train, actual
    Q_PROPERTY(double outputDutyCycle READ outputDutyCycle NOTIFY outputChanged)
    Q_PROPERTY(double freqOutActual READ freqOutActual NOTIFY outputChanged)       // 0 = not running
    Q_PROPERTY(QString outputState READ outputState NOTIFY outputChanged)          // "Generating", "Done"...
    // Trend of the measured values (x = seconds since the epoch) and the pulse-train preview.
    Q_PROPERTY(mybench::plot::TraceBuffer* trend READ trend CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer* pulsePreview READ pulsePreview CONSTANT)

public:
    enum CounterFunction { Measure, PulseTrain, CounterOff };
    Q_ENUM(CounterFunction)
    // Same order as mydaq::CounterMeasurement.
    enum Measurement { CountEdges, Frequency, Period, PulseWidth, SemiPeriod, TwoEdgeSeparation,
                       LinearEncoder, AngularEncoder };
    Q_ENUM(Measurement)
    enum Slope { Rising, Falling };
    Q_ENUM(Slope)
    enum CountDirection { CountUp, CountDown, CountExternal };
    Q_ENUM(CountDirection)
    enum Decoding { X1, X2, X4, TwoPulse };
    Q_ENUM(Decoding)
    enum Sampling { OnDemand, Buffered };
    Q_ENUM(Sampling)
    enum PulseDefinition { FrequencyAndDuty, HighAndLowTime };
    Q_ENUM(PulseDefinition)
    enum Generation { ContinuousPulses, FinitePulses };
    Q_ENUM(Generation)

    CtrController();
    ~CtrController() override;

    CounterFunction counterFunction() const { return m_counterFunction; }
    void setCounterFunction(CounterFunction function);
    Measurement measurement() const { return m_measurement; }
    void setMeasurement(Measurement measurement);
    Slope activeEdge() const { return m_activeEdge; }
    void setActiveEdge(Slope edge);
    Slope secondEdge() const { return m_secondEdge; }
    void setSecondEdge(Slope edge);
    CountDirection countDirection() const { return m_countDirection; }
    void setCountDirection(CountDirection direction);
    double initialCount() const { return m_initialCount; }
    void setInitialCount(double count);
    bool resetOnDio1() const { return m_resetOnDio1; }
    void setResetOnDio1(bool reset);
    double frequencyMin() const { return m_frequencyMin; }
    void setFrequencyMin(double hertz);
    double frequencyMax() const { return m_frequencyMax; }
    void setFrequencyMax(double hertz);
    double timeMin() const { return m_timeMin; }
    void setTimeMin(double seconds);
    double timeMax() const { return m_timeMax; }
    void setTimeMax(double seconds);
    Decoding decoding() const { return m_decoding; }
    void setDecoding(Decoding decoding);
    double distancePerPulse() const { return m_distancePerPulse; }
    void setDistancePerPulse(double distance);
    QString distanceUnit() const { return m_distanceUnit; }
    void setDistanceUnit(const QString& unit);
    int pulsesPerRevolution() const { return m_pulsesPerRevolution; }
    void setPulsesPerRevolution(int pulses);
    bool zIndexEnabled() const { return m_zIndexEnabled; }
    void setZIndexEnabled(bool enabled);
    double zIndexValue() const { return m_zIndexValue; }
    void setZIndexValue(double value);
    Sampling sampling() const { return m_sampling; }
    void setSampling(Sampling sampling);
    double updateInterval() const { return m_updateInterval; }
    void setUpdateInterval(double seconds);
    int bufferedSamples() const { return m_bufferedSamples; }
    void setBufferedSamples(int samples);
    PulseDefinition pulseDefinition() const { return m_pulseDefinition; }
    void setPulseDefinition(PulseDefinition definition);
    double pulseFrequency() const { return m_pulseFrequency; }
    void setPulseFrequency(double hertz);
    double dutyCycle() const { return m_dutyCycle; }
    void setDutyCycle(double duty);
    bool idleHigh() const { return m_idleHigh; }
    void setIdleHigh(bool high);
    double initialDelay() const { return m_initialDelay; }
    void setInitialDelay(double seconds);
    Generation pulseGeneration() const { return m_pulseGeneration; }
    void setPulseGeneration(Generation generation);
    int pulseCount() const { return m_pulseCount; }
    void setPulseCount(int count);
    bool freqOutEnabled() const { return m_freqOutEnabled; }
    void setFreqOutEnabled(bool enabled);
    double freqOutFrequency() const { return m_freqOutFrequency; }
    void setFreqOutFrequency(double hertz);

    QStringList measurementNames() const;
    bool intervalMeasurement() const;
    bool encoderMeasurement() const;
    bool bufferedAvailable() const { return intervalMeasurement(); }
    QString unit() const;
    QString displayName() const;
    double timebase() const;
    QString timebaseText() const;
    int plannedLines() const;
    QString terminals() const;
    QVariantList pins() const { return m_pins; }
    QString conflictText() const { return m_conflictText; }
    double coercedPulseFrequency() const;
    double coercedDutyCycle() const;
    double highTime() const;
    double lowTime() const;
    static double minimumPulseFrequency() { return ctr::minimumPulseFrequency(); }
    static double maximumPulseFrequency() { return ctr::maximumPulseFrequency(); }
    double coercedFreqOut() const;
    QString freqOutDivider() const;
    QStringList freqOutChoices() const;
    int freqOutIndex() const;

    double value() const { return m_value; }
    QString valueText() const;
    QString secondaryText() const;
    double statMinimum() const { return m_stats.minimum(); }
    double statMaximum() const { return m_stats.maximum(); }
    double statMean() const { return m_stats.mean(); }
    double statDeviation() const { return m_stats.standardDeviation(); }
    double statCount() const { return double(m_stats.count()); }
    double outputFrequency() const { return m_outputFrequency; }
    double outputDutyCycle() const { return m_outputDutyCycle; }
    double freqOutActual() const { return m_freqOutActual; }
    QString outputState() const { return m_outputState; }
    mybench::plot::TraceBuffer* trend() { return &m_trend; }
    mybench::plot::TraceBuffer* pulsePreview() { return &m_pulsePreview; }

    // The counter input configuration of the current settings (also used by the tests).
    mydaq::CounterInputConfig inputConfig() const;
    mydaq::PulseOutputConfig pulseConfig() const;

    // High/low time entry (HighAndLowTime definition): sets frequency and duty cycle.
    Q_INVOKABLE void setHighLowTimes(double high, double low);
    // Picks a FREQOUT table entry (freqOutChoices index).
    Q_INVOKABLE void selectFreqOut(int index);
    // Statistics and trend back to empty; counting measurements also restart from the initial count.
    Q_INVOKABLE void resetMeasurement();

signals:
    void counterFunctionChanged();
    void measurementChanged();
    void inputSettingsChanged();
    void rangeChanged();
    void samplingChanged();
    void pulseChanged();
    void freqOutChanged();
    void configChanged();
    void pinsChanged();
    void resultsChanged();
    void outputChanged();

protected:
    WorkerFunction createWorker() override;
    void onStarted() override;
    void onFinished() override;
    DataTable exportTable() const override;

private:
    struct Live {
        double pulseFrequency;
        double dutyCycle;
        double freqOut;
    };
    struct Reading {
        double value;
        double rate;
    };

    void updateResources();
    void updatePins();
    void updatePreview();
    // A setting that needs new device tasks changed: config/pins refresh, restart when running.
    void inputChanged(bool restartRun = true);
    void postLive();
    void clearResults();

    WorkerFunction measureLoop(std::shared_ptr<std::atomic<bool>> reset);

    CounterFunction m_counterFunction = Measure;
    Measurement m_measurement = Frequency;
    Slope m_activeEdge = Rising;
    Slope m_secondEdge = Falling;
    CountDirection m_countDirection = CountUp;
    double m_initialCount = 0;
    bool m_resetOnDio1 = false;
    double m_frequencyMin = 1.0;
    double m_frequencyMax = 1.0e6;
    double m_timeMin = 100e-9;
    double m_timeMax = 10.0;
    Decoding m_decoding = X4;
    double m_distancePerPulse = 1.0;
    QString m_distanceUnit = QStringLiteral("mm");
    int m_pulsesPerRevolution = 1024;
    bool m_zIndexEnabled = false;
    double m_zIndexValue = 0;
    Sampling m_sampling = OnDemand;
    double m_updateInterval = 0.1;
    int m_bufferedSamples = 1000;
    PulseDefinition m_pulseDefinition = FrequencyAndDuty;
    double m_pulseFrequency = 1000;
    double m_dutyCycle = 0.5;
    bool m_idleHigh = false;
    double m_initialDelay = 0;
    Generation m_pulseGeneration = ContinuousPulses;
    int m_pulseCount = 100;
    bool m_freqOutEnabled = false;
    double m_freqOutFrequency = 10000;

    QVariantList m_pins;
    QString m_conflictText;
    double m_value = qQNaN();
    double m_rate = qQNaN();
    ctr::RunningStats m_stats;
    double m_outputFrequency = 0;
    double m_outputDutyCycle = 0;
    double m_freqOutActual = 0;
    QString m_outputState;
    std::shared_ptr<Mailbox<Live>> m_live = std::make_shared<Mailbox<Live>>();
    std::shared_ptr<std::atomic<bool>> m_resetRequest = std::make_shared<std::atomic<bool>>(false);
    mybench::plot::TraceBuffer m_trend;
    mybench::plot::TraceBuffer m_pulsePreview;
};

void registerCtr(InstrumentRegistry& registry);

}  // namespace mybench::instruments
