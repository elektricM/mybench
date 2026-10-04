pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Controls

// Content of the Arbitrary tool panel: per Arbitrary channel the waveform (open a file, pick a
// library function, edit it in the Waveform Editor, clear) and the timing (continuous / once,
// hold / linear, return to 0 V); below, "Link update rates" for both channels.
ColumnLayout {
    id: root

    required property GenController controller

    spacing: Theme.spacingM

    Text {
        visible: root.controller.channel0.mode !== GenChannel.Arbitrary
                 && root.controller.channel1.mode !== GenChannel.Arbitrary
        Layout.fillWidth: true
        text: qsTr("Pick Arbitrary on a channel to play a waveform file, a library function or an editor document.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: [root.controller.channel0, root.controller.channel1]
        delegate: ColumnLayout {
            id: block
            required property GenChannel modelData
            readonly property color channelColor: modelData.index === 0 ? Theme.signalAo0 : Theme.signalAo1
            visible: modelData.mode === GenChannel.Arbitrary
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            RowLayout {
                Layout.fillWidth: true
                Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 5; color: block.channelColor }
                Text {
                    Layout.fillWidth: true
                    text: block.modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Font.DemiBold
                }
            }
            Text {
                Layout.fillWidth: true
                text: block.modelData.hasWaveform ? block.modelData.waveformName + " · " + block.modelData.waveformDetails
                                                  : qsTr("No waveform loaded")
                color: block.modelData.hasWaveform ? Theme.text : Theme.textMuted
                elide: Text.ElideMiddle
                font.pixelSize: Theme.fontSizeS
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                Button {
                    text: qsTr("Open…")
                    icon.source: Theme.icon("file")
                    onClicked: {
                        openDialog.channelIndex = block.modelData.index
                        openDialog.open()
                    }
                }
                Button {
                    text: qsTr("Edit")
                    icon.source: Theme.icon("edit")
                    onClicked: root.controller.editChannel(block.modelData.index)
                }
                Button {
                    text: qsTr("Clear")
                    enabled: block.modelData.hasWaveform
                    onClicked: root.controller.clearWaveform(block.modelData.index)
                }
            }
            Text {
                visible: root.controller.libraryNames.length > 0
                text: qsTr("Library")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                Repeater {
                    model: root.controller.libraryNames
                    delegate: Button {
                        required property string modelData
                        required property int index
                        text: modelData
                        flat: true
                        onClicked: root.controller.loadLibraryWaveform(block.modelData.index, index)
                    }
                }
            }
            SegmentedControl {
                Layout.fillWidth: true
                compact: true
                accentColor: block.channelColor
                model: [ { text: qsTr("Continuous") }, { text: qsTr("Once") } ]
                currentIndex: block.modelData.generationMode
                onActivated: index => block.modelData.generationMode = index
            }
            SegmentedControl {
                Layout.fillWidth: true
                compact: true
                accentColor: block.channelColor
                model: [ { text: qsTr("Hold"), toolTip: qsTr("Each sample is held until the next one") },
                         { text: qsTr("Linear"), toolTip: qsTr("Straight lines between the samples") } ]
                currentIndex: block.modelData.interpolation
                onActivated: index => block.modelData.interpolation = index
            }
            Switch {
                visible: block.modelData.generationMode === GenChannel.RunOnce
                text: qsTr("Return to 0 V after Run Once")
                checked: block.modelData.zeroAfterRunOnce
                onToggled: block.modelData.zeroAfterRunOnce = checked
            }
        }
    }

    Switch {
        Layout.fillWidth: true
        text: qsTr("Link update rates")
        checked: root.controller.linkUpdateRates
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Both Arbitrary channels share one Update Rate (as ELVISmx)")
        onToggled: root.controller.linkUpdateRates = checked
    }

    FileDialog {
        id: openDialog
        property int channelIndex: 0
        title: qsTr("Open waveform")
        currentFolder: root.controller.lastWaveformFolder
        nameFilters: [qsTr("Waveforms (*.mbw *.wav *.csv *.txt *.bin *.wdt *.wfm)"), qsTr("All files (*)")]
        onAccepted: root.controller.loadWaveform(channelIndex, selectedFile)
    }
}
