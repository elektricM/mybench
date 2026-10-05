pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework

// Header of an instrument pane (docs/UI.md §7 "Pane header", §13.5): icon, name and state text
// on the left; up to 3 instrument extras, Single, the Run/Stop toggle, Release Outputs (only while
// outputs are held and the instrument is stopped) and the ⋯ menu on the right. InstrumentPanel
// creates it; panels set its options through `runBar.*` (alias) or the InstrumentPanel properties.
//     PaneHeader {
//         controller: root.controller
//         extras: [ Button { text: qsTr("Autoscale") } ]
//         showTimeout: true
//         onExportDataRequested: ...
//     }
Rectangle {
    id: header

    // The controller, or the registry's controller of `instrumentId`.
    property string instrumentId: ""
    property InstrumentController controller: instrumentId.length > 0 ? InstrumentRegistry.controller(instrumentId) : null
    property string title: controller ? controller.title : ""
    property string iconName: controller ? (InstrumentRegistry.info(controller.instrumentId).icon ?? "") : ""
    // Overrides the state text derived from the controller.
    property string stateText: ""
    // Secondary state detail ("200 kS/s"): the controller's summary, else this text, else the rate.
    property string detailText: ""
    property bool focused: false
    // The shell's split: an × at the end that emits closeRequested() (Close Split; it never stops
    // the instrument), and a short note before the instrument items ("Split hidden — window too
    // narrow").
    property bool closable: false
    property string closeToolTip: qsTr("Close Split")
    property string note: ""
    // The shell's workspace: the icon and name can be dragged (`titleDrag`; the shell shows the
    // drag image and handles the drop: swap the panes, or close the pane on the sidebar).
    property string paneId: ""
    property bool draggable: false
    property alias titleDrag: titleDragHandler
    // Narrow pane: Single and Release lose their labels, the state detail is hidden.
    property bool compact: width < 640
    // Split-size pane (< 700): the ↺ button folds into ⋯ (which has Reset Settings…).
    readonly property bool narrow: width < 700
    // What the row lacks at preferred widths, and what the title can give before the instrument
    // items are clipped: in a narrow pane the title elides first.
    readonly property real rowDeficit: Math.max(0, row.implicitWidth - row.width)
    readonly property real titleSlack: Math.max(0, titleArea.implicitWidth - titleArea.Layout.minimumWidth)

    // Run options (the names of the v1 RunBar).
    property string runText: qsTr("Run")
    property string stopText: qsTr("Stop")
    property string resumeText: qsTr("Resume")
    property string singleText: qsTr("Single")
    property string singleIcon: "step"
    property string singleToolTip: qsTr("Acquire once, then stop")
    property bool showSingle: controller ? controller.supportsSingle : false
    property bool singleEnabled: controller ? controller.singleEnabled : false
    property bool showRelease: controller !== null && controller.holdsOutputs && !running
    property string releaseText: qsTr("Release Outputs")
    property string releaseToolTip: qsTr("Return the outputs to their safe state (AO to 0 V, digital lines to inputs)")
    property bool showTimeout: false
    property string timeoutText: qsTr("Timeout")
    property bool showRate: true

    // Header items before Single: instrument tools (alias `actions`, v1 `runBarActions`), display
    // tools (v1 `displayTools`) and the extras (`headerControls`, v1 `runBarControls`).
    default property alias actions: actionsRow.data
    property alias displayTools: displayToolsRow.data
    property alias extras: extrasRow.data
    property alias extraControls: legacyExtrasRow.data
    // Panel content between the run controls and ⋯ (InstrumentPanel.headerContent): a RunButton,
    // knob fields… The header grows to fit taller items (Theme.paneHeaderHeight at least).
    property alias content: contentRow.data
    // false: no Run/Stop and Single of the header, and no state text (the panel provides its own
    // run controls with their state, e.g. a RunButton in `content`).
    property bool showRunControls: true
    // Extra ⋯ entries (MenuItem, Menu, Action, MenuSeparator), after the instrument's commands.
    property list<QtObject> menuItems

    // ⋯ menu entries provided by the panel.
    property bool showExport: true
    property bool hasTraceStyles: false
    property bool measurementsAvailable: false
    property bool measurementsShown: true
    property bool showInspectorToggle: false
    property bool inspectorShown: true

    signal exportDataRequested()
    signal exportImageRequested()
    signal printRequested()
    signal resetRequested()
    signal helpRequested()
    signal traceStyleRequested()
    signal measurementsToggled()
    signal inspectorToggleRequested()
    // The × of a closable header (the shell closes the split pane).
    signal closeRequested()
    // (alias) Split-pane header of the old app shell.
    signal popOutRequested()

    readonly property bool running: controller !== null && controller.running
    readonly property bool paused: controller !== null && controller.paused
    readonly property bool timedOut: showTimeout && controller !== null && controller.timedOut
    // "running" | "waiting" | "paused" | "error" | "stopped" (the state dot)
    readonly property string status: {
        const c = controller
        if (!c)
            return "stopped"
        if (c.paused)
            return "paused"
        if (c.running)
            return c.timedOut ? "waiting" : "running"
        if (c.hasError)
            return "error"
        return "stopped"
    }
    // The derived state text (v1 RunBar.statusText).
    readonly property string statusText: {
        const c = controller
        if (!c)
            return ""
        switch (c.state) {
        case InstrumentController.Starting:
            return startingTimer.slow ? qsTr("Starting…") : qsTr("Running")
        case InstrumentController.Stopping:
            return qsTr("Stopping…")
        case InstrumentController.Running:
            return c.statusText.length > 0 ? c.statusText
                 : c.runMode === InstrumentController.Once ? qsTr("Single acquisition") : qsTr("Running")
        case InstrumentController.Paused:
            return c.pauseReason.length > 0 ? c.pauseReason : qsTr("Paused")
        default:
            if (c.hasError)
                return qsTr("Error")
            if (!DeviceHub.connected)
                return qsTr("No device")
            return c.statusText.length > 0 ? c.statusText : c.hasData ? qsTr("Stopped") : qsTr("Ready")
        }
    }
    readonly property string effectiveStateText: stateText.length > 0 ? stateText : statusText
    readonly property string effectiveDetail: {
        if (stateText.length > 0 || controller === null || paused)
            return ""
        if (controller.summary.length > 0)
            return controller.summary
        if (detailText.length > 0)
            return detailText
        return running && showRate && controller.sampleRate > 0 ? Units.format(controller.sampleRate, "S/s", 3) : ""
    }

    // The width the row needs to show the content inline with the full title and state text, from
    // the items' own widths (not row.implicitWidth, which depends on where the content is).
    readonly property real inlineWidth: {
        const items = [noteLabel, displayToolsRow, actionsRow, extrasRow, legacyExtrasRow, singleButton,
                       runButton, releaseButton, inspectorButton, resetButton, moreButton, closeButton]
        let width = row.anchors.leftMargin + row.anchors.rightMargin + titleArea.implicitWidth
                    + stateArea.Layout.minimumWidth + stateArea.Layout.leftMargin + row.spacing
        for (const item of items) {
            if (item.visible)
                width += Math.max(item.implicitWidth, item.Layout.minimumWidth) + row.spacing
        }
        return width + contentRow.implicitWidth + row.spacing
    }
    // Content that does not fit beside the title moves to a second row under it: it is never
    // clipped or squeezed over its neighbours (Alex, round 4: empty Scope bar, overlapping Bode bar).
    // (`hasContent`, not contentRow.visible: the effective visibility includes the parent's, which
    // would keep a hidden slot hidden for good.)
    readonly property bool hasContent: contentRow.children.length > 0
    readonly property bool contentWrapped: hasContent && inlineWidth > width
    readonly property real rowHeight: Math.max(Theme.paneHeaderHeight,
                                               hasContent && !contentWrapped ? contentRow.implicitHeight + 2 * Theme.spacingXs + 1 : 0)
    implicitHeight: rowHeight + (contentWrapped ? contentRow.implicitHeight + 2 * Theme.spacingXs : 0)
    implicitWidth: 480
    color: Theme.window

    // "Starting…" only for a start slower than 300 ms (docs/UI.md §11).
    Timer {
        id: startingTimer
        property bool slow: false
        interval: 300
        running: header.controller !== null && header.controller.state === InstrumentController.Starting
        onRunningChanged: if (running) slow = false
        onTriggered: slow = true
    }

    // Bottom edge: a hairline, or the 2 px accent line of the focused pane.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: header.focused ? 2 : 1
        color: header.focused ? Theme.accent : Theme.separator
    }

    // Second row: the instrument content when it does not fit beside the title.
    Item {
        id: wrapSlot
        objectName: "paneHeaderWrapRow"
        visible: header.contentWrapped
        clip: true
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        anchors.bottomMargin: 1
        height: header.contentWrapped ? contentRow.implicitHeight + 2 * Theme.spacingXs : 0
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: header.rowHeight - (header.contentWrapped ? 0 : 1)
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        RowLayout {
            id: titleArea
            objectName: "paneTitleArea"
            spacing: Theme.spacingS
            Layout.fillHeight: true
            // Shrinkable down to the icon and a few letters; never wider than the title.
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.minimumWidth: (titleIcon.visible ? titleIcon.implicitWidth + spacing : 0)
                                 + Math.min(titleLabel.implicitWidth, 40)
            Icon {
                id: titleIcon
                visible: header.iconName.length > 0
                name: header.iconName
                size: Theme.iconSize
                color: Theme.textSecondary
            }
            Text {
                id: titleLabel
                objectName: "paneTitle"
                text: header.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeHeading
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: Math.min(implicitWidth, 40)
                Layout.maximumWidth: Math.max(80, header.width * 0.3)
            }
            DragHandler {
                id: titleDragHandler
                target: null
                enabled: header.draggable && header.paneId.length > 0
                cursorShape: Qt.ClosedHandCursor
            }
            HoverHandler {
                enabled: titleDragHandler.enabled
                cursorShape: Qt.OpenHandCursor
            }
        }

        // State: dot, text and detail. Elides before anything on the right shrinks.
        RowLayout {
            id: stateArea
            Layout.fillWidth: true
            // The state label's own minimum (+ the dot): squeezed further, it would overlap the
            // items on its right.
            Layout.minimumWidth: (stateLabel.visible ? stateLabel.Layout.minimumWidth : 0)
                                 + (stateDot.visible ? stateDot.implicitWidth + spacing : 0)
            Layout.leftMargin: Theme.spacingXs
            clip: true
            spacing: 6

            Rectangle {
                id: stateDot
                visible: header.showRunControls && header.status !== "stopped"
                implicitWidth: 6
                implicitHeight: 6
                radius: 3
                color: header.status === "running" ? Theme.positive
                     : header.status === "error" ? Theme.negative : Theme.caution
            }
            Text {
                id: stateLabel
                objectName: "paneState"
                visible: header.showRunControls
                Layout.preferredWidth: implicitWidth
                Layout.maximumWidth: implicitWidth
                Layout.minimumWidth: Math.min(implicitWidth, 40)
                text: header.effectiveStateText
                color: header.status === "error" ? Theme.negative
                     : header.status === "paused" ? Theme.caution : Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Text {
                id: detailLabel
                visible: header.showRunControls && !header.compact && header.effectiveDetail.length > 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: "·  " + header.effectiveDetail
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeBody
                font.features: Theme.tabularFigures
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Text {
                visible: header.showRunControls && header.timedOut
                text: header.timeoutText
                color: Theme.caution
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Medium
            }
            Item {
                visible: !detailLabel.visible
                Layout.fillWidth: true
            }
        }

        Text {
            id: noteLabel
            objectName: "paneNote"
            visible: header.note.length > 0
            Layout.minimumWidth: 0
            Layout.maximumWidth: implicitWidth
            Layout.fillWidth: true
            text: header.note
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        // Instrument items, then the run controls.
        Row {
            id: displayToolsRow
            spacing: Theme.spacingXs
            visible: children.length > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: implicitWidth   // never overlaps the state text
        }
        Row {
            id: actionsRow
            spacing: Theme.spacingXs
            visible: children.length > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: implicitWidth   // never overlaps the state text
        }
        Row {
            id: extrasRow
            spacing: Theme.spacingXs
            visible: children.length > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: implicitWidth   // never overlaps the state text
        }
        Row {
            id: legacyExtrasRow
            spacing: Theme.spacingXs
            visible: children.length > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: implicitWidth   // never overlaps the state text
        }

        Button {
            id: singleButton
            objectName: "singleButton"
            Layout.minimumWidth: implicitWidth
            visible: header.showSingle && header.showRunControls
            text: header.compact ? "" : header.singleText
            icon.source: Theme.icon(header.singleIcon)
            display: header.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            enabled: header.controller !== null && header.singleEnabled
                     && (header.controller.canRun
                         || (header.controller.state === InstrumentController.Running
                             && header.controller.runMode === InstrumentController.Continuous))
            focusPolicy: Qt.TabFocus
            onClicked: header.controller.runOnce()
            Accessible.name: header.singleText
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: header.singleToolTip + "   " + Theme.keys("Shift+Space")
        }

        // Run/Stop: one toggle of constant width. Accent "Run" when stopped (or "Resume" when
        // paused), neutral "Stop" while running.
        Button {
            id: runButton
            objectName: "runButton"
            readonly property bool stopMode: header.running
            readonly property string label: stopMode ? header.stopText : header.paused ? header.resumeText : header.runText
            visible: header.showRunControls
            highlighted: !stopMode
            text: label
            icon.source: Theme.icon(stopMode ? "stop-filled" : "play")
            enabled: header.controller !== null
                     && (stopMode ? header.controller.state !== InstrumentController.Stopping : header.controller.canRun)
            focusPolicy: Qt.TabFocus
            Layout.preferredWidth: Math.max(implicitWidth, runMetrics.width + leftPadding + rightPadding
                                            + icon.width + spacing + 2)
            Layout.minimumWidth: Layout.preferredWidth
            onClicked: header.controller.toggle()
            Accessible.name: label
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: label + "   " + Theme.keys("Space")

            TextMetrics {
                id: runMetrics
                font: runButton.font
                text: [header.runText, header.stopText, header.resumeText]
                      .reduce((a, b) => a.length >= b.length ? a : b)
            }
        }

        Button {
            id: releaseButton
            objectName: "releaseButton"
            Layout.minimumWidth: implicitWidth
            visible: header.showRelease
            text: header.compact ? "" : header.releaseText
            icon.source: Theme.icon("power")
            display: header.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            focusPolicy: Qt.TabFocus
            onClicked: header.controller.returnToSafeState()
            Accessible.name: header.releaseText
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: header.releaseToolTip
        }

        // In a narrow pane the content is clipped at its end, so ⋯ and the split's × stay
        // reachable (the state text shrinks first).
        Item {
            id: contentClip
            objectName: "paneHeaderContentClip"
            visible: header.hasContent && !header.contentWrapped
            clip: true
            implicitWidth: contentRow.implicitWidth
            implicitHeight: contentRow.implicitHeight
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true                    // shrinkable (a non-filling item is fixed)
            Layout.preferredWidth: implicitWidth
            Layout.maximumWidth: implicitWidth
            // Clipped only once the title is down to its minimum (then down to 120).
            Layout.minimumWidth: Math.min(implicitWidth,
                                          Math.max(120, implicitWidth - Math.max(0, header.rowDeficit - header.titleSlack)))
            Layout.preferredHeight: implicitHeight

            Row {
                id: contentRow
                objectName: "paneHeaderContent"
                parent: header.contentWrapped ? wrapSlot : contentClip
                spacing: Theme.spacingS
                visible: children.length > 0
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        ToolButton {
            id: inspectorButton
            objectName: "inspectorToggle"
            visible: header.showInspectorToggle
            icon.source: Theme.icon("sidebar-right")
            checkable: true
            checked: header.inspectorShown
            onClicked: header.inspectorToggleRequested()
            Accessible.name: header.inspectorShown ? qsTr("Hide Inspector") : qsTr("Show Inspector")
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: Accessible.name + "   " + Theme.keys("Ctrl+I")
        }

        ToolButton {
            id: resetButton
            objectName: "resetButton"
            visible: header.controller !== null && !header.narrow
            icon.source: Theme.icon("reset")
            onClicked: header.resetRequested()
            Accessible.name: qsTr("Reset %1 to Defaults…").arg(header.title)
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: Accessible.name
        }

        ToolButton {
            id: moreButton
            objectName: "moreButton"
            icon.source: Theme.icon("more-horizontal")
            down: moreMenu.visible || pressed
            onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
            Accessible.name: qsTr("More")
            ToolTip.visible: hovered && !moreMenu.visible
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: qsTr("More")
        }

        ToolButton {
            id: closeButton
            objectName: "closeButton"
            visible: header.closable
            icon.source: Theme.icon("close")
            onClicked: header.closeRequested()
            Accessible.name: header.closeToolTip
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: header.closeToolTip
        }
    }

    // ⋯ menu: Export Data…, Export Image…, Print…, the instrument's commands and menuItems,
    // Trace Style…, Show Measurements, Reset Settings…, Help. Native on desktop runs, in the window
    // offscreen (screenshots, tests).
    property alias menu: moreMenu
    Menu {
        id: moreMenu
        objectName: "paneMenu"
        popupType: AppInfo.offscreen ? Popup.Item : Popup.Native

        MenuItem {
            text: qsTr("Export Data…")
            enabled: header.showExport && header.controller !== null && header.controller.hasData
            onTriggered: header.exportDataRequested()
        }
        MenuItem {
            text: qsTr("Export Image…")
            enabled: header.showExport
            onTriggered: header.exportImageRequested()
        }
        MenuItem {
            text: qsTr("Print…")
            enabled: header.showExport
            onTriggered: header.printRequested()
        }
        MenuSeparator { id: commandsStart }
        MenuItem {
            id: resetItem
            text: qsTr("Reset Settings…")
            enabled: header.controller !== null
            onTriggered: header.resetRequested()
        }
        MenuItem {
            text: qsTr("Help")
            onTriggered: header.helpRequested()
        }
    }

    // Dynamic ⋯ entries go between `commandsStart` and `resetItem`, followed by a separator while
    // there are any (native menus ignore `visible`, so entries are inserted and removed).
    property MenuSeparator dynamicEnd: MenuSeparator {}
    function menuIndexOf(item: QtObject): int {
        for (let i = 0; i < moreMenu.count; ++i) {
            if (moreMenu.itemAt(i) === item)
                return i
        }
        return -1
    }
    function dynamicInsertIndex(): int {
        const end = menuIndexOf(dynamicEnd)
        return end >= 0 ? end : menuIndexOf(resetItem)
    }
    function insertMenuEntry(index: int, entry: QtObject) {
        if (entry instanceof Action)
            moreMenu.insertAction(index, entry as Action)
        else if (entry instanceof Menu)
            moreMenu.insertMenu(index, entry as Menu)
        else if (entry instanceof Item)
            moreMenu.insertItem(index, entry as Item)
        updateDynamicEnd()
    }
    function removeMenuEntry(entry: QtObject) {
        if (entry instanceof Item)
            moreMenu.removeItem(entry as Item)
        updateDynamicEnd()
    }
    function updateDynamicEnd() {
        const end = menuIndexOf(dynamicEnd)
        const present = end >= 0
        const wanted = (present ? end : menuIndexOf(resetItem)) - menuIndexOf(commandsStart) - 1 > 0
        if (wanted && !present)
            moreMenu.insertItem(menuIndexOf(resetItem), dynamicEnd)
        else if (!wanted && present)
            moreMenu.takeItem(menuIndexOf(dynamicEnd))
    }
    // Commands of the controller (InstrumentController.commands) that need no argument.
    Instantiator {
        id: commandItems
        model: header.controller ? header.controller.commands.filter(c => !c.argumentLabel) : []
        delegate: MenuItem {
            required property var modelData
            text: modelData.title
            enabled: modelData.enabled !== false
            onTriggered: header.controller.invokeCommand(modelData.id)
        }
        onObjectAdded: (index, object) => header.insertMenuEntry(header.menuIndexOf(commandsStart) + 1 + index, object)
        onObjectRemoved: (index, object) => header.removeMenuEntry(object)
    }
    Instantiator {
        id: optionalItems
        model: (header.hasTraceStyles ? ["trace"] : []).concat(header.measurementsAvailable ? ["measurements"] : [])
        delegate: MenuItem {
            required property string modelData
            text: modelData === "trace" ? qsTr("Trace Style…") : qsTr("Show Measurements")
            checkable: modelData === "measurements"
            checked: modelData === "measurements" && header.measurementsShown
            onTriggered: modelData === "trace" ? header.traceStyleRequested() : header.measurementsToggled()
        }
        onObjectAdded: (index, object) => header.insertMenuEntry(header.dynamicInsertIndex(), object)
        onObjectRemoved: (index, object) => header.removeMenuEntry(object)
    }
    Component.onCompleted: {
        for (const entry of menuItems)
            insertMenuEntry(dynamicInsertIndex(), entry)
        updateDynamicEnd()
    }
}
