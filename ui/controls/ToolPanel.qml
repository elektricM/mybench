pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// Floating panel opened from a ToolPalette (Measure, Cursors, Math...): title and × in the
// header, the items declared inside in a column (12 px gaps) that scrolls when taller than the
// panel. Esc or × emit closeRequested(); the owner hides it. Place it over the display (it is a
// plain Item: anchor or position it, set `height` or let it size to its content).
//     ToolPanel { title: qsTr("Measurements"); visible: pane.tool === "measure"
//                 onCloseRequested: pane.tool = ""; ... }
// It takes the keyboard focus when it becomes visible (Esc closes it).
FocusScope {
    id: panel

    property string title
    property real maximumHeight: Infinity
    property alias headerExtras: headerExtrasRow.data
    default property alias content: body.data

    signal closeRequested()

    width: 280
    implicitWidth: 280
    implicitHeight: Math.min(maximumHeight, header.height + 1 + body.implicitHeight + 2 * Theme.spacingM + 2)
    Accessible.role: Accessible.Dialog
    Accessible.name: title

    // Takes keyboard focus when it is shown (not when created visible), so Esc closes it.
    onVisibleChanged: {
        if (visible)
            forceActiveFocus()
    }

    Keys.onEscapePressed: event => {
        panel.closeRequested()
        event.accepted = true
    }

    // One small shadow (Theme shadow tokens).
    Rectangle {
        id: shadowSource
        anchors.fill: frame
        radius: frame.radius
        color: Theme.shadowColor
        visible: false
    }
    MultiEffect {
        source: shadowSource
        anchors.fill: shadowSource
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowBlur: 0.5
        shadowVerticalOffset: Theme.shadowOffsetY
        blurMax: Theme.shadowBlur
        autoPaddingEnabled: true
        opacity: Theme.dark ? 1 : 0.9
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.cardFill
        border.width: 1
        border.color: Theme.controlBorder

        // Swallow clicks so they never reach the display below.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: mouse => {
                panel.forceActiveFocus()
                mouse.accepted = true
            }
            onWheel: wheel => wheel.accepted = true
        }

        Item {
            id: header
            x: 1
            y: 1
            width: parent.width - 2
            height: Theme.controlHeight + 10
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingXs + 2
                spacing: Theme.spacingS
                Text {
                    Layout.fillWidth: true
                    text: panel.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                RowLayout {
                    id: headerExtrasRow
                    spacing: Theme.spacingXs
                    visible: children.length > 0
                }
                IconButton {
                    objectName: "closeButton"
                    iconName: "close"
                    size: Theme.controlHeightS
                    toolTip: qsTr("Close (Esc)")
                    Accessible.name: qsTr("Close %1").arg(panel.title)
                    onClicked: panel.closeRequested()
                }
            }
        }
        Rectangle {
            x: 1
            y: header.y + header.height
            width: parent.width - 2
            height: 1
            color: Theme.separator
        }

        Flickable {
            id: flick
            x: 1
            y: header.y + header.height + 1
            width: parent.width - 2
            height: parent.height - y - 1
            contentWidth: width
            contentHeight: body.implicitHeight + 2 * Theme.spacingM
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: ScrollBar { policy: flick.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            ColumnLayout {
                id: body
                x: Theme.spacingM
                y: Theme.spacingM
                width: flick.width - 2 * Theme.spacingM
                spacing: Theme.spacingM
            }
        }
    }
}
