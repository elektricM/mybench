import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import MyBench.Controls

T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: 20
    topPadding: 8

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
    }

    background: Rectangle {
        radius: Theme.radiusXl
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.dark ? "#2C303A" : Theme.border
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 1.0
            shadowVerticalOffset: 12
            shadowOpacity: Theme.dark ? 0.95 : 0.5
            blurMax: 48
        }
    }

    header: Text {
        text: control.title
        visible: control.title.length > 0
        elide: Text.ElideRight
        color: Theme.text
        font.pixelSize: Theme.fontSizeXl
        font.weight: Font.DemiBold
        leftPadding: 20
        rightPadding: 20
        topPadding: 18
        bottomPadding: 4
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
