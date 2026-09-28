import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// Vertical level meter in dBFS (-60 ... 0): RMS bar, peak line, held peak, colored zones
// (green, amber from -12 dBFS, red from -3 dBFS).
//     AeqMeter { label: "In L"; peak: m.peak; rms: m.rms; hold: m.hold }
Item {
    id: root

    property string label
    property real peak: -120
    property real rms: -120
    property real hold: -120
    property bool active: true
    property real floorDb: -60

    function position(db: real): real {
        return Math.max(0, Math.min(1, (db - root.floorDb) / -root.floorDb))
    }
    function zoneColor(db: real): color {
        return db > -3 ? Theme.danger : db > -12 ? Theme.warning : Theme.success
    }

    implicitWidth: 30   // room for the peak number (-59, -120) beside its neighbour
    implicitHeight: 180

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.active && root.peak > -120 ? Math.round(root.peak) : "—"
            color: root.active ? (root.peak > -3 ? Theme.danger : Theme.textSecondary) : Theme.textMuted
            font.pixelSize: Theme.fontSizeXs
            font.features: Theme.tabularFigures
        }
        Rectangle {
            id: bar
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            implicitWidth: 8
            radius: 2
            color: Theme.dark ? "#16191F" : "#E3E6EB"
            opacity: root.active ? 1 : 0.4

            // RMS fill with the zone colors.
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                radius: 2
                height: parent.height * root.position(root.rms)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: root.zoneColor(root.rms) }
                    GradientStop { position: 1.0; color: Theme.alpha(root.zoneColor(root.rms), 0.55) }
                }
            }
            // Peak.
            Rectangle {
                width: parent.width
                height: 2
                y: parent.height * (1 - root.position(root.peak)) - 1
                visible: root.peak > root.floorDb
                color: root.zoneColor(root.peak)
                opacity: 0.8
            }
            // Held peak.
            Rectangle {
                width: parent.width + 4
                x: -2
                height: 2
                y: parent.height * (1 - root.position(root.hold)) - 1
                visible: root.hold > root.floorDb
                color: root.zoneColor(root.hold)
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.DemiBold
        }
    }
}
