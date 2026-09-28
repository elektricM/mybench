import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Edit > Scale Selection: stretches the selection in time (durations x factor, frequencies /
// factor) and scales its amplitude.
Dialog {
    id: root

    required property WaveformEditor editor

    title: qsTr("Scale Selection")
    anchors.centerIn: Overlay.overlay
    modal: true
    width: 360
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: {
        timeFactor.value = 1
        amplitudeFactor.value = 1
    }
    onAccepted: editor.scaleSelection(timeFactor.value, amplitudeFactor.value)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS
        Field {
            Layout.fillWidth: true
            label: qsTr("Time scale factor")
            SiField {
                id: timeFactor
                Layout.fillWidth: true
                engineering: false
                decimals: 4
                stepSize: 0.1
                from: 0.0001
                to: 10000
                value: 1
                onValueModified: v => value = v
            }
        }
        Field {
            Layout.fillWidth: true
            label: qsTr("Amplitude scale factor")
            SiField {
                id: amplitudeFactor
                Layout.fillWidth: true
                engineering: false
                decimals: 4
                stepSize: 0.1
                from: -10000
                to: 10000
                value: 1
                onValueModified: v => value = v
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Applies to the selected segment, component, or the whole waveform (Select All).")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
            wrapMode: Text.WordWrap
        }
    }
}
