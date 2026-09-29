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
    if (event->type() == QEvent::Wheel) {
        const auto* wheel = static_cast<QWheelEvent*>(event);
        const bool paused = !m_last.isValid() || m_last.elapsed() > m_gap;
        switch (wheel->phase()) {
        case Qt::ScrollBegin:
            setFresh(true);
            break;
        case Qt::ScrollUpdate:
        case Qt::ScrollMomentum:
        case Qt::ScrollEnd:
            setFresh(false);
            break;
        case Qt::NoScrollPhase:
            setFresh(paused);
            break;
        }
        m_last.start();
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace mybench
