// WheelGuard: native wheel etiquette for controls inside scrollable views (QML singleton).
//
// KDE/Breeze and Qt Widgets change a combo box or spin box on the wheel only when it has the
// keyboard focus, or when the wheel gesture *starts* over it; a scroll that merely passes over the
// control keeps scrolling the view. Controls ask `WheelGuard.allows(hasFocus)` from their wheel
// handler (or binds `WheelGuard.freshGesture` into the handler's `enabled`): true when focused or when the current wheel event begins a new gesture (touchpad
// ScrollBegin, or a mouse wheel after a pause). An application-wide event filter classifies every
// wheel event before it is delivered.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace mybench {

class WheelGuard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Pause after which a phase-less wheel event starts a new gesture (ms).
    Q_PROPERTY(int gestureGap READ gestureGap WRITE setGestureGap NOTIFY gestureGapChanged)
    // Bind a WheelHandler's `enabled` to it: it flips before the wheel event is delivered, and a
    // disabled handler lets the event through to the scrolling view underneath.
    Q_PROPERTY(bool freshGesture READ startsGesture NOTIFY freshGestureChanged)

public:
    explicit WheelGuard(QObject* parent = nullptr);

    // True when the wheel event being delivered starts a gesture.
    Q_INVOKABLE bool startsGesture() const { return m_fresh; }
    // Focused controls always take the wheel; others only at the start of a gesture.
    Q_INVOKABLE bool allows(bool hasFocus) const { return hasFocus || m_fresh; }

    int gestureGap() const { return m_gap; }
    void setGestureGap(int ms);

signals:
    void gestureGapChanged();
    void freshGestureChanged();
    // Emitted for every wheel event that starts a gesture (even when freshGesture was already
    // true): controls that keep the rest of a gesture they started reset their ownership here.
    void gestureStarted();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QElapsedTimer m_last;
    void setFresh(bool fresh);
    bool m_fresh = true;
    int m_gap = 350;
};

}  // namespace mybench
