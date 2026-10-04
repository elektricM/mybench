import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// In-pane message (docs/UI.md §10): paused, reconnected, over range, error, pre-run conflict.
// A neutral strip with the state as a colored icon only (no tinted wash), the message, optional
// detail, actions (flat Buttons) and a close button.
//     Banner { tone: "warning"; text: ctl.pauseReason
//              Button { flat: true; text: qsTr("Resume"); onClicked: ctl.resume() } }
Rectangle {
    id: root

    property string tone: "info"          // info, success, warning, danger
    property string text
    property string detail
    property bool closable: false
    default property alias actions: actionsRow.data
    readonly property color toneColor: tone === "success" ? Theme.positive
                                     : tone === "warning" ? Theme.caution
                                     : tone === "danger" ? Theme.negative : Theme.accent
    readonly property string toneIcon: tone === "success" ? "success"
                                     : tone === "warning" ? "warning"
                                     : tone === "danger" ? "error" : "info"

    signal closed()

    implicitWidth: layout.implicitWidth + Theme.spacingM + Theme.spacingXs
    implicitHeight: Math.max(Theme.controlHeight + Theme.spacingS, layout.implicitHeight + Theme.spacingS)
    radius: Theme.radiusControl
    color: Theme.base
    border.width: Theme.borderWidth
    border.color: Theme.separator
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingXs
        spacing: Theme.spacingS

        Icon {
            Layout.alignment: Qt.AlignVCenter
            name: root.toneIcon
            color: root.toneColor
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingXs
            Layout.bottomMargin: Theme.spacingXs
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: root.text
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                visible: root.detail.length > 0
                Layout.fillWidth: true
                text: root.detail
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        Row {
            id: actionsRow
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingXs
        }
        ToolButton {
            visible: root.closable
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Theme.controlHeightS
            implicitHeight: Theme.controlHeightS
            icon.source: Theme.icon("close")
            icon.width: Theme.iconSizeS
            icon.height: Theme.iconSizeS
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: qsTr("Dismiss")
            Accessible.name: qsTr("Dismiss")
            onClicked: root.closed()
        }
    }
}
