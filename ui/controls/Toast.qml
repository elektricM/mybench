import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework

// One transient notification (see ToastArea), on the popup surface: the level as a colored icon,
// title, message, an optional action, close. Level: Notifications.Info/Success/Warning/Error.
Item {
    id: root

    property int toastId: 0
    property int level: Notifications.Info
    property string title
    property string message
    property string actionText
    property int repeat: 1

    readonly property color levelColor: level === Notifications.Success ? Theme.positive
                                      : level === Notifications.Warning ? Theme.caution
                                      : level === Notifications.Error ? Theme.negative : Theme.accent
    readonly property string levelIcon: level === Notifications.Success ? "success"
                                      : level === Notifications.Warning ? "warning"
                                      : level === Notifications.Error ? "error" : "info"

    signal dismissRequested()
    signal actionRequested()

    implicitWidth: 360
    implicitHeight: content.implicitHeight + 2 * Theme.spacingM
    Accessible.role: Accessible.AlertMessage
    Accessible.name: title

    HoverHandler {
        id: hover
        onHoveredChanged: Notifications.hold(root.toastId, hovered)
    }

    PopupFrame {
        anchors.fill: parent
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingXs
        anchors.topMargin: Theme.spacingM
        spacing: Theme.spacingS + 2

        Icon {
            Layout.alignment: Qt.AlignTop
            name: root.levelIcon
            color: root.levelColor
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: root.repeat > 1 ? qsTr("%1 (%2×)").arg(root.title).arg(root.repeat) : root.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                visible: root.message.length > 0
                Layout.fillWidth: true
                text: root.message
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }
            Button {
                visible: root.actionText.length > 0
                Layout.topMargin: Theme.spacingXs
                Layout.leftMargin: -Theme.spacingS
                flat: true
                implicitHeight: Theme.controlHeightS
                text: root.actionText
                onClicked: root.actionRequested()
            }
        }
        ToolButton {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: -Theme.spacingXs
            implicitWidth: Theme.controlHeightS
            implicitHeight: Theme.controlHeightS
            icon.source: Theme.icon("close")
            icon.width: Theme.iconSizeS
            icon.height: Theme.iconSizeS
            opacity: hover.hovered ? 1 : 0.6
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: qsTr("Dismiss")
            Accessible.name: qsTr("Dismiss")
            onClicked: root.dismissRequested()
        }
    }
}
