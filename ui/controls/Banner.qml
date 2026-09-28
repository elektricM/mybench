import QtQuick
import QtQuick.Layouts

// Inline message strip (pause, error, hints) with actions on the right.
//     Banner { tone: "warning"; text: ctl.pauseReason
//              ActionButton { text: qsTr("Resume"); compact: true; onClicked: ctl.resume() } }
Rectangle {
    id: root

    property string tone: "info"          // info, success, warning, danger
    property string text
    property string detail
    property bool closable: false
    default property alias actions: actionsRow.data
    readonly property color toneColor: tone === "success" ? Theme.success
                                     : tone === "warning" ? Theme.warning
                                     : tone === "danger" ? Theme.danger : Theme.accent
    readonly property string toneIcon: tone === "success" ? "success"
                                     : tone === "warning" ? "warning"
                                     : tone === "danger" ? "error" : "info"

    signal closed()

    implicitHeight: Math.max(38, layout.implicitHeight + 12)
    radius: Theme.radiusM
    color: Theme.alpha(toneColor, Theme.dark ? 0.12 : 0.09)
    border.width: 1
    border.color: Theme.alpha(toneColor, Theme.dark ? 0.35 : 0.3)

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS + 2

        Icon {
            name: root.toneIcon
            color: root.toneColor
        }
        Column {
            Layout.fillWidth: true
            spacing: 1
            Text {
                width: parent.width
                text: root.text
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                visible: root.detail.length > 0
                width: parent.width
                text: root.detail
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeM
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        Row {
            id: actionsRow
            spacing: Theme.spacingS
        }
        IconButton {
            visible: root.closable
            iconName: "close"
            iconSize: Theme.iconSizeS
            size: 24
            toolTip: qsTr("Dismiss")
            onClicked: root.closed()
        }
    }
}
