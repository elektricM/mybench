import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.ScrollView {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    // Native wheel/touchpad scrolling and no rubber band on the Flickable inside (WheelScroll).
    WheelScroll {
        flickable: control.contentItem as Flickable
        enabled: flickable !== null
    }

    Component.onCompleted: {
        const flick = contentItem as Flickable
        if (flick)
            flick.boundsBehavior = Flickable.StopAtBounds
    }

    ScrollBar.vertical: ScrollBar {
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
        active: control.ScrollBar.horizontal.active
    }

    ScrollBar.horizontal: ScrollBar {
        parent: control
        x: control.leftPadding
        y: control.height - height
        width: control.availableWidth
        active: control.ScrollBar.vertical.active
    }
}
