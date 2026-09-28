pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import MyBench.Framework
import MyBench.Controls

// Every MyBench.Controls component, for visual review in both themes.
ApplicationWindow {
    id: window

    readonly property InstrumentController demo: InstrumentRegistry.controller("demo")
    property bool themeFromCommandLine: AppInfo.theme.length > 0

    // local state of the standalone examples
    property real amplitude: 2.0
    property real offset: -0.25
    property real duty: 50
    property int waveform: 0
    property int mode: 0
    property real level: 0.4
    property int volts: 6
    property bool ch0: true
    property bool ch1: true
    property bool mathOn: false

    readonly property var voltsPerDiv: [0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5]
    readonly property var voltsLabels: ["5 mV", "10 mV", "20 mV", "50 mV", "100 mV", "200 mV", "500 mV", "1 V", "2 V", "5 V"]
    readonly property var timeLabels: ["5 µs", "10 µs", "20 µs", "50 µs", "100 µs", "200 µs", "500 µs", "1 ms", "2 ms", "5 ms",
                                       "10 ms", "20 ms", "50 ms", "100 ms", "200 ms"]

    width: 1440
    height: 1000
    visible: true
    title: qsTr("Controls Gallery — %1").arg(AppInfo.name)

    Settings {
        id: prefs
        category: "appearance"
        property bool darkTheme: true
    }
    Binding {
        target: Theme
        property: "dark"
        value: window.themeFromCommandLine ? AppInfo.theme === "dark" : prefs.darkTheme
    }
    Binding {
        target: Theme
        property: "animations"
        value: AppInfo.animations
    }

    header: Rectangle {
        implicitHeight: Theme.topBarHeight
        color: Theme.chrome
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingL
            anchors.rightMargin: Theme.spacingM
            spacing: Theme.spacingM
            Icon { name: "app-logo"; size: 22; color: Theme.accent }
            Text {
                text: AppInfo.name
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.Bold
                font.letterSpacing: 0.4
            }
            Text { text: "/"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeL }
            Text {
                text: qsTr("Controls Gallery")
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            ResourceChips {}
            DeviceSelector { Layout.preferredWidth: 240 }
            IconButton {
                iconName: Theme.dark ? "sun" : "moon"
                toolTip: Theme.dark ? qsTr("Light theme") : qsTr("Dark theme")
                onClicked: {
                    prefs.darkTheme = !Theme.dark
                    window.themeFromCommandLine = false
                }
            }
        }
        Divider { anchors.bottom: parent.bottom; width: parent.width }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: grid.implicitHeight + 48
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        GridLayout {
            id: grid
            x: 24
            y: 24
            width: page.width - 48
            columns: width > 1300 ? 3 : 2
            columnSpacing: 16
            rowSpacing: 16

            // --- Buttons ----------------------------------------------------------------
            Card {
                title: qsTr("Buttons")
                subtitle: qsTr("ActionButton tones, IconButton, standard Button")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton { text: qsTr("Neutral") }
                    ActionButton { text: qsTr("Primary"); tone: "accent"; iconName: "save" }
                    ActionButton { text: qsTr("Run"); tone: "success"; iconName: "play" }
                    ActionButton { text: qsTr("Stop all"); tone: "danger"; iconName: "stop-filled" }
                    ActionButton { text: qsTr("Ghost"); tone: "ghost"; iconName: "refresh" }
                    ActionButton { text: qsTr("Subtle"); tone: "subtle"; color: Theme.ch1 }
                    ActionButton { text: qsTr("Disabled"); enabled: false }
                }
                RowLayout {
                    spacing: 4
                    IconButton { iconName: "search"; toolTip: qsTr("Command palette"); shortcutText: Theme.keys("Ctrl+K") }
                    IconButton { iconName: "split"; toolTip: qsTr("Split view") }
                    IconButton { iconName: "pin"; toolTip: qsTr("Pin"); checkable: true; checked: true }
                    IconButton { iconName: "help-circle"; toolTip: qsTr("Help") }
                    IconButton { iconName: "bell"; toolTip: qsTr("Log"); badgeCount: 3 }
                    IconButton { iconName: "settings"; bordered: true; toolTip: qsTr("Settings") }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Button") }
                    Button { text: qsTr("Default"); highlighted: true }
                }
            }

            // --- SiField ------------------------------------------------------------------
            Card {
                title: qsTr("SiField")
                subtitle: qsTr("Typed prefixes (1.5k, 20m), per-digit tuning, coercion")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                FieldRow {
                    Field {
                        label: qsTr("Frequency")
                        trailing: "0.2 Hz – 20 kHz"
                        SiField {
                            Layout.fillWidth: true
                            unit: "Hz"
                            from: 0.2
                            to: 20000
                            value: (window.demo as DemoController)?.frequency ?? 1000
                            onValueModified: v => (window.demo as DemoController).frequency = v
                        }
                    }
                    Field {
                        label: qsTr("Amplitude")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Vpp"
                            from: 0
                            to: 10
                            digits: 3
                            value: window.amplitude
                            onValueModified: v => window.amplitude = v
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("DC Offset")
                        SiField {
                            Layout.fillWidth: true
                            unit: "V"
                            from: -5
                            to: 5
                            digits: 3
                            value: window.offset
                            onValueModified: v => window.offset = v
                        }
                    }
                    Field {
                        label: qsTr("Duty Cycle")
                        SiField {
                            Layout.fillWidth: true
                            unit: "%"
                            engineering: false
                            decimals: 0
                            stepSize: 1
                            from: 1
                            to: 99
                            value: window.duty
                            onValueModified: v => window.duty = v
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Disabled")
                        SiField { Layout.fillWidth: true; unit: "S/s"; value: 200000; enabled: false }
                    }
                    Field {
                        label: qsTr("SpinBox")
                        SpinBox { Layout.fillWidth: true; from: 1; to: 100; value: 5 }
                    }
                }
            }

            // --- Knobs ------------------------------------------------------------------------
            Card {
                title: qsTr("Knob")
                subtitle: qsTr("Ring (discrete) and continuous lin/log; drag, wheel, keys")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    ColumnLayout {
                        spacing: 6
                        Knob {
                            Layout.alignment: Qt.AlignHCenter
                            label: qsTr("Volts/Div")
                            values: window.voltsPerDiv
                            labels: window.voltsLabels
                            currentIndex: window.volts
                            color: Theme.ch0
                            onIndexMoved: i => window.volts = i
                        }
                        ComboBox {
                            Layout.preferredWidth: 96
                            model: window.voltsLabels
                            currentIndex: window.volts
                            onActivated: i => window.volts = i
                        }
                    }
                    ColumnLayout {
                        spacing: 6
                        Knob {
                            Layout.alignment: Qt.AlignHCenter
                            label: qsTr("Time/Div")
                            values: window.timeLabels
                            labels: window.timeLabels
                            currentIndex: (window.demo as DemoController)?.timeDivIndex ?? 0
                            onIndexMoved: i => (window.demo as DemoController).timeDivIndex = i
                        }
                        ComboBox {
                            Layout.preferredWidth: 96
                            model: window.timeLabels
                            currentIndex: (window.demo as DemoController)?.timeDivIndex ?? 0
                            onActivated: i => (window.demo as DemoController).timeDivIndex = i
                        }
                    }
                    ColumnLayout {
                        spacing: 6
                        Knob {
                            Layout.alignment: Qt.AlignHCenter
                            label: qsTr("Frequency")
                            from: 0.2
                            to: 20000
                            logarithmic: true
                            unit: "Hz"
                            minLabel: "200m"
                            maxLabel: "20k"
                            value: (window.demo as DemoController)?.frequency ?? 1000
                            color: Theme.accent
                            onMoved: v => (window.demo as DemoController).frequency = v
                        }
                        SiField {
                            Layout.preferredWidth: 104
                            unit: "Hz"
                            from: 0.2
                            to: 20000
                            value: (window.demo as DemoController)?.frequency ?? 1000
                            onValueModified: v => (window.demo as DemoController).frequency = v
                        }
                    }
                    ColumnLayout {
                        spacing: 6
                        Knob {
                            Layout.alignment: Qt.AlignHCenter
                            label: qsTr("Level")
                            from: -1
                            to: 1
                            unit: "V"
                            value: window.level
                            color: Theme.math
                            diameter: 48
                            onMoved: v => window.level = v
                        }
                    }
                }
            }

            // --- Choices ----------------------------------------------------------------------
            Card {
                title: qsTr("Choices")
                subtitle: qsTr("SegmentedControl, ComboBox, CheckBox, RadioButton, switches")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Field {
                    label: qsTr("Acquisition Mode")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Run Continuously"), qsTr("Run Once")]
                        currentIndex: window.mode
                        onActivated: i => window.mode = i
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Waveform")
                        SegmentedControl {
                            model: [{ icon: "wave-sine-line", toolTip: qsTr("Sine") },
                                    { icon: "wave-square-line", toolTip: qsTr("Square") },
                                    { icon: "wave-triangle-line", toolTip: qsTr("Triangle") },
                                    { icon: "wave-arbitrary-line", toolTip: qsTr("Arbitrary") }]
                            currentIndex: window.waveform
                            accentColor: Theme.ch0
                            onActivated: i => window.waveform = i
                        }
                    }
                    Field {
                        label: qsTr("Source")
                        ComboBox { Layout.fillWidth: true; model: ["AI 0", "AI 1", "AudioInput Left", "AudioInput Right"] }
                    }
                }
                RowLayout {
                    spacing: 16
                    CheckBox { text: qsTr("Cursors On"); checked: true }
                    CheckBox { text: qsTr("Autoscale") }
                    RadioButton { text: qsTr("Rising"); checked: true }
                    RadioButton { text: qsTr("Falling") }
                }
                RowLayout {
                    spacing: 20
                    ToggleSwitch { text: qsTr("Null offset"); checked: true }
                    Switch { text: qsTr("Hold") }
                    ToggleSwitch { text: qsTr("Disabled"); enabled: false }
                }
            }

            // --- Indicators -----------------------------------------------------------------
            Card {
                title: qsTr("Indicators")
                subtitle: qsTr("Led, StatusDot, Badge, ChannelChip, KeyHint")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                RowLayout {
                    spacing: 18
                    Led { on: true; text: qsTr("Running") }
                    Led { on: true; color: Theme.warning; text: qsTr("Timeout") }
                    Led { on: false; color: Theme.warning; text: qsTr("Timeout") }
                    Led { on: true; color: Theme.danger; text: qsTr("OL") }
                    Led { on: true; color: Theme.ch1; size: 14 }
                }
                RowLayout {
                    spacing: 8
                    Repeater {
                        model: 8
                        delegate: Led {
                            required property int index
                            on: index % 3 !== 1
                            color: Theme.ch0
                            size: 16
                        }
                    }
                    Item { Layout.preferredWidth: 12 }
                    StatusDot { color: Theme.success; pulse: true }
                    StatusDot { color: Theme.warning }
                    StatusDot { color: Theme.danger }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Badge { text: qsTr("Simulated"); color: Theme.simulated }
                    Badge { text: qsTr("Running"); color: Theme.success }
                    Badge { text: qsTr("Paused"); color: Theme.warning }
                    Badge { text: "AI"; color: Theme.accent }
                    Badge { text: qsTr("Solid"); color: Theme.accent; solid: true }
                    KeyHint { keys: "Ctrl+K" }
                    KeyHint { keys: "Space" }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ChannelChip { text: "CH 0"; detail: "1 V/div"; color: Theme.ch0; checked: window.ch0; onToggled: window.ch0 = checked }
                    ChannelChip { text: "CH 1"; detail: "500 mV/div"; color: Theme.ch1; checked: window.ch1; onToggled: window.ch1 = checked }
                    ChannelChip { text: qsTr("Math"); color: Theme.math; checked: window.mathOn; onToggled: window.mathOn = checked }
                    ChannelChip { text: qsTr("Ref"); color: Theme.ref; checked: false }
                    ChannelChip { text: "AO 0"; color: Theme.ch0; compact: true }
                }
            }

            // --- Readouts -----------------------------------------------------------------------
            Card {
                title: qsTr("Readout")
                subtitle: qsTr("Live from the demo instrument (simulated DMM)")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: readout.implicitHeight + 32
                    radius: Theme.radiusL
                    color: Theme.display
                    border.width: 1
                    border.color: Theme.displayBorder
                    Readout {
                        id: readout
                        anchors.centerIn: parent
                        width: parent.width - 48
                        label: qsTr("DC Voltage")
                        value: (window.demo as DemoController)?.reading ?? NaN
                        overload: (window.demo as DemoController)?.overload ?? false
                        stale: window.demo !== null && !window.demo.running
                        unit: "V"
                        digits: 4
                        caption: qsTr("Range 2 V · Specify range")
                        showBar: true
                        barValue: ((window.demo as DemoController)?.reading ?? 0) / 2
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Readout { Layout.fillWidth: true; label: "RMS"; value: 0.7071; unit: "V"; fontSize: 22; color: Theme.ch0 }
                    Readout { Layout.fillWidth: true; label: qsTr("Freq"); value: 1000.2; unit: "Hz"; fontSize: 22; color: Theme.ch0 }
                    Readout { Layout.fillWidth: true; label: "Vp-p"; overload: true; unit: "V"; fontSize: 22 }
                }
            }

            // --- Inspector ------------------------------------------------------------------------
            Card {
                title: qsTr("Inspector & Section")
                subtitle: qsTr("Collapsible cards with channel colors")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.rowSpan: 2
                Inspector {
                    Layout.fillWidth: true
                    implicitHeight: 560
                    color: Theme.surfaceSunken
                    radius: Theme.radiusL
                    Section {
                        title: qsTr("Channel 0 Settings")
                        accentColor: Theme.ch0
                        summary: "AI 0 · " + window.voltsLabels[window.volts]
                        headerTools: ToggleSwitch { checked: window.ch0; onToggled: window.ch0 = checked }
                        Field {
                            label: qsTr("Source")
                            ComboBox { Layout.fillWidth: true; model: ["AI 0", "AI 1"] }
                        }
                        FieldRow {
                            Field {
                                label: qsTr("Probe")
                                disabled: true
                                ComboBox { Layout.fillWidth: true; model: ["1x"]; enabled: false }
                            }
                            Field {
                                label: qsTr("Coupling")
                                ComboBox { Layout.fillWidth: true; model: ["DC", "AC"] }
                            }
                        }
                        FieldRow {
                            Field {
                                label: qsTr("Volts/Div")
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: window.voltsLabels
                                    currentIndex: window.volts
                                    onActivated: i => window.volts = i
                                }
                            }
                            Field {
                                label: qsTr("Vertical Position (Div)")
                                SiField { Layout.fillWidth: true; engineering: false; decimals: 1; stepSize: 0.1; from: -5; to: 5; value: 0 }
                            }
                        }
                    }
                    Section {
                        title: qsTr("Channel 1 Settings")
                        accentColor: Theme.ch1
                        summary: "AI 1 · 500 mV"
                        collapsed: true
                        Field { label: qsTr("Source"); ComboBox { Layout.fillWidth: true; model: ["AI 1"] } }
                    }
                    Section {
                        title: qsTr("Timebase")
                        summary: window.timeLabels[(window.demo as DemoController)?.timeDivIndex ?? 0] + "/div"
                        Field {
                            label: qsTr("Time/Div")
                            ComboBox {
                                Layout.fillWidth: true
                                model: window.timeLabels
                                currentIndex: (window.demo as DemoController)?.timeDivIndex ?? 0
                                onActivated: i => (window.demo as DemoController).timeDivIndex = i
                            }
                        }
                    }
                    Section {
                        title: qsTr("Trigger")
                        FieldRow {
                            Field { label: qsTr("Type"); ComboBox { Layout.fillWidth: true; model: [qsTr("Immediate"), qsTr("Edge")] } }
                            Field {
                                label: qsTr("Slope")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: [{ icon: "slope-rising-line", toolTip: qsTr("Rising") }, { icon: "slope-falling-line", toolTip: qsTr("Falling") }]
                                }
                            }
                        }
                        Field {
                            label: qsTr("Level")
                            SiField { Layout.fillWidth: true; unit: "V"; from: -10; to: 10; value: window.level; onValueModified: v => window.level = v }
                        }
                        Field {
                            label: qsTr("Horizontal Position (%)")
                            Slider { Layout.fillWidth: true; from: 0; to: 100; value: 50 }
                        }
                    }
                }
            }

            // --- Run bar ------------------------------------------------------------------------
            Card {
                title: qsTr("RunBar")
                subtitle: qsTr("Bound to the demo instrument — try Run, Single, Space")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: runBar.implicitHeight
                    radius: Theme.radiusL
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    clip: true
                    RunBar {
                        id: runBar
                        anchors.fill: parent
                        color: "transparent"
                        controller: window.demo
                        showTimeout: true
                        IconButton { iconName: "export"; toolTip: qsTr("Export data") }
                        IconButton { iconName: "help-circle"; toolTip: qsTr("Help") }
                    }
                }
            }

            // --- Feedback ---------------------------------------------------------------------------
            Card {
                title: qsTr("Feedback")
                subtitle: qsTr("Banner, toasts (bottom right), progress")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Banner {
                    Layout.fillWidth: true
                    tone: "warning"
                    text: qsTr("Scope paused — AI in use by Bode")
                    ActionButton { text: qsTr("Resume"); iconName: "play"; compact: true; tone: "subtle"; color: Theme.warning }
                }
                Banner {
                    Layout.fillWidth: true
                    tone: "danger"
                    text: qsTr("USB communication error")
                    detail: qsTr("Check the cable and reconnect the device.")
                    closable: true
                }
                Banner { Layout.fillWidth: true; tone: "info"; text: qsTr("Autoscale adjusted Volts/Div to 500 mV") }
                RowLayout {
                    spacing: 8
                    ActionButton { text: qsTr("Info"); compact: true; onClicked: Notifications.notify(Notifications.Info, qsTr("myDAQ connected"), qsTr("Press Run to resume.")) }
                    ActionButton { text: qsTr("Success"); compact: true; onClicked: Notifications.notify(Notifications.Success, qsTr("Session saved"), "bench.mybench") }
                    ActionButton { text: qsTr("Warning"); compact: true; onClicked: Notifications.notify(Notifications.Warning, qsTr("Scope paused — AI in use by Bode"), qsTr("Press Resume to take it back."), qsTr("Resume")) }
                    ActionButton { text: qsTr("Error"); compact: true; onClicked: Notifications.notify(Notifications.Error, qsTr("Oscilloscope"), qsTr("Data were lost because the computer could not read fast enough.")) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ProgressBar { Layout.fillWidth: true; value: 0.62 }
                    ProgressBar { Layout.fillWidth: true; indeterminate: true }
                    BusyIndicator { running: true }
                }
            }

            // --- Tabs, fields, sliders ------------------------------------------------------------
            Card {
                title: qsTr("Tabs, text, sliders, menus")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                TabBar {
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Basic Settings") }
                    TabButton { text: qsTr("Advanced Settings") }
                    TabButton { text: qsTr("Measurements") }
                }
                FieldRow {
                    Field { label: qsTr("Session name"); TextField { Layout.fillWidth: true; placeholderText: qsTr("Untitled") } }
                    Field {
                        label: qsTr("Menu")
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Session ▾")
                            onClicked: menu.popup()
                            Menu {
                                id: menu
                                MenuItem { text: qsTr("New Session"); icon.source: Theme.icon("file-plus") }
                                MenuItem { text: qsTr("Open…"); icon.source: Theme.icon("folder-open") }
                                MenuItem { text: qsTr("Save"); icon.source: Theme.icon("save") }
                                MenuSeparator {}
                                MenuItem { text: qsTr("Pin dashboard"); checkable: true; checked: true }
                            }
                        }
                    }
                }
                Slider { Layout.fillWidth: true; value: 0.3 }
            }
        }
    }

    ToastArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingL
    }

    Component.onCompleted: {
        if (AppInfo.screenshot) {
            Notifications.notify(Notifications.Warning, qsTr("Scope paused — AI in use by Bode"), qsTr("Press Resume to take it back."), qsTr("Resume"), 0)
            Notifications.notify(Notifications.Success, qsTr("Session saved"), "bench.mybench", "", 0)
        }
    }
}
