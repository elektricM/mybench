import QtQuick

// Native wheel and touchpad scrolling for a Flickable/ListView (docs/UI.md scrolling):
//     Flickable { id: flick; ...; WheelScroll { flickable: flick } }
// - touchpad / high-resolution devices: `pixelDelta` moves the content pixel for pixel, with the
//   platform's own momentum events, and no Qt Quick flick animation on top;
// - notched mouse wheels: a fixed step (wheelScrollLines x 20 px, the Qt Widgets/Breeze step) per
//   notch, applied immediately, with no kinetic deceleration of our own;
// - Shift + wheel scrolls horizontally when the content is wider than the view;
// - the content stops at its bounds (no rubber banding) and the event is consumed only when
//   the content can move in that direction, so a nested view hands the rest to its parent.
WheelHandler {
    id: handler

    required property Flickable flickable
    // Pixels per wheel notch; the platform's "lines per notch" setting times a line of 20 px.
    property real notchStep: (Application.styleHints.wheelScrollLines ?? 3) * 20

    parent: flickable
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    onWheel: event => {
        const horizontal = (event.modifiers & Qt.ShiftModifier) !== 0
        const f = handler.flickable
        const usePixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0
        let dx = usePixels ? event.pixelDelta.x : event.angleDelta.x / 120 * handler.notchStep
        let dy = usePixels ? event.pixelDelta.y : event.angleDelta.y / 120 * handler.notchStep
        if (horizontal && dx === 0) {
            dx = dy
            dy = 0
        }
        const maxY = f.originY + f.contentHeight - f.height + f.bottomMargin
        const maxX = f.originX + f.contentWidth - f.width + f.rightMargin
        const minY = f.originY - f.topMargin
        const minX = f.originX - f.leftMargin
        const newY = Math.max(minY, Math.min(Math.max(minY, maxY), f.contentY - dy))
        const newX = Math.max(minX, Math.min(Math.max(minX, maxX), f.contentX - dx))
        const moved = newY !== f.contentY || newX !== f.contentX
        event.accepted = moved || f.contentHeight > f.height
        if (!moved)
            return
        f.cancelFlick()
        f.contentY = newY
        f.contentX = newX
    }
}
