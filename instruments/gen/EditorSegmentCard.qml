pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Parameters of the selected segment or component (the ELVISmx editor's right-hand pane):
// Segment Name/Duration/Gain; component Function (operator), type (Library / Expression / Sketch)
// and the type's parameters; Select All summary.
ChannelCard {
    id: root

    required property WaveformEditor editor
    readonly property int kind: editor.selectionKind   // 1 segment, 2 component, 3 all
    readonly property int type: editor.componentType   // 0 library, 1 expression, 2 sketch

    title: kind === 3 ? qsTr("Whole Waveform")
         : kind === 2 ? qsTr("Segment %1 · Component %2").arg(editor.segmentIndex + 1).arg(editor.componentIndex + 1)
         : qsTr("Segment %1").arg(editor.segmentIndex + 1)
    objectName: "editorSegmentCard"
    checkable: false
    color: Theme.accent
    // Library function picked (the picture of the selected component).
    readonly property int libraryIndex: kind === 2 && type === 0 ? editor.libraryFunction : -1

    // --- Segment ---------------------------------------------------------------------------------
    Field {
        visible: root.kind === 1
        Layout.fillWidth: true
        label: qsTr("Name")
        TextField {
            Layout.fillWidth: true
            text: root.editor.segmentName
            placeholderText: qsTr("(no name)")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Name of the segment (shown in the list)")
            onEditingFinished: if (text !== root.editor.segmentName) root.editor.segmentName = text
        }
    }
    KnobField {
        id: durationKnob
        visible: root.kind === 1
        Layout.fillWidth: true
        objectName: "segmentDurationField"
        label: qsTr("Duration")
        // In the document's time unit (s, ms or µs).
        unit: root.editor.timeUnit
        knobSize: 40
        compact: root.compact
        digits: 5
        from: 0.0001
        to: 100000
        logarithmic: true
        toolTip: qsTr("Duration of the segment, 0.0001 to 100 000 %1").arg(root.editor.timeUnit)
        value: root.editor.segmentDuration * root.editor.displayScale
        onValueModified: v => root.editor.segmentDuration = (durationKnob.knob.dragging ? Number(v.toPrecision(3)) : v) / root.editor.displayScale
    }
    FieldRow {
        visible: root.kind === 1
        Layout.fillWidth: true
        Field {
            label: qsTr("Gain")
            hint: qsTr("Set by Scale Selection; multiplies the whole segment")
            SiField {
                Layout.fillWidth: true
                engineering: false
                decimals: 4
                stepSize: 0.1
                toolTip: qsTr("Gain of the segment")
                value: root.editor.segmentGain
                onValueModified: v => root.editor.segmentGain = v
            }
        }
    }

    // --- Component --------------------------------------------------------------------------------
    // Function pictures: the common library functions, a formula or a sketch (Functions tool:
    // the whole library of 21).
    GridLayout {
        objectName: "editorFunctionPictures"
        visible: root.kind === 2
        enabled: !root.editor.drawMode
        Layout.fillWidth: true
        columns: 4
        columnSpacing: 3
        rowSpacing: 3
        Repeater {
            model: [
                { id: "Sine", text: qsTr("Sine"), icon: "wave-sine", type: 0, fn: 1 },
                { id: "Square", text: qsTr("Square"), icon: "wave-square", type: 0, fn: 2 },
                { id: "Triangle", text: qsTr("Tri"), icon: "wave-triangle", type: 0, fn: 3 },
                { id: "Ramp", text: qsTr("Ramp"), icon: "wave-sawtooth", type: 0, fn: 4 },
                { id: "DC", text: qsTr("DC"), icon: "wave-dc", type: 0, fn: 0 },
                { id: "Noise", text: qsTr("Noise"), icon: "wave-noise", type: 0, fn: 5 },
                { id: "Formula", text: qsTr("f(x)"), icon: "edit", type: 1, fn: -1 },
                { id: "Sketch", text: qsTr("Sketch"), icon: "wave-arbitrary", type: 2, fn: -1 }
            ]
            delegate: PictureButton {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitWidth: 40
                leftPadding: 2
                rightPadding: 2
                objectName: "fn" + modelData.id
                text: modelData.text
                iconName: modelData.icon
                checked: root.type === modelData.type && (modelData.fn < 0 || root.libraryIndex === modelData.fn)
                ToolTip.visible: hovered
                ToolTip.text: modelData.type === 1 ? qsTr("Formula of x") : modelData.type === 2 ? qsTr("Sketch: drawn or imported values")
                              : root.editor.functionNames[modelData.fn] ?? ""
                onClicked: {
                    if (modelData.type === 0)
                        root.editor.libraryFunction = modelData.fn
                    root.editor.componentType = modelData.type
                }
            }
        }
    }
    Field {
        visible: root.kind === 2
        Layout.fillWidth: true
        label: qsTr("Combine")
        disabled: !root.editor.operatorEnabled
        SegmentedControl {
            objectName: "componentOperator"
            Layout.fillWidth: true
            compact: true
            enabled: root.editor.operatorEnabled
            model: [ { text: "+", toolTip: qsTr("Add") }, { text: "−", toolTip: qsTr("Subtract") },
                     { text: "×", toolTip: qsTr("Multiply") }, { text: "/", toolTip: qsTr("Divide") },
                     { text: "FM", toolTip: qsTr("Modulate the frequency") } ]
            currentIndex: root.editor.componentOperator
            onActivated: index => root.editor.componentOperator = index
        }
    }

    GridLayout {
        visible: root.kind === 2 && root.type === 0
        Layout.fillWidth: true
        columns: 1
        columnSpacing: Theme.spacingS
        rowSpacing: Theme.spacingXs
        Repeater {
            model: root.kind === 2 && root.type === 0 ? root.editor.params : []
            delegate: KnobField {
                id: paramField
                required property var modelData
                Layout.fillWidth: true
                objectName: "param" + modelData.index
                label: modelData.label
                unit: modelData.unit
                caption: modelData.alias ? qsTr("Aliasing") : ""
                accentColor: modelData.alias ? Theme.warning : Theme.accent
                knobSize: 34
                compact: root.compact
                digits: 5
                from: modelData.min
                to: modelData.max
                stepSize: modelData.stepSize
                toolTip: modelData.alias ? qsTr("%1 · above half the sample rate").arg(modelData.label) : modelData.label
                value: modelData.value
                onValueModified: v => root.editor.setParam(paramField.modelData.index, v)
            }
        }
        Field {
            visible: root.editor.hasSeed
            Layout.fillWidth: true
            label: qsTr("Seed")
            hint: qsTr("Noise seed: the same seed renders the same samples everywhere; 0 = new random values at every render (as ELVISmx)")
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
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Formula of x; the preview and the error follow the typing")
                // Applied while typing (one undo step), so the error text and the preview follow.
                onTextEdited: root.editor.expression = text
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
                      toolTip: qsTr("x at the start of each cycle")
                      value: root.editor.expressionFrom; onValueModified: v => root.editor.expressionFrom = v }
        }
        Field {
            label: qsTr("To")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 4; stepSize: 0.1
                      toolTip: qsTr("x at the end of each cycle")
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
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Name of the sketch")
                onEditingFinished: if (text !== root.editor.sketchName) root.editor.sketchName = text
            }
        }
        Field {
            label: qsTr("Samples")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 0; stepSize: 1; from: 2; to: 1000000
                      toolTip: qsTr("Values in one cycle (2 to 1 000 000); changing it resamples the sketch")
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
                      toolTip: qsTr("Repetitions over the segment")
                      value: root.editor.componentCycles; onValueModified: v => root.editor.componentCycles = v }
        }
        Field {
            label: qsTr("Gain")
            SiField { Layout.fillWidth: true; engineering: false; decimals: 4; stepSize: 0.1
                      toolTip: qsTr("Gain of the component")
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
            enabled: !root.editor.drawMode
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

    SketchImport { id: importFile; editor: root.editor }
}
