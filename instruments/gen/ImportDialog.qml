pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Import wizards of the Waveform Editor (ELVISmx Text File / Waveform File / binary wizards):
// delimiters, column, start line and sample count for text; channel for multi-channel WAV (and
// LabVIEW .wdt records); the assumptions for raw binary; a live preview; Insert or Replace.
Dialog {
    id: root

    required property WaveformEditor editor
    property url file
    property int mode: 0           // 0: insert a new segment, 1: replace the selected sketch's values
    property var info: ({})
    readonly property bool text: (info.kind ?? "") === "text"

    function openFor(url: url) {
        file = url
        automatic.checked = true
        column.value = 1
        startLine.value = 0
        count.value = 0
        channel.currentIndex = 0
        refresh()
        open()
    }
    function options(): var {
        let delimiters = ""
        if (tab.checked) delimiters += "\t"
        if (semicolon.checked) delimiters += ";"
        if (comma.checked) delimiters += ","
        if (space.checked) delimiters += " "
        if (other.checked && otherChar.text.length > 0) delimiters += otherChar.text[0]
        return { automatic: automatic.checked, delimiters: delimiters, column: column.value,
                 startLine: startLine.value, count: count.value, channel: Math.max(0, channel.currentIndex) }
    }
    function refresh() {
        info = editor.inspectImport(file, options())
    }

    title: text ? qsTr("Text File Wizard") : (info.kind ?? "") === "bin" ? qsTr("Binary File Import") : qsTr("Waveform File Wizard")
    anchors.centerIn: Overlay.overlay
    modal: true
    width: Math.min(680, Overlay.overlay ? Overlay.overlay.width - 40 : 680)
    standardButtons: Dialog.Cancel
    onAccepted: editor.acceptImport(file, options(), mode)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        Text {
            Layout.fillWidth: true
            text: decodeURIComponent(String(root.file).split("/").pop())
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }

        // Text options
        CheckBox {
            id: automatic
            visible: root.text
            text: qsTr("Automatic (header, time column and sample rate detected)")
            checked: true
            onToggled: root.refresh()
        }
        GridLayout {
            visible: root.text && !automatic.checked
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingL
            rowSpacing: Theme.spacingXs
            Text { text: qsTr("Delimiter"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
            RowLayout {
                spacing: Theme.spacingS
                CheckBox { id: tab; text: qsTr("Tab"); checked: true; onToggled: root.refresh() }
                CheckBox { id: semicolon; text: qsTr("Semicolon"); onToggled: root.refresh() }
                CheckBox { id: comma; text: qsTr("Comma"); onToggled: root.refresh() }
                CheckBox { id: space; text: qsTr("Space"); onToggled: root.refresh() }
                CheckBox { id: other; text: qsTr("Other"); onToggled: root.refresh() }
                TextField { id: otherChar; Layout.preferredWidth: 32; maximumLength: 1; enabled: other.checked; onTextEdited: root.refresh() }
            }
            Text { text: qsTr("Column"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
            RowLayout {
                spacing: Theme.spacingL
                SiField { id: column; Layout.preferredWidth: 80; engineering: false; decimals: 0; stepSize: 1; from: 1; to: 1000; value: 1
                          onValueModified: v => { value = v; root.refresh() } }
                Text { text: qsTr("Start Line"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
                SiField { id: startLine; Layout.preferredWidth: 80; engineering: false; decimals: 0; stepSize: 1; from: 0; to: 1e9; value: 0
                          toolTip: qsTr("0: the first line whose column is a number")
                          onValueModified: v => { value = v; root.refresh() } }
                Text { text: qsTr("No. Samples"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
                SiField { id: count; Layout.preferredWidth: 90; engineering: false; decimals: 0; stepSize: 1; from: 0; to: 1e7; value: 0
                          toolTip: qsTr("0: all remaining values")
                          onValueModified: v => { value = v; root.refresh() } }
            }
        }
        Rectangle {
            visible: root.text && !automatic.checked
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            radius: Theme.radiusS
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border
            ScrollView {
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                Text {
                    text: (root.info.lines ?? []).map((l, i) => String(i + 1).padStart(4, " ") + "  " + l).join("\n")
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeS
                }
            }
        }

        // Waveform files: channel choice
        RowLayout {
            visible: !root.text && (root.info.channels ?? 1) > 1
            spacing: Theme.spacingS
            Text { text: qsTr("Channel"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
            ComboBox {
                id: channel
                model: Array.from({ length: root.info.channels ?? 1 }, (_, i) => qsTr("Channel %1").arg(i + 1))
                onActivated: root.refresh()
            }
        }
        Text {
            visible: (root.info.kind ?? "") === "bin"
            Layout.fillWidth: true
            text: qsTr("Raw binary: no header, 16-bit signed little-endian samples, full scale ±5 V (value = raw × 5 / 32768).")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
            wrapMode: Text.WordWrap
        }
        Text {
            visible: (root.info.kind ?? "") === "wdt"
            Layout.fillWidth: true
            text: qsTr("LabVIEW waveform file (NI Waveform Editor): read on a best-effort basis; check the preview.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Waveform Preview")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
        }
        BenchPlot {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            placeholderText: root.info.ok ? "" : (root.info.error ?? "")
            xAxis { min: 0; max: Math.max(1, (root.info.count ?? 1) - 1); title: qsTr("Sample") }
            yAxis { autoScale: true; minimumSpan: 0.1; unit: "V" }
            PlotTrace { buffer: root.editor.importPreview; color: Theme.accent; width: 1.2 }
        }
        Text {
            Layout.fillWidth: true
            text: root.info.ok ? qsTr("%1 samples%2").arg(root.info.count)
                                   .arg(root.info.sampleRate > 0 ? qsTr(" at %1").arg(Units.format(root.info.sampleRate, "S/s")) : qsTr(" (no sample rate: the document's rate is used)"))
                               : qsTr("Cannot import: %1").arg(root.info.error ?? "")
            color: root.info.ok ? Theme.textSecondary : Theme.danger
            font.pixelSize: Theme.fontSizeS
            wrapMode: Text.WordWrap
        }
    }

    footer: RowLayout {
        spacing: Theme.spacingS
        Item { Layout.fillWidth: true }
        SegmentedControl {
            Layout.bottomMargin: Theme.spacingL
            compact: true
            model: [ { text: qsTr("Insert"), toolTip: qsTr("Insert a new segment after the current one") },
                     { text: qsTr("Replace"), toolTip: qsTr("Replace the values of the selected Sketch component") } ]
            currentIndex: root.mode
            onActivated: index => root.mode = index
        }
        ActionButton {
            Layout.bottomMargin: Theme.spacingL
            text: qsTr("Cancel")
            onClicked: root.reject()
        }
        ActionButton {
            Layout.bottomMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            text: qsTr("OK")
            tone: "accent"
            enabled: root.info.ok ?? false
            onClicked: root.accept()
        }
    }
}
