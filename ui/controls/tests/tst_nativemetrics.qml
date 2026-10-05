import QtQuick
import QtTest
import MyBench.Framework
import MyBench.Controls

// Our value fields, steppers and segmented controls have the height of the standard controls
// beside them: 24 px under the macOS style (ctest controls_qml_macos_metrics), lineHeight + 10
// under every other style (MyBench.Style in controls_qml, Fusion offscreen).
Item {
    id: root
    width: 400
    height: 300

    Component {
        id: fieldComponent
        SiField { width: 120; value: 1; unit: "V" }
    }
    Component {
        id: segmentedComponent
        SegmentedControl { model: ["Auto", "Normal", "None"] }
    }
    Component {
        id: stepComponent
        StepSelector { width: 140; model: ["1", "2", "5"] }
    }

    TestCase {
        name: "NativeMetrics"
        when: windowShown

        readonly property bool macStyle: AppInfo.controlsStyle === "macOS"

        function test_controlHeight() {
            if (macStyle) {
                verify(Theme.nativeControls)
                compare(Theme.nativeControlHeight, 24)
                compare(Theme.controlHeight, 24)
            } else {
                compare(Theme.nativeControlHeight, 0)
                compare(Theme.controlHeight, Theme.lineHeight + 10)
            }
        }

        function test_controlsFollow() {
            for (const component of [fieldComponent, segmentedComponent, stepComponent]) {
                const control = createTemporaryObject(component, root)
                verify(control)
                compare(control.implicitHeight, Theme.controlHeight)
            }
        }
    }
}
