pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Parameters of the selected segment or component (the ELVISmx editor's right-hand pane):
// Segment Name/Duration/Gain; component Function (operator), type (Library / Expression / Sketch)
// and the type's parameters; Select All summary.
Section {
    id: root

    required property WaveformEditor editor
    readonly property int kind: editor.selectionKind   // 1 segment, 2 component, 3 all
    readonly property int type: editor.componentType   // 0 library, 1 expression, 2 sketch

    title: kind === 3 ? qsTr("Whole Waveform")
         : kind === 2 ? qsTr("Segment %1 · Component %2").arg(editor.segmentIndex + 1).arg(editor.componentIndex + 1)
         : qsTr("Segment %1").arg(editor.segmentIndex + 1)
    iconName: "sliders"

    // --- Segment ---------------------------------------------------------------------------------
    Field {
        visible: root.kind === 1
        Layout.fillWidth: true
        label: qsTr("Name")
        TextField {
            Layout.fillWidth: true
            text: root.editor.segmentName
            placeholderText: qsTr("(no name)")
            onEditingFinished: if (text !== root.editor.segmentName) root.editor.segmentName = text
        }
    }
    FieldRow {
        visible: root.kind === 1
        Layout.fillWidth: true
        Field {
            label: qsTr("Duration")
            SiField {
                Layout.fillWidth: true
                unit: "s"
                engineering: false
                decimals: 4
                stepSize: 0.1
                from: 0.0001
                to: 100000
                value: root.editor.segmentDuration
                onValueModified: v => root.editor.segmentDuration = v
            }
        }
        Field {
            label: qsTr("Gain")
            hint: qsTr("Set by Scale Selection; multiplies the whole segment")
            SiField {
                Layout.fillWidth: true
                engineering: false
                decimals: 4
                stepSize: 0.1
                value: root.editor.segmentGain
                onValueModified: v => root.editor.segmentGain = v
            }
        }
    }
    Text {
        visible: root.kind === 1
        Layout.fillWidth: true
        text: qsTr("Add components with New Component: library functions, expressions or sketches, combined with + − × / or FM.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    // --- Component --------------------------------------------------------------------------------
    Field {
        visible: root.kind === 2
        Layout.fillWidth: true
        label: qsTr("Type")
        SegmentedControl {
            Layout.fillWidth: true
            compact: true
            model: [qsTr("Library"), qsTr("Expression"), qsTr("Sketch")]
            currentIndex: root.type
            onActivated: index => root.editor.componentType = index
        }
    }
    Field {
        visible: root.kind === 2
        Layout.fillWidth: true
        label: qsTr("Function")
        hint: qsTr("How this component combines with the result of the components above it (not for the first one)")
        disabled: !root.editor.operatorEnabled
        ComboBox {
            Layout.fillWidth: true
            enabled: root.editor.operatorEnabled
            model: [qsTr("+  add"), qsTr("−  subtract"), qsTr("×  multiply"), qsTr("/  divide"), qsTr("FM  modulate the frequency")]
            currentIndex: root.editor.componentOperator
            onActivated: index => root.editor.componentOperator = index
        }
    }

    // Library
    Field {
        visible: root.kind === 2 && root.type === 0
        Layout.fillWidth: true
        label: qsTr("Function Library")
        ComboBox {
            Layout.fillWidth: true
            model: root.editor.functionNames
            currentIndex: root.editor.libraryFunction
            onActivated: index => root.editor.libraryFunction = index
        }
    }
    GridLayout {
        visible: root.kind === 2 && root.type === 0
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingS
        rowSpacing: Theme.spacingXs
        Repeater {
            model: root.kind === 2 && root.type === 0 ? root.editor.params : []
            delegate: Field {
                id: paramField
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: modelData.label
                trailing: modelData.alias ? "⚠" : ""
                hint: modelData.alias ? qsTr("Above half the sample rate: the waveform will alias") : ""
                SiField {
                    Layout.fillWidth: true
                    engineering: false
                    decimals: paramField.modelData.decimals
                    stepSize: paramField.modelData.stepSize
                    unit: paramField.modelData.unit
                    from: paramField.modelData.min
                    to: paramField.modelData.max
                    accentColor: paramField.modelData.alias ? Theme.warning : Theme.accent
                    value: paramField.modelData.value
                    onValueModified: v => root.editor.setParam(paramField.modelData.index, v)
                }
            }
        }
        Field {
            visible: root.editor.hasSeed
            Layout.fillWidth: true
            label: qsTr("Seed")
            hint: qsTr("Noise seed: the same seed renders the same samples everywhere")
            SiField {
                Layout.fillWidth: true
                engineering: false
                decimals: 0
                stepSize: 1
                from: 0
                to: 2147483647
                value: root.editor.seed
                onValueModified: v => root.editor.seed = v
            }
        }
    }

    // Expression
    Field {
        visible: root.kind === 2 && root.type === 1
        Layout.fillWidth: true
        label: qsTr("Expression")
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            Text {
                text: "f(x) ="
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSizeM
            }
            TextField {
                id: expressionField
                Layout.fillWidth: true
                font.family: Theme.monoFamily
                text: root.editor.expression
                onEditingFinished: if (text !== root.editor.expression) root.editor.expression = text
            }
        }
    }
    Text {
        visible: root.kind === 2 && root.type === 1 && root.editor.expressionError.length > 0
        Layout.fillWidth: true
        text: root.editor.expressionError
        color: Theme.danger
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }
    Text {
        visible: root.kind === 2 && root.type === 1
        Layout.fillWidth: true
        text: qsTr("x, pi, e, + − * / ^, abs acos asin atan ceil cos cosh exp floor ln log max min mod sign sin sinh sqrt tan tanh")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeXs
        wrapMode: Text.WordWrap
    }
    GroupLabel { visible: root.kind === 2 && root.type === 1; text: qsTr("X Range") }
    FieldRow {
        visible: root.kind === 2 && root.type === 1
        Layout.fillWidth: true
        Field {
            label: qsTr("From")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 4; stepSize: 0.1
                      value: root.editor.expressionFrom; onValueModified: v => root.editor.expressionFrom = v }
        }
        Field {
            label: qsTr("To")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 4; stepSize: 0.1
                      value: root.editor.expressionTo; onValueModified: v => root.editor.expressionTo = v }
        }
    }

    // Sketch
    FieldRow {
        visible: root.kind === 2 && root.type === 2
        Layout.fillWidth: true
        Field {
            label: qsTr("Name")
            TextField {
                Layout.fillWidth: true
                text: root.editor.sketchName
                onEditingFinished: if (text !== root.editor.sketchName) root.editor.sketchName = text
            }
        }
        Field {
            label: qsTr("Samples")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 0; stepSize: 1; from: 2; to: 1000000
                      value: root.editor.sketchSamples; onValueModified: v => root.editor.sketchSamples = v }
        }
    }
    // Expression and Sketch
    FieldRow {
        visible: root.kind === 2 && root.type >= 1
        Layout.fillWidth: true
        Field {
            label: qsTr("Cycles")
            SiField { Layout.fillWidth: true; engineering: false; decimals: root.type === 2 ? 3 : 4; stepSize: 1; from: 0.0001; to: 1e6
                      value: root.editor.componentCycles; onValueModified: v => root.editor.componentCycles = v }
        }
        Field {
            label: qsTr("Gain")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 4; stepSize: 0.1
                      value: root.editor.componentGain; onValueModified: v => root.editor.componentGain = v }
        }
    }

    RowLayout {
        visible: root.kind === 2 && root.type === 2
        Layout.fillWidth: true
        ActionButton {
            Layout.fillWidth: true
            text: qsTr("Import…")
            iconName: "upload"
            compact: true
            toolTip: qsTr("Replace the sketch values with data from a file")
            onClicked: importFile.open()
        }
        ActionButton {
            Layout.fillWidth: true
            text: root.editor.drawMode ? qsTr("Drawing…") : qsTr("Draw")
            iconName: "edit"
            compact: true
            tone: root.editor.drawMode ? "accent" : "neutral"
            toolTip: qsTr("Draw one cycle of the sketch on the plot (freehand or with points)")
            onClicked: root.editor.drawMode = !root.editor.drawMode
        }
    }

    // --- Select All -----------------------------------------------------------------------------------
    Text {
        visible: root.kind === 3
        Layout.fillWidth: true
        text: qsTr("%1 samples, %2 s. Copy copies the whole waveform, Scale Selection scales every segment, Clear resets the document.")
              .arg(root.editor.totalSamples).arg(root.editor.totalDuration)
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    SketchImport { id: importFile; editor: root.editor }
}
