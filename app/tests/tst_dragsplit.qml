import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// Drag and drop to split (docs/UI.md §8): a sidebar row or Overview tile dropped on the left or
// right third of the workspace splits with it there, on the middle it replaces the focused pane;
// a pane header dropped on the other half swaps, on the sidebar it closes that pane. Nothing stops.
Item {
    id: host
    width: 1300
    height: 700

    AppPreferences { id: prefs }
    WorkspaceState { id: ws }
    ShellDialogs {
        id: dialogs
        preferences: prefs
        workspace: ws
    }
    AppActions {
        id: actions
        workspace: ws
        dialogs: dialogs
        preferences: prefs
    }
    Sidebar {
        id: sidebar
        width: Theme.sidebarWidth
        height: parent.height
        workspace: ws
        dialogs: dialogs
    }
    Workspace {
        id: workspace
        x: sidebar.width
        width: parent.width - sidebar.width
        height: parent.height
        workspace: ws
        actions: actions
    }

    // --- WorkspaceState.dropPane / dropOnSidebar ---------------------------------------------------
    TestCase {
        name: "DropState"

        function init() {
            ws.fromMap({})
        }
        function layoutOf(): string {
            return ws.overview ? "overview" : ws.selected + "|" + ws.beside + " @" + ws.focused
        }

        function test_dropPane_data() {
            return [
                { tag: "overview right shows", start: [], pane: "bb", zone: "right", fromPane: false, expect: "bb| @bb" },
                { tag: "single, right splits", start: ["aa"], pane: "bb", zone: "right", fromPane: false, expect: "aa|bb @bb" },
                { tag: "single, left splits", start: ["aa"], pane: "bb", zone: "left", fromPane: false, expect: "bb|aa @bb" },
                { tag: "single, center replaces", start: ["aa"], pane: "bb", zone: "center", fromPane: false, expect: "bb| @bb" },
                { tag: "single, itself", start: ["aa"], pane: "aa", zone: "right", fromPane: false, expect: "aa| @aa" },
                { tag: "split, right replaces right", start: ["aa", "bb"], pane: "cc", zone: "right", fromPane: false, expect: "aa|cc @cc" },
                { tag: "split, left replaces left", start: ["aa", "bb"], pane: "cc", zone: "left", fromPane: false, expect: "cc|bb @cc" },
                { tag: "split, center replaces focused", start: ["aa", "bb"], pane: "cc", zone: "center", fromPane: false, expect: "aa|cc @cc" },
                { tag: "split, left pane to right swaps", start: ["aa", "bb"], pane: "aa", zone: "right", fromPane: false, expect: "bb|aa @aa" },
                { tag: "split, view to right", start: ["aa"], pane: "cc/editor", zone: "right", fromPane: false, expect: "aa|cc/editor @cc/editor" },
                { tag: "header to other half swaps", start: ["aa", "bb"], pane: "aa", zone: "right", fromPane: true, expect: "bb|aa @bb" },
                { tag: "header to own half", start: ["aa", "bb"], pane: "aa", zone: "left", fromPane: true, expect: "aa|bb @bb" },
                { tag: "header to center", start: ["aa", "bb"], pane: "bb", zone: "center", fromPane: true, expect: "aa|bb @bb" },
                { tag: "unknown pane", start: ["aa"], pane: "zz", zone: "right", fromPane: false, expect: "aa| @aa" }
            ]
        }
        function test_dropPane(data) {
            if (data.start.length > 0)
                ws.show(data.start[0])
            if (data.start.length > 1)
                ws.showBeside(data.start[1])
            ws.dropPane(data.pane, data.zone, data.fromPane)
            compare(layoutOf(), data.expect)
        }

        function test_dropOnSidebar() {
            ws.show("aa")
            ws.showBeside("bb")
            ws.dropOnSidebar("aa")
            compare(layoutOf(), "bb| @bb")
            ws.dropOnSidebar("cc")            // not shown: nothing
            compare(layoutOf(), "bb| @bb")
            ws.dropOnSidebar("bb")            // the only pane: back to the Overview
            compare(layoutOf(), "overview")
        }

        function test_dropNeverStops() {
            const aa = InstrumentRegistry.controller("aa")
            const bb = InstrumentRegistry.controller("bb")
            ws.show("aa")
            aa.run()
            bb.run()
            tryCompare(aa, "running", true)
            tryCompare(bb, "running", true)
            ws.dropPane("bb", "center", false)
            ws.dropPane("aa", "left", false)
            ws.dropPane("aa", "right", true)
            ws.dropOnSidebar("aa")
            ws.dropOnSidebar("bb")
            verify(aa.running)
            verify(bb.running)
            aa.stop()
            bb.stop()
            tryCompare(aa, "running", false)
            tryCompare(bb, "running", false)
        }
    }

    // --- The real drag images and drop areas -------------------------------------------------------
    TestCase {
        name: "DragSplit"
        when: windowShown

        function init() {
            ws.fromMap({})
            ws.show("aa")
            waitForRendering(host)
        }
        // The drag image of `paneId` with `objectName` (the chips live in the window's content item).
        function chipOf(objectName: string, paneId: string): DragChip {
            const children = host.Window.window.contentItem.children
            for (let i = 0; i < children.length; ++i) {
                const chip = children[i] as DragChip
                if (chip && chip.objectName === objectName && chip.paneId === paneId)
                    return chip
            }
            return null
        }
        // Drags `chip` to (x, y) of `target` and drops it there.
        function dragTo(chip: DragChip, target: Item, x: real, y: real) {
            chip.follow(target.mapToItem(null, x, y))
            chip.Drag.active = true
            chip.Drag.drop()
            chip.Drag.active = false
        }

        function test_rowChipsExist() {
            verify(chipOf("rowDragChip", "bb") !== null)
            verify(chipOf("rowDragChip", "cc/editor") !== null)
            verify(chipOf("paneDragChip", "aa") !== null)
        }

        function test_rowOnRightThirdSplits() {
            dragTo(chipOf("rowDragChip", "bb"), workspace, workspace.width * 0.9, 200)
            compare(ws.selected, "aa")
            compare(ws.beside, "bb")
            compare(ws.focused, "bb")
        }
        function test_rowOnLeftThirdSplits() {
            dragTo(chipOf("rowDragChip", "bb"), workspace, workspace.width * 0.1, 200)
            compare(ws.selected, "bb")
            compare(ws.beside, "aa")
        }
        function test_rowOnCenterReplacesFocused() {
            ws.showBeside("bb")
            ws.focus("bb")
            dragTo(chipOf("rowDragChip", "cc"), workspace, workspace.width / 2, 200)
            compare(ws.selected, "aa")
            compare(ws.beside, "cc")
            compare(ws.focused, "cc")
        }
        function test_rowOnOverviewShows() {
            ws.showOverview()
            dragTo(chipOf("rowDragChip", "cc"), workspace, workspace.width * 0.9, 200)
            compare(ws.selected, "cc")
            verify(!ws.split)
        }
        function test_dropOutsideDoesNothing() {
            dragTo(chipOf("rowDragChip", "bb"), sidebar, 20, 200)   // a row is not closed by the sidebar
            compare(ws.selected, "aa")
            verify(!ws.split)
        }

        function test_previewFollowsTheZone() {
            const chip = chipOf("rowDragChip", "bb")
            const preview = findChild(workspace, "dropPreview")
            verify(preview !== null)
            chip.follow(workspace.mapToItem(null, workspace.width * 0.9, 200))
            chip.Drag.active = true
            tryVerify(() => preview.visible)
            compare(preview.x, workspace.width / 2)
            compare(preview.width, workspace.width / 2)
            const shots = AppInfo.value("shot-dir")
            if (shots.length > 0)
                grabImage(host).save(shots + "/drag-preview.png")
            chip.follow(workspace.mapToItem(null, workspace.width / 2, 200))
            tryCompare(preview, "x", 0)
            compare(preview.width, workspace.width)
            chip.Drag.cancel()
            chip.Drag.active = false
            tryVerify(() => !preview.visible)
            compare(ws.beside, "")
        }

        function test_headerOnOtherHalfSwaps() {
            ws.showBeside("bb")
            dragTo(chipOf("paneDragChip", "aa"), workspace, workspace.width * 0.9, 200)
            compare(ws.selected, "bb")
            compare(ws.beside, "aa")
        }
        function test_headerOnOwnHalfDoesNothing() {
            ws.showBeside("bb")
            dragTo(chipOf("paneDragChip", "aa"), workspace, workspace.width * 0.1, 200)
            compare(ws.selected, "aa")
            compare(ws.beside, "bb")
        }
        function test_headerOnSidebarCloses() {
            ws.showBeside("bb")
            const bb = InstrumentRegistry.controller("bb")
            bb.run()
            tryCompare(bb, "running", true)
            dragTo(chipOf("paneDragChip", "bb"), sidebar, 20, 200)
            compare(ws.selected, "aa")
            verify(!ws.split)
            verify(bb.running)                  // closing a pane never stops its instrument
            bb.stop()
            tryCompare(bb, "running", false)
        }

        // A real pointer drag of a sidebar row: the DragHandler starts the drag image, the release
        // drops it on the right third.
        function test_mouseDragFromRow() {
            const row = findChild(sidebar, "sidebarRow-bb")
            verify(row !== null)
            const from = row.mapToItem(host, 40, row.height / 2)
            const to = workspace.mapToItem(host, workspace.width * 0.85, 300)
            mousePress(host, from.x, from.y)
            const steps = 12
            for (let i = 1; i <= steps; ++i) {
                mouseMove(host, from.x + (to.x - from.x) * i / steps, from.y + (to.y - from.y) * i / steps, -1, Qt.LeftButton)
                wait(5)
            }
            mouseRelease(host, to.x, to.y)
            tryCompare(ws, "beside", "bb")
            compare(ws.selected, "aa")
            tryCompare(row.dragChip, "visible", false)      // no drag image left behind
        }
        // Released where nothing accepts it (the sidebar): nothing changes, the image goes away.
        function test_mouseDragDroppedNowhere() {
            const row = findChild(sidebar, "sidebarRow-bb")
            const from = row.mapToItem(host, 40, row.height / 2)
            mousePress(host, from.x, from.y)
            for (let i = 1; i <= 8; ++i) {
                mouseMove(host, from.x + 4 * i, from.y + 12 * i, -1, Qt.LeftButton)
                wait(5)
            }
            verify(row.dragChip.visible)
            mouseRelease(host, from.x + 32, from.y + 96)
            tryCompare(row.dragChip, "visible", false)
            compare(ws.selected, "aa")
            verify(!ws.split)
        }
    }
}
