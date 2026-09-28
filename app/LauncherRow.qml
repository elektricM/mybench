import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Controls

// One clickable row of a Home launcher card (sessions, tools, files, resources): icon, title,
// optional subtitle and trailing text; extra buttons (children) appear on hover.
//     LauncherRow { iconName: "file"; title: "data.csv"; subtitle: "~/Desktop"; onClicked: ... }
T.AbstractButton {
    id: row

    property string iconName
    property color iconColor: Theme.textSecondary
    property string title
    property string subtitle
    property string trailing
    property string toolTip
    property bool available: true        // false: dimmed (missing file)
    property bool monoSubtitle: false
    property int subtitleElide: Text.ElideRight   // Text.ElideMiddle for paths
    default property alias tools: toolRow.data
    readonly property bool showTools: hovered || visualFocus || toolRow.childHovered

    Layout.fillWidth: true
    implicitHeight: Math.max(subtitle.length > 0 ? 38 : 30, content.implicitHeight + topPadding + bottomPadding)
    leftPadding: Theme.spacingS
    rightPadding: Theme.spacingXs
    topPadding: 3
    bottomPadding: 3
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: title

    background: Rectangle {
        radius: Theme.radiusM
        color: row.down ? Theme.surfacePressed : row.hovered ? Theme.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: row.visualFocus; radius: Theme.radiusM + 3 }
    }

    contentItem: RowLayout {
        id: content
        spacing: Theme.spacingS
        Icon {
            visible: row.iconName.length > 0
            Layout.alignment: Qt.AlignVCenter
            name: row.iconName
            color: row.available ? row.iconColor : Theme.textDisabled
            size: Theme.iconSize
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: row.title
                color: row.available ? Theme.text : Theme.textMuted
                font.pixelSize: Theme.fontSizeBody
                font.strikeout: !row.available
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: row.subtitle.length > 0
                text: row.subtitle
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
                font.family: row.monoSubtitle ? Theme.monoFamily : Theme.fontFamily
                elide: row.subtitleElide
            }
        }
        Text {
            visible: row.trailing.length > 0 && !row.showTools
            Layout.alignment: Qt.AlignVCenter
            text: row.trailing
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
        }
        Row {
            id: toolRow
            readonly property bool childHovered: {
                for (let i = 0; i < children.length; ++i)
                    if ((children[i] as T.AbstractButton)?.hovered)
                        return true
                return false
            }
            visible: row.showTools && children.length > 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
        }
    }

    ToolTip.visible: hovered && toolTip.length > 0 && !down
    ToolTip.delay: 700
    ToolTip.text: toolTip
}
