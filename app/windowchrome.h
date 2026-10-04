// WindowChrome: what the shell needs to know about the native window frame (docs/UI.md §7, the
// unified title bar on macOS). With Qt.ExpandedClientAreaHint the toolbar sits in the title bar
// area: Qt reports that area as SafeArea margins (top), but not where the window buttons are, and
// AppKit would still draw the title text over the toolbar. QML:
//     WindowChrome.buttonsRect(window)        // the traffic lights, window coordinates; empty
//                                             // where there are none (other platforms, offscreen)
//     WindowChrome.setTitleHidden(window, true)   // keep the title (Mission Control, Window menu)
//                                                 // but do not draw it
#pragma once

#include <QObject>
#include <QRectF>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class WindowChrome : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit WindowChrome(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QRectF buttonsRect(QWindow* window) const;
    Q_INVOKABLE void setTitleHidden(QWindow* window, bool hidden) const;
};
