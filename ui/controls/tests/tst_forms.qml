import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MyBench.Framework
import MyBench.Controls

// Section v2, FormRow and Inspector (docs/UI.md §13.2–13.4).
Item {
    id: root
    width: 900
    height: 700

    Component {
        id: sectionComponent
        Section {
            width: 300
            sectionId: "trigger"
            title: "Trigger"
            summary: "Edge ↑ CH0"
            trailing: Switch { objectName: "enable" }
            FormRow { label: "Type"; SegmentedControl { model: ["Immediate", "Edge"] } }
        }
    }
    Component {
        id: rowComponent
        FormRow {
            width: 300
            label: "Source"
            ComboBox { Layout.fillWidth: true; model: ["AI 0", "Audio Input Right (long entry)"] }
        }
    }
    Component {
        id: segmentedRowComponent
        FormRow {
            label: "Mode"
            SegmentedControl { model: ["Automatic", "Normal", "Single shot", "Roll"] }
        }
    }
    Component {
        id: scrubRowComponent
        FormRow {
            width: 300
            label: "Frequency"
            property alias field: field
            SiField { id: field; Layout.fillWidth: true; unit: "Hz"; from: 0; to: 1e6; value: 1000; onValueModified: v => value = v }
        }
    }
    Component {
        id: inspectorComponent
        Inspector {
            width: 300
            height: 200
            Repeater {
                model: 8
                delegate: Section {
                    required property int index
                    title: "Section " + index
                    FormRow { label: "Value"; SiField { Layout.fillWidth: true } }
                    FormRow { label: "Other"; ComboBox { Layout.fillWidth: true; model: ["a"] } }
                }
            }
        }
    }
    Item { id: hostA; x: 0; width: 300; height: 200 }
    Item { id: hostB; x: 400; width: 320; height: 260 }

    TestCase {
        name: "SectionV2"
        when: windowShown

        function test_headerSummaryTrailing() {
            const section = createTemporaryObject(sectionComponent, root)
            compare(section.sectionId, "trigger")
            const trailing = findChild(section, "enable")
            verify(trailing)
            verify(trailing.visible)
            // The summary shows only while collapsed.
            const summary = findText(section, "Edge ↑ CH0")
            verify(summary)
            verify(!summary.visible)
            section.collapsed = true
            verify(summary.visible)
            verify(trailing.visible)                  // trailing controls stay in the header
        }
        function test_clickTogglesTrailingDoesNot() {
            const section = createTemporaryObject(sectionComponent, root)
            Theme.animations = false
            const expanded = section.implicitHeight
            mouseClick(section, 40, 10)
            verify(section.collapsed)
            verify(section.implicitHeight < expanded)
            const trailing = findChild(section, "enable")
            const p = trailing.mapToItem(section, trailing.width / 2, trailing.height / 2)
            mouseClick(section, p.x, p.y)
            verify(trailing.checked)                  // the switch took the click
            verify(section.collapsed)                 // the header did not toggle
            Theme.animations = true
        }
        function test_aliases() {
            const section = createTemporaryObject(sectionComponent, root, { accentColor: "#ff0000" })
            compare(section.swatchColor, Qt.color("#ff0000"))
            verify(section.hasSwatch)
            section.swatchColor = "transparent"
            verify(!section.hasAccent)
        }
        function test_collapsedStateKept() {
            const key = "section/tst/trigger" + Date.now()
            const section = createTemporaryObject(sectionComponent, root, { stateKey: key })
            verify(!section.collapsed)
            section.toggle()
            compare(SessionManager.viewValue(key), true)
            const other = createTemporaryObject(sectionComponent, root, { stateKey: key })
            verify(other.collapsed)
        }
        function findText(item: Item, text: string): Item {
            for (let i = 0; i < item.children.length; ++i) {
                const child = item.children[i]
                if (child.text === text && child instanceof Text)
                    return child
                const found = findText(child, text)
                if (found)
                    return found
            }
            return null
        }
    }

    TestCase {
        name: "FormRow"
        when: windowShown

        function test_labelColumn() {
            const row = createTemporaryObject(rowComponent, root)
            verify(!row.labelOnTop)                   // a combo elides; it never pushes the label up
            compare(row.columns, 2)
            compare(row.labelWidth, 120)              // 40 % of 300, at most 120
            row.width = 200
            compare(row.labelWidth, 88)               // at least 88
            row.width = 260
            compare(row.labelWidth, 104)
            const combo = row.children[1].children[0]
            tryVerify(() => combo.width > 0)
            verify(Math.abs(combo.x + row.children[1].x - (row.labelWidth + row.spacing)) < 1)
        }
        function test_wideSegmentedMovesLabelUp() {
            const row = createTemporaryObject(segmentedRowComponent, root, { width: 600 })
            verify(!row.labelOnTop)
            row.width = 260
            verify(row.labelOnTop)
            compare(row.columns, 1)
        }
        function test_explicitTop() {
            const row = createTemporaryObject(rowComponent, root, { labelPosition: "top" })
            verify(row.labelOnTop)
        }
        function test_scrubbingTheLabel() {
            const row = createTemporaryObject(scrubRowComponent, root)
            compare(row.scrubTarget, row.field)
            const start = row.field.value
            mousePress(row, 20, 12)
            mouseMove(row, 30, 12)
            mouseMove(row, 50, 12)
            mouseMove(row, 80, 12)
            mouseRelease(row, 80, 12)
            verify(row.field.value > start)
            const after = row.field.value
            mousePress(row, 80, 12)
            mouseMove(row, 60, 12)
            mouseMove(row, 30, 12)
            mouseMove(row, 10, 12)
            mouseRelease(row, 10, 12)
            verify(row.field.value < after)
        }
    }

    TestCase {
        name: "Inspector"
        when: windowShown

        function test_noBackground() {
            const inspector = createTemporaryObject(inspectorComponent, root)
            compare(inspector.color, Qt.color("transparent"))
            verify(!(inspector instanceof Rectangle))
        }
        function test_keepsScrollWhenReparented() {
            const inspector = createTemporaryObject(inspectorComponent, hostA)
            inspector.anchors.fill = hostA
            tryVerify(() => inspector.flickable.contentHeight > inspector.height + 200)
            inspector.contentY = 180
            wait(20)
            inspector.anchors.fill = undefined
            inspector.parent = hostB
            inspector.anchors.fill = hostB
            wait(50)
            compare(inspector.contentY, 180)
            // Through a host with no height (hidden dock) and back.
            inspector.anchors.fill = undefined
            inspector.parent = null
            inspector.parent = hostA
            inspector.anchors.fill = hostA
            wait(50)
            compare(inspector.contentY, 180)
        }
        function test_widths() {
            const inspector = createTemporaryObject(inspectorComponent, root)
            for (const w of [260, 300, 420]) {
                inspector.width = w
                wait(10)
                let section = null
                for (let i = 0; i < inspector.sections.length && !section; ++i)
                    section = inspector.sections[i] as Section
                verify(section)
                compare(section.width, w)
            }
        }
    }
}
