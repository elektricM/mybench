import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls

// PaneHeader and the InstrumentPanel v2 frame (docs/UI.md §7, §13.5, audit U8): one Run/Stop
// toggle (accent Run, neutral Stop), Single, Release Outputs only while outputs are held and the
// instrument is stopped, the ⋯ menu, and the v1 option names panels still use.
Item {
    id: root
    width: 1200
    height: 700

    Component {
        id: headerComponent
        PaneHeader {
            width: 900
            controller: InstrumentRegistry.controller("beta")
        }
    }
    Component {
        id: compatPanel
        InstrumentPanel {
            id: panel
            width: 1100
            height: 650
            controller: InstrumentRegistry.controller("gamma")
            displayTitle: "CH 0 · 5 ms/div"
            displayInfo: "Sample Rate 50 kS/s"
            runBar.showTimeout: true
            runBar.timeoutText: "No input"
            runBar.singleToolTip: "Take one reading"
            runBarControls: [ Button { objectName: "autoscale"; text: "Autoscale" } ]
            runBarActions: [ ToolButton { objectName: "logTool"; text: "Log" } ]
            displayTools: [ ToolButton { objectName: "viewTool"; text: "View" } ]
            traceStyles: [{ name: "CH 0", style: null, color: "red" }]
            measurements: [ Rectangle { objectName: "strip"; implicitHeight: 60; property string summaryText: "RMS 1 V" } ]
            banners: [ Banner { objectName: "ownBanner"; text: "Own banner"; visible: false } ]
            Rectangle { objectName: "displayContent"; anchors.fill: parent; color: "black" }
        }
    }

    // Round-2 layouts: the panel's own run controls in the header (headerContent), no header Run.
    Component {
        id: contentPanel
        InstrumentPanel {
            width: 1100
            height: 650
            controller: InstrumentRegistry.controller("gamma")
            showRunControls: false
            headerContent: [ Rectangle { objectName: "bigRun"; implicitWidth: 120; implicitHeight: 52 },
                             Rectangle { objectName: "knobField"; implicitWidth: 90; implicitHeight: 40 } ]
        }
    }

    function menuTexts(menu: Menu): var {
        const texts = []
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            texts.push(item instanceof MenuSeparator ? "-" : (item as MenuItem).text)
        }
        return texts
    }

    TestCase {
        name: "PaneHeader"
        when: windowShown

        function cleanup() {
            const beta = InstrumentRegistry.controller("beta")
            beta.stop()
            tryVerify(() => beta.idle)
        }

        function test_runStopToggle() {
            const header = createTemporaryObject(headerComponent, root)
            const run = findChild(header, "runButton")
            verify(run)
            compare(header.height, Theme.paneHeaderHeight)
            compare(run.text, "Run")
            verify(run.highlighted)                      // accent when stopped
            const widthStopped = run.width
            mouseClick(run)
            tryCompare(header.controller, "running", true)
            compare(run.text, "Stop")
            verify(!run.highlighted)                     // neutral while running
            compare(run.width, widthStopped)             // one toggle, constant width
            tryCompare(header, "status", "running")
            mouseClick(run)
            tryCompare(header.controller, "running", false)
            compare(run.text, "Run")
        }

        function test_singleAndRelease() {
            const header = createTemporaryObject(headerComponent, root)
            const single = findChild(header, "singleButton")
            const release = findChild(header, "releaseButton")
            verify(single.visible)                       // supportsSingle
            verify(single.enabled)
            verify(!release.visible)                     // no outputs held
            header.showRelease = true
            verify(release.visible)
            compare(release.text, "Release Outputs")
            header.width = 500                           // compact: icon only
            verify(header.compact)
            compare(single.text, "")
        }

        function test_stateText() {
            const header = createTemporaryObject(headerComponent, root)
            const state = findChild(header, "paneState")
            verify(["Ready", "Stopped", "No device"].indexOf(state.text) >= 0, state.text)
            header.stateText = "Waiting for trigger"
            compare(state.text, "Waiting for trigger")
            header.stateText = ""
            header.controller.run()
            tryCompare(state, "text", "Running")
        }

        function test_menu() {
            const header = createTemporaryObject(headerComponent, root)
            compare(header.menu.popupType, Popup.Item)   // offscreen rule (docs/UI.md §13.9)
            compare(root.menuTexts(header.menu),
                    ["Export Data…", "Export Image…", "Print…", "-", "Reset Settings…", "Help"])
            header.hasTraceStyles = true
            compare(root.menuTexts(header.menu),
                    ["Export Data…", "Export Image…", "Print…", "-", "Trace Style…", "-", "Reset Settings…", "Help"])
            header.hasTraceStyles = false
            compare(root.menuTexts(header.menu),
                    ["Export Data…", "Export Image…", "Print…", "-", "Reset Settings…", "Help"])
            const spy = createTemporaryObject(spyComponent, root, { target: header, signalName: "helpRequested" })
            header.menu.itemAt(header.menu.count - 1).triggered()
            compare(spy.count, 1)
        }

        // The shell's split pane: × = closeRequested() (Close Split; never stops), and a note.
        function test_closableAndNote() {
            const header = createTemporaryObject(headerComponent, root)
            const close = findChild(header, "closeButton")
            const note = findChild(header, "paneNote")
            verify(close)
            verify(note)
            verify(!close.visible)
            verify(!note.visible)
            header.closable = true
            header.note = "Split hidden — window too narrow"
            verify(close.visible)
            verify(note.visible)
            compare(note.text, "Split hidden — window too narrow")
            header.controller.run()
            tryCompare(header.controller, "running", true)
            const spy = createTemporaryObject(spyComponent, root, { target: header, signalName: "closeRequested" })
            mouseClick(close)
            compare(spy.count, 1)
            compare(header.controller.running, true)
            // InstrumentPanel passes both through and forwards the signal.
            const panel = createTemporaryObject(compatPanel, root)
            verify(!panel.paneHeader.closable)
            panel.closable = true
            panel.note = "Note"
            verify(panel.paneHeader.closable)
            compare(panel.paneHeader.note, "Note")
            const panelSpy = createTemporaryObject(spyComponent, root, { target: panel, signalName: "closeRequested" })
            mouseClick(findChild(panel.paneHeader, "closeButton"))
            compare(panelSpy.count, 1)
        }

        // A confirmation carries its verb: native only for a standard button label.
        function test_contentAndRunControls() {
            const header = createTemporaryObject(headerComponent, root)
            const run = findChild(header, "runButton")
            const single = findChild(header, "singleButton")
            const content = findChild(header, "paneHeaderContent")
            verify(run.visible)
            verify(single.visible)
            verify(!content.visible)                                    // empty: no gap
            compare(header.height, Theme.paneHeaderHeight)

            verify(findChild(header, "paneState").visible)
            header.showRunControls = false
            verify(!run.visible)
            verify(!single.visible)

            const panel = createTemporaryObject(contentPanel, root)
            const panelHeader = panel.paneHeader
            verify(!findChild(panelHeader, "runButton").visible)
            verify(!findChild(panelHeader, "singleButton").visible)
            const big = findChild(panelHeader, "bigRun")
            const knob = findChild(panelHeader, "knobField")
            verify(big && knob)
            compare(big.parent.objectName, "paneHeaderContent")
            // The header grows to fit the 52 px item; the items sit between Run and ⋯, centred.
            compare(panelHeader.height, 52 + 2 * Theme.spacingXs + 1)
            const more = findChild(panelHeader, "moreButton")
            const bigPos = big.mapToItem(panelHeader, 0, 0)
            const knobPos = knob.mapToItem(panelHeader, 0, 0)
            verify(knobPos.x > bigPos.x)
            verify(knobPos.x + knob.width <= more.mapToItem(panelHeader, 0, 0).x)
            fuzzyCompare(bigPos.y + big.height / 2, (panelHeader.height - 1) / 2, 1)
            // A narrow pane: the content is clipped at its end, ⋯ stays inside the header and
            // nothing overlaps the state text.
            panel.width = 330
            waitForRendering(panel)
            const clipItem = big.parent.parent
            verify(clipItem.clip)
            verify(clipItem.width < clipItem.implicitWidth, clipItem.width + "/" + clipItem.implicitWidth)
            verify(more.mapToItem(panelHeader, 0, 0).x + more.width <= panelHeader.width, more.mapToItem(panelHeader, 0, 0).x + "+" + more.width + " in " + panelHeader.width + " clip " + clipItem.width)
            // The panel's own run controls carry the state: no state text in the header.
            verify(!findChild(panelHeader, "paneState").visible)
            verify(findChild(header, "paneState").visible === false)    // showRunControls false above
        }

        function test_confirmVerbs() {
            const dialog = createTemporaryObject(confirmComponent, root, { acceptText: "Reset" })
            verify(dialog.acceptButton >= 0)
            dialog.acceptText = "Reset Settings"
            compare(dialog.acceptButton, -1)
            dialog.acceptText = "Quit"
            compare(dialog.acceptButton, -1)
            verify(dialog.inWindow)
            dialog.destructive = true
            dialog.open()
            let styled = null
            tryVerify(() => (styled = findChild(dialog, "confirmDialog")) !== null && dialog.opened)
            const accept = findChild(styled.footer, "confirmAcceptButton")
            compare(accept.text, "Quit")
            verify(!accept.highlighted)                     // destructive: not the accent primary
            const spy = createTemporaryObject(spyComponent, root, { target: dialog, signalName: "accepted" })
            tryVerify(() => accept.activeFocus)             // the verb is the default button
            keyClick(Qt.Key_Return)
            compare(spy.count, 1)
            tryCompare(styled, "visible", false)
        }

        // The 9 v1 panels build unchanged through the aliases.
        function test_v1Aliases() {
            const panel = createTemporaryObject(compatPanel, root)
            const header = panel.runBar
            compare(header, panel.paneHeader)
            verify(header.showTimeout)
            compare(header.timeoutText, "No input")
            compare(header.singleToolTip, "Take one reading")
            compare(header.detailText, "Sample Rate 50 kS/s")    // displayInfo → state detail
            compare(panel.displayArea.summary, "CH 0 · 5 ms/div") // displayTitle → display summary
            for (const name of ["autoscale", "logTool", "viewTool"]) {
                const item = findChild(header, name)
                verify(item, name)
                verify(item.visible, name)
            }
            verify(findChild(panel.displayArea, "displayContent"))
            verify(header.hasTraceStyles)
            verify(header.measurementsAvailable)
            verify(panel.measurementsVisible)
            panel.setMeasurementsVisible(false)
            verify(!panel.measurementsVisible)
            panel.setMeasurementsVisible(true)
            verify(findChild(panel, "ownBanner"))
            compare(panel.paneId, "gamma")
            compare(panel.paneTitle, "gamma")
        }

        // The header's reset button (before ⋯) asks "Reset" first, like the ⋯ entry.
        function test_resetButtonConfirms() {
            const panel = createTemporaryObject(compatPanel, root)
            const button = findChild(panel.paneHeader, "resetButton")
            verify(button)
            verify(button.visible)
            compare(button.Accessible.name, "Reset gamma to Defaults…")
            const more = findChild(panel.paneHeader, "moreButton")
            verify(button.mapToItem(panel.paneHeader, 0, 0).x < more.mapToItem(panel.paneHeader, 0, 0).x)
            mouseClick(button)
            let dialog = null
            tryVerify(() => (dialog = findChild(panel, "confirmDialog")) !== null && dialog.opened)
            compare(findChild(dialog.footer, "confirmAcceptButton").text, "Reset")
            mouseClick(findChild(dialog.footer, "confirmCancelButton"))
            tryCompare(dialog, "visible", false)
        }

        // Reset Settings… asks first (ConfirmDialog, in the window offscreen), then resets.
        function test_resetSettingsConfirms() {
            const panel = createTemporaryObject(compatPanel, root)
            panel.requestResetSettings()
            let dialog = null
            tryVerify(() => (dialog = findChild(panel, "confirmDialog")) !== null && dialog.opened)
            compare(dialog.title, "Reset gamma settings?")
            const accept = findChild(dialog.footer, "confirmAcceptButton")
            verify(accept)
            compare(accept.text, "Reset")
            mouseClick(accept)
            tryCompare(dialog, "visible", false)
            panel.requestResetSettings()
            tryVerify(() => dialog.opened)
            mouseClick(findChild(dialog.footer, "confirmCancelButton"))
            tryCompare(dialog, "visible", false)
        }
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }

    Component {
        id: confirmComponent
        ConfirmDialog { title: "Quit?"; text: "Two instruments run." }
    }

    Component {
        id: deviceButtonComponent
        DeviceButton {}
    }

    // DeviceMenu / DeviceButton (docs/UI.md §7 device button, §9 Device menu).
    TestCase {
        name: "DeviceButton"
        when: windowShown

        function test_menuAndLabel() {
            const button = createTemporaryObject(deviceButtonComponent, root)
            verify(DeviceHub.connected)
            verify(DeviceHub.simulated)
            compare(button.label, "Simulated myDAQ")
            const texts = root.menuTexts(button.menu)
            verify(texts.indexOf("Rescan") > 0, texts)
            verify(texts.indexOf("Reset Device…") > 0, texts)
            verify(texts.indexOf("Device Info…") > 0, texts)
            compare(texts[0], DeviceHub.name)                           // the devices come first
            verify((button.menu.itemAt(0) as MenuItem).checked)         // the current one
            if (DeviceHub.simulatorAvailable) {
                verify(texts.indexOf("Simulated Device") > 0, texts)
                compare(texts[texts.length - 1], "Simulator Settings…")
            }
            const spy = createTemporaryObject(spyComponent, root, { target: button.menu, signalName: "resetRequested" })
            const reset = button.menu.itemAt(texts.indexOf("Reset Device…")) as MenuItem
            verify(reset.enabled)
            reset.triggered()
            compare(spy.count, 1)
            button.requestSelect(DeviceHub.currentId)                   // same device: nothing
            compare(DeviceHub.connected, true)
        }
    }
}
