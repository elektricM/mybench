import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import MyBench.Controls

T.Popup {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 12

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
    }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.dark ? "#2C303A" : Theme.border
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.9
            shadowVerticalOffset: 8
            shadowOpacity: Theme.dark ? 0.9 : 0.45
            blurMax: 40
        }
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
