// ResourceArbiter: who holds which part of the myDAQ.
//
// The hardware allows one analog-input task (AI 0/1 or AudioIn), one timed analog-output task
// (AO 0/1 or AudioOut), the isolated DMM, the counter/timer ctr0, the FREQOUT generator, and the
// 8 DIO lines individually (counter and FREQOUT signals use DIO lines too: declare them). Every
// InstrumentController declares what it needs (InstrumentController::setRequiredResources()).
// When an instrument starts and a resource is held by another one, the holder is *paused* with a
// reason ("Oscilloscope paused — AI in use by Bode Analyzer", full titles) and a Resume action,
// never an opaque error.
//
// All methods run on the UI thread. QML: the `ResourceArbiter` singleton (holders for the top-bar
// chips); C++: ResourceArbiter::instance().
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace mybench {

class InstrumentController;

class ResourceArbiter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // One entry per held resource, in the order AI, AO, DMM, DIO (one entry per DIO holder):
    // { resource: "AI", name: "analog input", lines: "" | "DIO 0–3", instrumentId, title, shortTitle }
    // (order AI, AO, DMM, CTR, FREQOUT, DIO)
    Q_PROPERTY(QVariantList holders READ holders NOTIFY holdersChanged)
    Q_PROPERTY(bool anyHeld READ anyHeld NOTIFY holdersChanged)
    // Instruments whose panel is on screen (set by the shell). A paused instrument whose panel
    // is visible shows its banner only; otherwise a toast with Resume is shown too (app.md 5.2).
    Q_PROPERTY(QStringList visibleInstruments READ visibleInstruments WRITE setVisibleInstruments NOTIFY visibleInstrumentsChanged)

public:
    enum Resource {
        NoResource = 0x0,
        AnalogInput = 0x1,    // the single AI task (AI 0/1 or AudioIn L/R share the ADCs)
        AnalogOutput = 0x2,   // the single timed AO task (AO 0/1 or AudioOut L/R)
        Multimeter = 0x4,     // the isolated DMM
        DigitalLines = 0x8,   // some of DIO 0..7 (which ones: the controller's dioLines mask)
        Counter = 0x10,       // the counter/timer ctr0 (one input or pulse-output task)
        FrequencyOutput = 0x20,   // the FREQOUT square-wave generator (DIO 4)
    };
    Q_ENUM(Resource)
    Q_DECLARE_FLAGS(Resources, Resource)
    Q_FLAG(Resources)

    static ResourceArbiter* instance();
    static ResourceArbiter* create(QQmlEngine*, QJSEngine*);   // QML singleton factory

    // Short label ("AI", "AO", "DMM", "DIO") and phrase ("analog input", ...) of one resource.
    Q_INVOKABLE static QString label(Resource resource);
    Q_INVOKABLE static QString description(Resource resource);
    // "DIO 0–3", "DIO 0, 2, 5–7" for a line mask.
    Q_INVOKABLE static QString dioLinesLabel(int mask);

    // Grants every resource `controller` currently declares. Holders of a conflicting resource
    // are paused (InstrumentController::pause() with a reason naming `controller`), and a
    // notification with a Resume action is shown. Returns the paused controllers; their workers
    // may still be shutting down (the caller waits for them before touching the device).
    QList<InstrumentController*> acquire(InstrumentController* controller);
    // Releases everything `controller` holds (idempotent).
    void release(InstrumentController* controller);

    // Current holder of a (non-DIO) resource, or nullptr.
    InstrumentController* holder(Resource resource) const;
    // Controllers holding at least one line of `mask`.
    QList<InstrumentController*> dioHolders(quint8 mask) const;
    // Controllers that `controller` would pause if it started now.
    QList<InstrumentController*> conflicts(const InstrumentController* controller) const;
    Q_INVOKABLE bool isHeldBy(const QString& instrumentId) const;

    QVariantList holders() const;
    bool anyHeld() const;
    QStringList visibleInstruments() const { return m_visible; }
    void setVisibleInstruments(const QStringList& ids);

signals:
    void holdersChanged();
    void visibleInstrumentsChanged();
    // `paused` was paused because `by` started. `reason` is the user-facing text.
    void preempted(mybench::InstrumentController* paused, mybench::InstrumentController* by,
                   const QString& reason);

private:
    explicit ResourceArbiter(QObject* parent = nullptr);

    QPointer<InstrumentController> m_ai;
    QPointer<InstrumentController> m_ao;
    QPointer<InstrumentController> m_dmm;
    QPointer<InstrumentController> m_counter;
    QPointer<InstrumentController> m_freqOut;
    std::array<QPointer<InstrumentController>, 8> m_dio;   // per DIO line
    QStringList m_visible;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ResourceArbiter::Resources)

}  // namespace mybench
