import QtQuick
import QtQuick.Controls.impl
import QtQuick.Effects
import QtQuick.Templates as T
import MyBench.Controls

T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 0
    padding: 4
    overlap: 4
    font.pixelSize: Theme.fontSizeBody

    delegate: MenuItem {}

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        T.ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 32
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.dark ? "#2C303A" : Theme.border
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.8
            shadowVerticalOffset: 6
            shadowOpacity: Theme.dark ? 0.9 : 0.45
            blurMax: 32
        }
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
