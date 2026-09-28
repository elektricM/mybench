pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Effects
import QtQuick.Templates as T
import MyBench.Controls

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    leftPadding: padding + (!control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    rightPadding: padding + (control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    padding: 4
    spacing: 4
    hoverEnabled: true

    delegate: ItemDelegate {
        required property var model
        required property int index

        width: ListView.view.width
        text: model[control.textRole]
        font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        checkable: true
        checked: control.currentIndex === index
    }

    indicator: Icon {
        x: control.mirrored ? control.padding + 4 : control.width - width - control.padding - 6
        y: control.topPadding + (control.availableHeight - height) / 2
        name: "chevron-down-line"
        size: 12
        color: control.enabled ? (control.hovered ? Theme.text : Theme.textMuted) : Theme.textDisabled
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    }

    contentItem: T.TextField {
        leftPadding: !control.mirrored ? 6 : 1
        rightPadding: control.mirrored ? 6 : 1
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.alpha(Theme.accent, 0.35)
        selectedTextColor: Theme.text
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusM
        visible: !control.flat || control.down || control.hovered
        color: control.editable ? Theme.surfaceSunken
             : control.down || control.popup.visible ? Theme.surfacePressed
             : control.hovered ? Theme.surfaceHover : Theme.surfaceRaised
        border.width: 1
        border.color: control.activeFocus && control.editable ? Theme.accent
                    : control.popup.visible ? Theme.borderStrong
                    : control.hovered ? Theme.borderStrong : Theme.border
        opacity: control.enabled ? 1 : 0.55
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusM + 3 }
    }

    popup: T.Popup {
        y: control.height + 4
        width: Math.max(control.width, 140)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 420)
        topMargin: 8
        bottomMargin: 8
        padding: 4
        font: control.font
        palette: control.palette

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
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: ScrollIndicator {}
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
                shadowBlur: 0.8
                shadowVerticalOffset: 6
                shadowOpacity: Theme.dark ? 0.9 : 0.45
                blurMax: 32
            }
        }
    }
}
