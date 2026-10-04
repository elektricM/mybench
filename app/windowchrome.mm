// WindowChrome on macOS: the standard window buttons (close, minimize, zoom) of the NSWindow behind
// a QWindow, and the title text visibility.
#include "windowchrome.h"

#include <QGuiApplication>
#include <QWindow>

#import <AppKit/AppKit.h>

namespace {

NSWindow* nativeWindow(QWindow* window)
{
    if (!window || QGuiApplication::platformName() != QLatin1String("cocoa") || !window->handle())
        return nil;
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    return view ? view.window : nil;
}

}  // namespace

QRectF WindowChrome::buttonsRect(QWindow* window) const
{
    NSWindow* ns = nativeWindow(window);
    if (!ns)
        return {};
    NSRect united = NSZeroRect;
    for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton* button = [ns standardWindowButton:kind];
        if (!button || button.hidden || !button.superview || button.superview.hidden)
            continue;
        const NSRect frame = [button convertRect:button.bounds toView:nil];   // window coordinates
        united = NSIsEmptyRect(united) ? frame : NSUnionRect(united, frame);
    }
    if (NSIsEmptyRect(united))
        return {};
    // Window coordinates have their origin at the bottom left; Qt's at the top left.
    const CGFloat height = ns.frame.size.height;
    return QRectF(united.origin.x, height - NSMaxY(united), united.size.width, united.size.height);
}

void WindowChrome::setTitleHidden(QWindow* window, bool hidden) const
{
    if (NSWindow* ns = nativeWindow(window))
        ns.titleVisibility = hidden ? NSWindowTitleHidden : NSWindowTitleVisible;
}
