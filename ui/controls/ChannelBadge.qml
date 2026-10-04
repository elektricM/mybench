import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// A channel's identity: its signal-color swatch and name (docs/UI.md §13.8). Checkable badges show
// or hide the channel; an unchecked badge has an empty swatch and tertiary text.
//     ChannelBadge { color: Theme.signalCh0; text: "CH0" }
//     ChannelBadge { color: Theme.signalCh1; text: "CH1"; checkable: true; checked: ctl.ch1Visible
//                    onToggled: on => ctl.ch1Visible = on }
T.Control {
    id: control

    property string text
    property bool checkable: false
    property bool checked: true
    property color color: Theme.signalCh0
    property string detail            // secondary text after the name, e.g. "1 V/div"
    property string toolTip

    signal toggled(bool checked)

    function toggle() {
        if (!checkable || !enabled)
            return
        checked = !checked
        toggled(checked)
    }

    hoverEnabled: checkable
    focusPolicy: checkable ? Qt.TabFocus : Qt.NoFocus
    implicitHeight: Theme.controlHeightS
    implicitWidth: row.implicitWidth + (checkable ? 2 * Theme.spacingS : 0)
    font.pixelSize: Theme.fontSizeSmall
    Accessible.role: checkable ? Accessible.CheckBox : Accessible.StaticText
    Accessible.name: text
    Accessible.checked: checked
    Keys.onSpacePressed: toggle()
    TapHandler {
        id: tap
        enabled: control.checkable
        onTapped: control.toggle()
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: !control.checkable ? "transparent"
             : tap.pressed ? Theme.pressedFill : control.hovered ? Theme.hoverFill : "transparent"
        FocusRing { shown: control.visualFocus }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.spacingXs + 2
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 2
                color: control.checked ? control.color : "transparent"
                border.width: control.checked ? 0 : 1.5
                border.color: control.color
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.checked ? Theme.text : Theme.textTertiary
            }
            Text {
                visible: control.detail.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: control.detail
                font.pixelSize: control.font.pixelSize
                font.features: Theme.tabularFigures
                color: Theme.textSecondary
            }
        }
    }

    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: toolTip
}
