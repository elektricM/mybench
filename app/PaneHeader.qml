import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Slim header of a panel shown side by side: icon, title, run state, focus marker, actions.
Rectangle {
    id: header

    property string instrumentId
    property bool focused: false
    readonly property var info: InstrumentRegistry.info(instrumentId)
    readonly property InstrumentController controller: InstrumentRegistry.controller(instrumentId)

    signal closeRequested()
    signal popOutRequested()

    implicitHeight: 34
    color: Theme.surface

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 2
        color: header.focused ? Theme.accent : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
    Divider {
        anchors.bottom: parent.bottom
        width: parent.width
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS
        Icon {
            name: header.info.icon ?? ""
            size: Theme.iconSizeS
            color: header.focused ? Theme.text : Theme.textSecondary
        }
        Text {
            text: header.info.title ?? ""
            color: header.focused ? Theme.text : Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.DemiBold
        }
        StatusDot {
            visible: header.controller !== null && (header.controller.running || header.controller.paused)
            color: header.controller && header.controller.paused ? Theme.warning : Theme.success
            pulse: header.controller !== null && header.controller.state === InstrumentController.Running
            size: 7
        }
        Item { Layout.fillWidth: true }
        IconButton {
            size: 24
            iconSize: Theme.iconSizeS
            iconName: "external"
            toolTip: qsTr("Move to its own tab")
            onClicked: header.popOutRequested()
        }
        IconButton {
            size: 24
            iconSize: Theme.iconSizeS
            iconName: "close"
            toolTip: qsTr("Close panel")
            onClicked: header.closeRequested()
        }
    }
}
