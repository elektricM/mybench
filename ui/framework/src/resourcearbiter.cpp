#include "mybench/resourcearbiter.h"

#include "mybench/instrumentcontroller.h"
#include "mybench/notifications.h"

#include <QJSEngine>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace mybench {

ResourceArbiter::ResourceArbiter(QObject* parent) : QObject(parent) {}

ResourceArbiter* ResourceArbiter::instance()
{
    static ResourceArbiter* const arbiter = new ResourceArbiter;
    return arbiter;
}

ResourceArbiter* ResourceArbiter::create(QQmlEngine*, QJSEngine*)
{
    ResourceArbiter* arbiter = instance();
    QJSEngine::setObjectOwnership(arbiter, QJSEngine::CppOwnership);
    return arbiter;
}

QString ResourceArbiter::label(Resource resource)
{
    switch (resource) {
    case AnalogInput:
        return u"AI"_s;
    case AnalogOutput:
        return u"AO"_s;
    case Multimeter:
        return u"DMM"_s;
    case DigitalLines:
        return u"DIO"_s;
    case Counter:
        return u"CTR"_s;
    case FrequencyOutput:
        return u"FREQOUT"_s;
    case NoResource:
        break;
    }
    return {};
}

QString ResourceArbiter::description(Resource resource)
{
    switch (resource) {
    case AnalogInput:
        return tr("analog input");
    case AnalogOutput:
        return tr("analog output");
    case Multimeter:
        return tr("multimeter");
    case DigitalLines:
        return tr("digital lines");
    case Counter:
        return tr("counter");
    case FrequencyOutput:
        return tr("frequency output");
    case NoResource:
        break;
    }
    return {};
}

QString ResourceArbiter::dioLinesLabel(int mask)
{
    QStringList parts;
    int line = 0;
    while (line < 8) {
        if (!(mask & (1 << line))) {
            ++line;
            continue;
        }
        int last = line;
        while (last + 1 < 8 && (mask & (1 << (last + 1))))
            ++last;
        if (last == line)
            parts << QString::number(line);
        else if (last == line + 1)
            parts << QString::number(line) << QString::number(last);
        else
            parts << u"%1–%2"_s.arg(line).arg(last);
        line = last + 1;
    }
    return parts.isEmpty() ? u"DIO"_s : u"DIO "_s + parts.join(u", "_s);
}

QList<InstrumentController*> ResourceArbiter::conflicts(const InstrumentController* controller) const
{
    QList<InstrumentController*> result;
    auto add = [&](InstrumentController* holder) {
        if (holder && holder != controller && !result.contains(holder))
            result.append(holder);
    };
    const ResourceArbiter::Resources needs = controller->resources();
    if (needs.testFlag(AnalogInput))
        add(m_ai);
    if (needs.testFlag(AnalogOutput))
        add(m_ao);
    if (needs.testFlag(Multimeter))
        add(m_dmm);
    if (needs.testFlag(Counter))
        add(m_counter);
    if (needs.testFlag(FrequencyOutput))
        add(m_freqOut);
    if (needs.testFlag(DigitalLines)) {
        for (int line = 0; line < 8; ++line) {
            if (controller->dioLines() & (1 << line))
                add(m_dio[size_t(line)]);
        }
    }
    return result;
}

QList<InstrumentController*> ResourceArbiter::acquire(InstrumentController* controller)
{
    QList<InstrumentController*> paused;
    bool changed = false;

    auto claim = [&](QPointer<InstrumentController>& slot, const QString& what) {
        InstrumentController* holder = slot;
        if (holder && holder != controller && !paused.contains(holder)) {
            const QString reason = tr("%1 paused — %2 in use by %3").arg(holder->title(), what, controller->title());
            holder->pause(reason);
            paused.append(holder);
            // The banner in the holder's panel says it already when that panel is on screen.
            if (!m_visible.contains(holder->instrumentId())) {
                QPointer<InstrumentController> weak(holder);
                Notifications::instance()->show(Notifications::Warning, reason, tr("Press Resume to take it back."),
                                                tr("Resume"), [weak] { if (weak) weak->resume(); },
                                                u"pause:"_s + holder->instrumentId());
            }
            emit preempted(holder, controller, reason);
        }
        if (slot != controller) {
            slot = controller;
            changed = true;
        }
    };

    const Resources needs = controller->resources();
    if (needs.testFlag(AnalogInput))
        claim(m_ai, label(AnalogInput));
    if (needs.testFlag(AnalogOutput))
        claim(m_ao, label(AnalogOutput));
    if (needs.testFlag(Multimeter))
        claim(m_dmm, label(Multimeter));
    if (needs.testFlag(Counter))
        claim(m_counter, label(Counter));
    if (needs.testFlag(FrequencyOutput))
        claim(m_freqOut, label(FrequencyOutput));
    if (needs.testFlag(DigitalLines)) {
        for (int line = 0; line < 8; ++line) {
            if (controller->dioLines() & (1 << line))
                claim(m_dio[size_t(line)], u"DIO %1"_s.arg(line));
        }
    }
    if (changed)
        emit holdersChanged();
    return paused;
}

