#include "wheelguard.h"

#include <QEvent>
#include <QGuiApplication>
#include <QWheelEvent>

namespace mybench {

WheelGuard::WheelGuard(QObject* parent) : QObject(parent)
{
    if (auto* app = QGuiApplication::instance())
        app->installEventFilter(this);
}

void WheelGuard::setGestureGap(int ms)
{
    if (ms == m_gap)
        return;
    m_gap = ms;
    emit gestureGapChanged();
}

void WheelGuard::setFresh(bool fresh)
{
    if (fresh == m_fresh)
        return;
    m_fresh = fresh;
    emit freshGestureChanged();
}

bool WheelGuard::eventFilter(QObject* watched, QEvent* event)
{
    // Classify each wheel event once, when it reaches its window: Qt Quick then re-sends it to the
    // items under the pointer, which must not count as further events of the gesture.
    if (event->type() == QEvent::Wheel && watched->isWindowType()) {
        const auto* wheel = static_cast<QWheelEvent*>(event);
        const bool paused = !m_last.isValid() || m_last.elapsed() > m_gap;
        // A touchpad gesture begins with ScrollBegin; a phase-less (mouse) event after a pause
        // begins a new gesture; updates, momentum and the end continue the current one.
        const Qt::ScrollPhase phase = wheel->phase();
        const bool starts = phase == Qt::ScrollBegin || (phase == Qt::NoScrollPhase && paused);
        setFresh(starts);
        if (starts)
            emit gestureStarted();
        m_last.start();
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace mybench
