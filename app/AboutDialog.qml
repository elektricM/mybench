import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// About (app.md 17).
Dialog {
    id: dialog

    anchors.centerIn: Overlay.overlay
    width: 460
    modal: true
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: Theme.spacingM
        RowLayout {
            spacing: Theme.spacingM
            Image {
                source: Theme.icon("app-icon-color")
                sourceSize: Qt.size(56, 56)
            }
            ColumnLayout {
                spacing: 2
                Text {
                    text: AppInfo.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeHeadline
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                }
                Text {
                    text: qsTr("Version %1").arg(AppInfo.version)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeBody
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Open-source instruments for the NI myDAQ: multimeter, oscilloscope, waveform generator, Bode and spectrum analyzers, digital I/O and counter/timer, in one window.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("%1 is an independent project, not affiliated with or endorsed by NI. NI, myDAQ, ELVIS and LabVIEW are trademarks of their owner.").arg(AppInfo.name)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
            wrapMode: Text.WordWrap
        }
    }
}
