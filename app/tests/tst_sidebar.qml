import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// The sidebar (docs/UI.md §7): Overview, then the instruments by group with their views as child
// rows; a row has the state dot and the key value; click shows, ⌘/Ctrl-click shows beside; the
// context menu; keys ↑/↓, Return, Space (the sidebar's Space is not also the window's Run/Stop).
Item {
    id: host
    width: 400
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

    function menuTexts(menu: Menu): var {
        const texts = []
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            texts.push(item instanceof MenuSeparator ? "-" : (item as MenuItem).text)
        }
        return texts
    }
    function menuItem(menu: Menu, text: string): MenuItem {
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i) as MenuItem
            if (item && item.text === text)
                return item
        }
        return null
    }

    TestCase {
        name: "Sidebar"
        when: windowShown

        function init() {
            ws.fromMap({})
            sidebar.compact = false
            sidebar.width = Theme.sidebarWidth
            for (const id of ["aa", "bb", "cc"]) {
                const c = InstrumentRegistry.controller(id)
                c.stop()
                c.setKey("")
                tryCompare(c, "keyValue", "")              // coalesced to ≤ 4 Hz
            }
            tryVerify(() => !InstrumentRegistry.anyRunning)
        }

        function row(paneId: string): SidebarRow {
            return findChild(sidebar, "sidebarRow-" + paneId) as SidebarRow
        }

        function test_entriesByGroup() {
            const kinds = sidebar.entries.map(e => e.kind + ":" + (e.paneId || e.title))
            compare(kinds, ["overview:Overview", "group:Measure", "instrument:aa", "instrument:bb",
                            "group:Generate", "instrument:cc", "view:cc/editor"])
            verify(row("overview"))
            verify(row("aa"))
            verify(row("cc/editor").child)
            verify(row("overview").selected)               // Overview at start
        }

        function test_clickShowsAndModifierShowsBeside() {
            mouseClick(row("bb"))
            compare(ws.selected, "bb")
            compare(ws.focused, "bb")
            verify(row("bb").selected)
            verify(!row("overview").selected)
            mouseClick(row("cc"), 10, 10, Qt.LeftButton, Qt.ControlModifier)
            compare(ws.selected, "bb")
            compare(ws.beside, "cc")
            verify(row("cc").selected)                     // focused pane
            verify(row("bb").shownBeside)                  // the other pane
            mouseClick(row("overview"))
            verify(ws.overview)
        }

        function test_stateDotAndKeyValue() {
            const aa = InstrumentRegistry.controller("aa")
            const r = row("aa")
            compare(r.status, "")
            const height = r.height
            aa.setKey("1.234 V")
            tryCompare(r, "keyValue", "1.234 V")
            verify(r.height > height)                      // the second line
            aa.run()
            tryCompare(r, "status", "running")
            compare(r.statusColor, Theme.positive)
            aa.stop()
            tryCompare(r, "status", "")
        }

        function test_contextMenu() {
            const menu = row("aa").contextMenu
            verify(menu)
            compare(host.menuTexts(menu), ["Show", "Show Beside", "-", "Run", "Single", "-", "Reset Settings…", "Help"])
            host.menuItem(menu, "Show").triggered()
            compare(ws.selected, "aa")
            host.menuItem(menu, "Run").triggered()
            const aa = InstrumentRegistry.controller("aa")
            tryCompare(aa, "running", true)
            compare(host.menuItem(menu, "Stop").text, "Stop")
            host.menuItem(menu, "Stop").triggered()
            tryCompare(aa, "running", false)
            host.menuItem(row("bb").contextMenu, "Show Beside").triggered()
            compare(ws.beside, "bb")
            host.menuItem(menu, "Help").triggered()
            compare(ws.dockPage, "help")
            compare(ws.helpTopic, "aa")
            // A view row: Show, Show Beside, Help.
            compare(host.menuTexts(row("cc/editor").contextMenu), ["Show", "Show Beside", "-", "Help"])
            verify(row("overview").contextMenu === null)
        }

        function test_keyboard() {
            const list = findChild(sidebar, "sidebarList") as ListView
            list.forceActiveFocus()
            compare(list.currentIndex, 0)
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 2)                  // the group label is skipped
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(ws.selected, "bb")
            // Space runs/stops the keyboard row once (the window's Space shortcut stays out).
            const bb = InstrumentRegistry.controller("bb")
            keyClick(Qt.Key_Space)
            tryCompare(bb, "running", true)
            wait(50)
            compare(bb.running, true)
            keyClick(Qt.Key_Space)
            tryCompare(bb, "running", false)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(list.currentIndex, 0)
            keyClick(Qt.Key_Return)
            verify(ws.overview)
        }

        function test_compactRows() {
            sidebar.compact = true
            sidebar.width = Theme.sidebarCompactWidth
            const r = row("aa")
            compare(r.width, Theme.sidebarCompactWidth)
            InstrumentRegistry.controller("aa").setKey("2 V")
            tryVerify(() => r.ToolTip.text.indexOf("AA Instrument") === 0)
            tryVerify(() => r.ToolTip.text.indexOf("2 V") > 0)
            verify(r.ToolTip.text.indexOf("Stopped") > 0)
        }
    }
}
