pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Live value and statistics of one logged channel (Data Logger display, top row).
//     LogChannelCard { name: "AI 0"; color: Theme.ch0; unit: "V"; stats: entry }
Rectangle {
    id: root

    property string name
    property string unit
    property color accent: Theme.ch0
    property var stats: null          // { last, minimum, maximum, mean, deviation, count, overload }
    property bool stale: false
    property bool compact: false

    readonly property bool hasData: stats !== null && stats !== undefined && stats.count > 0

    function fmt(v: real, digits: int): string {
        return isNaN(v) ? "—" : Units.format(v, root.unit, digits)
    }

    implicitHeight: layout.implicitHeight + 2 * Theme.spacingS
    implicitWidth: 220
    radius: Theme.radiusM
    color: Theme.alpha(accent, Theme.dark ? 0.07 : 0.05)
    border.color: Theme.alpha(accent, 0.35)

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 1
        width: 3
        radius: 1.5
        color: root.accent
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        anchors.topMargin: Theme.spacingS
        anchors.bottomMargin: Theme.spacingS
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Text {
                text: root.name
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.weight: Font.DemiBold
            }
            Badge {
                visible: root.hasData && root.stats.overload
                text: qsTr("Over range")
                color: Theme.danger
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.hasData ? qsTr("%1 pts").arg(Units.format(root.stats.count, "", 3)) : ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                font.features: Theme.tabularFigures
            }
        }
        Text {
            text: root.hasData ? root.fmt(root.stats.last, 5) : "— " + root.unit
            color: root.accent
            opacity: root.stale ? 0.6 : 1
            font.pixelSize: root.compact ? Theme.fontSizeTitle : Theme.fontSizeHeadline
            font.weight: Font.Light
            font.features: Theme.tabularFigures
        }
        GridLayout {
            visible: !root.compact
            Layout.fillWidth: true
            columns: 4
            columnSpacing: Theme.spacingS
            rowSpacing: 0
            Repeater {
                model: [qsTr("MIN"), qsTr("MAX"), qsTr("MEAN"), qsTr("STD DEV")]
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: modelData
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.DemiBold
                }
            }
            Repeater {
                model: root.hasData ? [root.stats.minimum, root.stats.maximum, root.stats.mean, root.stats.deviation]
                                    : [NaN, NaN, NaN, NaN]
                delegate: Text {
                    required property real modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: root.fmt(modelData, 4)
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeS
                    font.features: Theme.tabularFigures
                    elide: Text.ElideRight
                }
            }
        }
    }
}