void ResourceArbiter::release(InstrumentController* controller)
{
    bool changed = false;
    auto drop = [&](QPointer<InstrumentController>& slot) {
        if (slot == controller) {
            slot.clear();
            changed = true;
        }
    };
    drop(m_ai);
    drop(m_ao);
    drop(m_dmm);
    drop(m_counter);
    drop(m_freqOut);
    for (auto& line : m_dio)
        drop(line);
    if (changed)
        emit holdersChanged();
}

void ResourceArbiter::setVisibleInstruments(const QStringList& ids)
{
    if (ids == m_visible)
        return;
    m_visible = ids;
    emit visibleInstrumentsChanged();
}

InstrumentController* ResourceArbiter::holder(Resource resource) const
{
    switch (resource) {
    case AnalogInput:
        return m_ai;
    case AnalogOutput:
        return m_ao;
    case Multimeter:
        return m_dmm;
    case Counter:
        return m_counter;
    case FrequencyOutput:
        return m_freqOut;
    case DigitalLines:
    case NoResource:
        break;
    }
    return nullptr;
}

QList<InstrumentController*> ResourceArbiter::dioHolders(quint8 mask) const
{
    QList<InstrumentController*> result;
    for (int line = 0; line < 8; ++line) {
        InstrumentController* holder = m_dio[size_t(line)];
        if ((mask & (1 << line)) && holder && !result.contains(holder))
            result.append(holder);
    }
    return result;
}

bool ResourceArbiter::isHeldBy(const QString& instrumentId) const
{
    auto is = [&](const QPointer<InstrumentController>& slot) {
        return slot && slot->instrumentId() == instrumentId;
    };
    if (is(m_ai) || is(m_ao) || is(m_dmm) || is(m_counter) || is(m_freqOut))
        return true;
    for (const auto& line : m_dio) {
        if (is(line))
            return true;
    }
    return false;
}

bool ResourceArbiter::anyHeld() const
{
    if (m_ai || m_ao || m_dmm || m_counter || m_freqOut)
        return true;
    for (const auto& line : m_dio) {
        if (line)
            return true;
    }
    return false;
}

QVariantList ResourceArbiter::holders() const
{
    QVariantList list;
    auto entry = [](Resource resource, InstrumentController* holder, const QString& lines) {
        QVariantMap map;
        map.insert(u"resource"_s, label(resource));
        map.insert(u"name"_s, description(resource));
        map.insert(u"lines"_s, lines);
        map.insert(u"instrumentId"_s, holder->instrumentId());
        map.insert(u"title"_s, holder->title());
        map.insert(u"shortTitle"_s, holder->shortTitle());
        return map;
    };
    if (m_ai)
        list.append(entry(AnalogInput, m_ai, {}));
    if (m_ao)
        list.append(entry(AnalogOutput, m_ao, {}));
    if (m_dmm)
        list.append(entry(Multimeter, m_dmm, {}));
    if (m_counter)
        list.append(entry(Counter, m_counter, {}));
    if (m_freqOut)
        list.append(entry(FrequencyOutput, m_freqOut, {}));
    QList<InstrumentController*> dio;
    for (const auto& line : m_dio) {
        if (line && !dio.contains(line.data()))
            dio.append(line.data());
    }
    for (InstrumentController* holder : std::as_const(dio)) {
        int mask = 0;
        for (int line = 0; line < 8; ++line) {
            if (m_dio[size_t(line)] == holder)
                mask |= 1 << line;
        }
        list.append(entry(DigitalLines, holder, dioLinesLabel(mask)));
    }
    return list;
}

}  // namespace mybench
