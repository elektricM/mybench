import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import MyBench.Framework

// One notification card (see ToastArea). Level: Notifications.Info/Success/Warning/Error.
Item {
    id: root

    property int toastId: 0
    property int level: Notifications.Info
    property string title
    property string message
    property string actionText
    property int repeat: 1

    readonly property color levelColor: level === Notifications.Success ? Theme.success
                                      : level === Notifications.Warning ? Theme.warning
                                      : level === Notifications.Error ? Theme.danger : Theme.accent
    readonly property string levelIcon: level === Notifications.Success ? "success"
                                      : level === Notifications.Warning ? "warning"
                                      : level === Notifications.Error ? "error" : "info"

    signal dismissRequested()
    signal actionRequested()

    implicitWidth: 360
    implicitHeight: content.implicitHeight + 24

    HoverHandler {
        id: hover
        onHoveredChanged: Notifications.hold(root.toastId, hovered)
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.dark ? "#2C303A" : Theme.border
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.9
            shadowVerticalOffset: 6
            shadowOpacity: Theme.dark ? 0.9 : 0.5
            blurMax: 32
        }
    }

    // level accent on the left edge
    Rectangle {
        x: 0
        y: 10
        width: 3
        height: parent.height - 20
        radius: 1.5
        color: root.levelColor
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        anchors.topMargin: 12
        spacing: Theme.spacingM

        Icon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: root.levelIcon
            color: root.levelColor
            size: 18
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Badge {
                    visible: root.repeat > 1
                    text: "×" + root.repeat
                    caps: false
                    color: Theme.textSecondary
                }
            }
            Text {
                visible: root.message.length > 0
                Layout.fillWidth: true
                text: root.message
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeM
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
                lineHeight: 1.1
            }
            ActionButton {
                visible: root.actionText.length > 0
                Layout.topMargin: 6
                text: root.actionText
                tone: "subtle"
                color: root.levelColor === Theme.danger ? Theme.accent : root.levelColor
                compact: true
                onClicked: root.actionRequested()
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: -4
            iconName: "close"
            iconSize: Theme.iconSizeS
            size: 24
            opacity: hover.hovered ? 1 : 0.6
            toolTip: qsTr("Dismiss")
            onClicked: root.dismissRequested()
        }
    }
}
