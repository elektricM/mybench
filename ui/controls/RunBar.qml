pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// Run bar of an instrument panel: Run/Stop toggle, Single (Run Once), status (running, waiting,
// paused, error), optional timeout LED, and panel actions on the right.
//     RunBar {
//         controller: root.controller
//         showTimeout: true
//         extraControls: ActionButton { text: qsTr("Autoscale"); compact: true }
//         IconButton { iconName: "export"; toolTip: qsTr("Export data") }   // right side
//     }
Rectangle {
    id: root

    property InstrumentController controller: null
    property string runText: qsTr("Run")
    property string stopText: qsTr("Stop")
    property string resumeText: qsTr("Resume")
    property string singleText: qsTr("Single")
    property string singleIcon: "step"
    property string singleToolTip: qsTr("Acquire once, then stop")
    property bool showSingle: controller ? controller.supportsSingle : true
    // Single allowed right now (InstrumentController.singleEnabled; override to add conditions).
    property bool singleEnabled: controller ? controller.singleEnabled : true
    // Idle instrument still driving outputs (InstrumentController.holdsOutputs): a button returns
    // them to their safe state (the same as Stop / Shift+F5).
    property bool showRelease: controller !== null && controller.holdsOutputs && !running
    property string releaseText: qsTr("Release outputs")
    property string releaseToolTip: qsTr("Return the outputs to their safe state (AO to 0 V, digital lines to inputs)")
    property bool showTimeout: false
    property string timeoutText: qsTr("Timeout")
    property bool showRate: true
    default property alias actions: actionsRow.data
    property alias extraControls: extraRow.data

    // Narrow panels (split panes): icon-only Single, no rate text, LED-only timeout.
    readonly property bool compact: width < 640
    readonly property bool running: controller !== null && controller.running
    readonly property bool paused: controller !== null && controller.paused
    readonly property string statusText: {
        const c = controller
        if (!c)
            return ""
        switch (c.state) {
        case InstrumentController.Starting:
            return qsTr("Starting…")
        case InstrumentController.Stopping:
            return qsTr("Stopping…")
        case InstrumentController.Running:
            return c.statusText.length > 0 ? c.statusText
                 : c.runMode === InstrumentController.Once ? qsTr("Single acquisition…") : qsTr("Running")
        case InstrumentController.Paused:
            return c.pauseReason
        default:
            if (c.hasError)
                return c.errorText
            if (c.inputWarning.length > 0)
                return c.inputWarning
            if (!DeviceHub.connected)
                return qsTr("No device")
            return c.statusText.length > 0 ? c.statusText : c.hasData ? qsTr("Stopped") : qsTr("Ready")
        }
    }
    readonly property color statusColor: {
        const c = controller
        if (!c)
            return Theme.textMuted
        if (c.state === InstrumentController.Running || c.state === InstrumentController.Starting)
            return Theme.success
        if (c.paused)
            return Theme.warning
        if (c.hasError)
            return Theme.danger
        if (c.inputWarning.length > 0)
            return Theme.warning
        return Theme.textMuted
    }
    readonly property bool statusHighlighted: controller !== null
                                              && (controller.hasError || controller.paused
                                                  || (!running && controller.inputWarning.length > 0))

    implicitHeight: 52
    color: Theme.surface

    Divider {
        anchors.top: parent.top
        width: parent.width
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingM
        spacing: Theme.spacingS + 2

        // Run / Stop / Resume.
        T.Button {
            id: runButton
            readonly property bool stopMode: root.running
            readonly property color fill: root.paused ? Theme.accent
                                        : stopMode ? (hovered ? Theme.stopHover : Theme.stop)
                                        : (hovered ? Theme.runHover : Theme.run)
            enabled: root.controller !== null && (root.running ? root.controller.state !== InstrumentController.Stopping
                                                              : root.controller.canRun)
            implicitWidth: Math.max(96, runContent.implicitWidth + 32)
            implicitHeight: Theme.controlHeightL
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            onClicked: root.controller.toggle()
            Accessible.name: stopMode ? root.stopText : root.paused ? root.resumeText : root.runText

            background: Rectangle {
                radius: Theme.radiusM
                color: runButton.fill
                opacity: runButton.enabled ? 1 : 0.4
                Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, runButton.down ? 0.0 : 0.13) }
                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, runButton.down ? 0.12 : 0.0) }
                    }
                }
                FocusRing { shown: runButton.visualFocus; radius: Theme.radiusM + 3 }
            }
            contentItem: Item {
                Row {
                    id: runContent
                    anchors.centerIn: parent
                    spacing: 7
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: runButton.stopMode ? "stop-filled" : "play"
                        size: Theme.iconSize
                        color: "#FFFFFF"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: runButton.stopMode ? root.stopText : root.paused ? root.resumeText : root.runText
                        color: "#FFFFFF"
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Font.DemiBold
                    }
                }
            }
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: (stopMode ? root.stopText : root.paused ? root.resumeText : root.runText) + "   Space"
        }

        ActionButton {
            visible: root.showSingle
            text: root.compact ? "" : root.singleText
            iconName: root.singleIcon
            toolTip: root.singleToolTip
            implicitHeight: Theme.controlHeightL
            enabled: root.controller !== null && root.singleEnabled
                     && (root.controller.canRun
                         || (root.controller.state === InstrumentController.Running
                             && root.controller.runMode === InstrumentController.Continuous))
            onClicked: root.controller.runOnce()
        }

        ActionButton {
            objectName: "releaseButton"
            visible: root.showRelease
            text: root.compact ? "" : root.releaseText
            iconName: "stop-filled"
            tone: "subtle"
            color: Theme.stop
            toolTip: root.releaseToolTip
            implicitHeight: Theme.controlHeightL
            onClicked: root.controller.returnToSafeState()
        }

        Row {
            id: extraRow
            spacing: Theme.spacingS
            Layout.alignment: Qt.AlignVCenter
        }

        Divider {
            vertical: true
            length: 24
            Layout.leftMargin: Theme.spacingXs
            Layout.rightMargin: Theme.spacingXs
        }

        StatusDot {
            color: root.statusColor
            pulse: root.controller !== null && root.controller.state === InstrumentController.Running
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            text: root.statusText
            color: root.statusHighlighted ? root.statusColor : Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            elide: Text.ElideRight
        }
        Text {
            visible: root.showRate && !root.compact && root.running && root.controller.sampleRate > 0
            text: Units.format(root.controller ? root.controller.sampleRate : 0, "S/s", 4)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
            font.features: Theme.tabularFigures
        }
        Led {
            visible: root.showTimeout
            on: root.controller !== null && root.controller.timedOut
            color: Theme.warning
            text: root.compact ? "" : root.timeoutText
            ToolTip.visible: timeoutHover.hovered
            ToolTip.text: root.timeoutText
            HoverHandler { id: timeoutHover }
        }

        Row {
            id: actionsRow
            spacing: Theme.spacingXs
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
