pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Simulator settings (app.md 2.7, MYBENCH extension): what the simulated myDAQ "sees", for
// teaching and testing without hardware. Connection, the test signal of each undriven input,
// AI noise, the AO 0 -> AI 1 RC corner, and the DMM input per function. Changes apply at once.
Dialog {
    id: dialog

    anchors.centerIn: Overlay.overlay
    width: Math.min(760, (Overlay.overlay ? Overlay.overlay.width : 800) - 48)
    height: Math.min(implicitHeight, (Overlay.overlay ? Overlay.overlay.height : 800) - 48)
    modal: true
    title: qsTr("Simulator settings")
    standardButtons: Dialog.Close

    readonly property int nameWidth: 84
    readonly property int waveWidth: 118
    readonly property int fieldWidth: 124
    // mydaq::dsp::Waveform order.
    readonly property var waveformNames: [qsTr("Sine"), qsTr("Square"), qsTr("Triangle"), qsTr("Sawtooth"), qsTr("DC"), qsTr("Noise")]
    readonly property var inputNames: [qsTr("AI 0"), qsTr("AI 1"), qsTr("Audio In L"), qsTr("Audio In R")]
    // mydaq::DmmFunction order: label, unit, range.
    readonly property var dmmFunctions: [
        { name: qsTr("DC voltage"), unit: "V", from: -60, to: 60 },
        { name: qsTr("AC voltage (rms)"), unit: "V", from: 0, to: 20 },
        { name: qsTr("DC current"), unit: "A", from: -1, to: 1 },
        { name: qsTr("AC current (rms)"), unit: "A", from: 0, to: 1 },
        { name: qsTr("Resistance"), unit: "Ω", from: 0, to: 100e6 },
        { name: qsTr("Diode"), unit: "V", from: 0, to: 2 },
        { name: qsTr("Continuity"), unit: "Ω", from: 0, to: 10e3 }
    ]

    component Heading: Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingS
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.DemiBold
        font.letterSpacing: 1
    }
    component Caption: Text {
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeM
    }

    contentItem: ScrollView {
        id: scroll
        implicitHeight: content.implicitHeight
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: content
            width: scroll.availableWidth
            spacing: Theme.spacingS

            Heading { text: qsTr("CONNECTION") }
            RowLayout {
                Layout.fillWidth: true
                Caption { Layout.fillWidth: true; text: qsTr("Plugged in (off: simulates unplugging the USB cable)") }
                ToggleSwitch {
                    checked: DeviceHub.simulatorPlugged
                    Accessible.name: qsTr("Plugged in")
                    onToggled: DeviceHub.simulatorPlugged = checked
                }
            }

            Divider { Layout.fillWidth: true }
            Heading { text: qsTr("ANALOG INPUTS") }
            Text {
                Layout.fillWidth: true
                text: qsTr("An input that no output drives shows its test signal. AO 0 drives AI 0 directly and AI 1 through an RC low-pass; Audio Out drives Audio In.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Caption { Layout.preferredWidth: dialog.nameWidth; text: qsTr("Input") }
                Caption { Layout.preferredWidth: dialog.waveWidth; text: qsTr("Waveform") }
                Caption { Layout.preferredWidth: dialog.fieldWidth; text: qsTr("Frequency") }
                Caption { Layout.preferredWidth: dialog.fieldWidth; text: qsTr("Amplitude (peak)") }
                Caption { Layout.preferredWidth: dialog.fieldWidth; text: qsTr("Offset") }
                Caption { Layout.fillWidth: true; text: qsTr("Duty cycle") }
            }
            Repeater {
                model: 4
                delegate: SignalRow {}
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingM
                rowSpacing: Theme.spacingS
                Caption { Layout.fillWidth: true; text: qsTr("Noise on every AI sample (rms)") }
                SiField {
                    Layout.preferredWidth: 130
                    label: qsTr("Noise")
                    unit: "V"
                    from: 0
                    to: 1
                    value: DeviceHub.simulatorNoiseRms
                    onValueModified: v => DeviceHub.simulatorNoiseRms = v
                }
                Caption { Layout.fillWidth: true; text: qsTr("RC corner (AO 0 → AI 1)") }
                SiField {
                    Layout.preferredWidth: 130
                    label: qsTr("RC corner")
                    unit: "Hz"
                    from: 1
                    to: 100000
                    value: DeviceHub.simulatorRcCutoff
                    onValueModified: v => DeviceHub.simulatorRcCutoff = v
                }
            }

            Divider { Layout.fillWidth: true }
            Heading { text: qsTr("MULTIMETER") }
            RowLayout {
                Layout.fillWidth: true
                Caption { Layout.fillWidth: true; text: qsTr("Test leads open (Ω, diode and continuity read overload)") }
                ToggleSwitch {
                    checked: DeviceHub.simulatorDmmOpenCircuit
                    Accessible.name: qsTr("DMM leads open")
                    onToggled: DeviceHub.simulatorDmmOpenCircuit = checked
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: Theme.spacingM
                rowSpacing: Theme.spacingS
                Repeater {
                    model: dialog.dmmFunctions
                    delegate: RowLayout {
                        id: dmmRow
                        required property var modelData
                        required property int index
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: Theme.spacingM
                        Caption { Layout.fillWidth: true; text: dmmRow.modelData.name }
                        SiField {
                            Layout.preferredWidth: 130
                            label: dmmRow.modelData.name
                            unit: dmmRow.modelData.unit
                            from: dmmRow.modelData.from
                            to: dmmRow.modelData.to
                            value: Number(DeviceHub.simulatorDmmValues[dmmRow.index] ?? 0)
                            onValueModified: v => DeviceHub.setSimulatorDmmValue(dmmRow.index, v)
                        }
                    }
                }
            }
        }
    }

    // One analog input's test signal.
    component SignalRow: RowLayout {
        id: row
        required property int index
        readonly property var testSignal: DeviceHub.simulatorTestSignals[index] ?? ({})
        readonly property int waveform: Number(testSignal.waveform ?? 0)
        readonly property bool audio: index >= 2
        function set(key: string, value: var) {
            const changes = {}
            changes[key] = value
            DeviceHub.setSimulatorTestSignal(index, changes)
        }
        Layout.fillWidth: true
        spacing: Theme.spacingS

        Text {
            Layout.preferredWidth: dialog.nameWidth
            text: dialog.inputNames[row.index]
            color: Theme.text
            font.pixelSize: Theme.fontSizeM
            font.weight: Font.Medium
        }
        ComboBox {
            Layout.preferredWidth: dialog.waveWidth
            model: dialog.waveformNames
            currentIndex: row.waveform
            Accessible.name: qsTr("%1 waveform").arg(dialog.inputNames[row.index])
            onActivated: i => row.set("waveform", i)
        }
        SiField {
            Layout.preferredWidth: dialog.fieldWidth
            label: qsTr("%1 frequency").arg(dialog.inputNames[row.index])
            unit: "Hz"
            from: 0.001
            to: 100000
            enabled: row.waveform !== 4 && row.waveform !== 5
            value: Number(row.testSignal.frequency ?? 0)
            onValueModified: v => row.set("frequency", v)
        }
        SiField {
            Layout.preferredWidth: dialog.fieldWidth
            label: qsTr("%1 amplitude").arg(dialog.inputNames[row.index])
            unit: "V"
            from: 0
            to: 20
            enabled: row.waveform !== 4
            value: Number(row.testSignal.amplitude ?? 0)
            onValueModified: v => row.set("amplitude", v)
        }
        SiField {
            Layout.preferredWidth: dialog.fieldWidth
            label: qsTr("%1 offset").arg(dialog.inputNames[row.index])
            toolTip: row.audio ? qsTr("Audio inputs are AC coupled: no offset") : ""
            unit: "V"
            from: -10
            to: 10
            enabled: !row.audio
            value: Number(row.testSignal.offset ?? 0)
            onValueModified: v => row.set("offset", v)
        }
        SiField {
            Layout.fillWidth: true
            label: qsTr("%1 duty cycle").arg(dialog.inputNames[row.index])
            unit: "%"
            engineering: false
            decimals: 0
            from: 1
            to: 99
            enabled: row.waveform === 1
            value: Math.round(Number(row.testSignal.dutyCycle ?? 0.5) * 100)
            onValueModified: v => row.set("dutyCycle", v / 100)
        }
    }
}
