import QtQuick
import MyBench.Framework

// Layout of the workspace: tabs (each one instrument, or two side by side), the current tab
// (-1 = Home), the focused instrument (Space, help), the pinned dashboard column and the help
// drawer. Serialized into SessionManager.workspace (sessions + auto-saved layout).
QtObject {
    id: state

    // [{ primary: "scope", secondary: "gen" | "" , ratio: 0.5 }]
    property var tabs: []
    property int currentIndex: -1
    property string focused: ""
    property bool dashboardPinned: false
    // Instrument tiles of the dashboard column, top to bottom (app.md 2.4; default DMM + DIO).
    readonly property var defaultDashboardTiles: ["dmm", "dio"]
    property var dashboardTiles: defaultDashboardTiles
    property bool helpOpen: false
    property string helpTopic: "app"
    // Instruments whose panel was shown at least once (panels are created lazily, then kept).
    property var opened: ({})

    readonly property var currentTab: currentIndex >= 0 && currentIndex < tabs.length ? tabs[currentIndex] : null
    readonly property bool home: currentTab === null
    readonly property bool split: currentTab !== null && currentTab.secondary.length > 0

    signal changed()

    function indexOf(id: string): int {
        for (let i = 0; i < tabs.length; ++i)
            if (tabs[i].primary === id || tabs[i].secondary === id)
                return i
        return -1
    }
    function isVisible(id: string): bool {
        return currentTab !== null && (currentTab.primary === id || currentTab.secondary === id)
    }
    function markOpened(id: string) {
        if (opened[id])
            return
        const copy = Object.assign({}, opened)
        copy[id] = true
        opened = copy
    }
    function open(id: string) {
        if (!InstrumentRegistry.contains(id))
            return
        let i = indexOf(id)
        if (i < 0) {
            tabs = tabs.concat([{ primary: id, secondary: "", ratio: 0.5 }])
            i = tabs.length - 1
        }
        markOpened(id)
        currentIndex = i
        focused = id
        changed()
    }
    // Shows `id` beside the current tab's instrument (moves it out of its own tab).
    function openBeside(id: string) {
        if (!InstrumentRegistry.contains(id))
            return
        if (currentTab === null) {
            open(id)
            return
        }
        if (currentTab.primary === id || currentTab.secondary === id)
            return
        // Already split: the right pane's instrument moves to its own tab (never dropped).
        if (currentTab.secondary.length > 0) {
            openInPane(id, "right")
            return
        }
        const primary = currentTab.primary
        let list = removed(id)
        const index = list.findIndex(t => t.primary === primary || t.secondary === primary)
        list[index] = { primary: primary, secondary: id, ratio: list[index].ratio ?? 0.5 }
        tabs = list
        currentIndex = index
        markOpened(id)
        focused = id
        changed()
    }
    // Moves the secondary panel of tab `index` to its own tab.
    function unsplit(index: int) {
        const tab = tabs[index]
        if (!tab || tab.secondary.length === 0)
            return
        const list = tabs.slice()
        list[index] = { primary: tab.primary, secondary: "", ratio: tab.ratio }
        list.splice(index + 1, 0, { primary: tab.secondary, secondary: "", ratio: 0.5 })
        tabs = list
        focused = tab.primary
        changed()
    }
    function swap(index: int) {
        const tab = tabs[index]
        if (!tab || tab.secondary.length === 0)
            return
        const list = tabs.slice()
        list[index] = { primary: tab.secondary, secondary: tab.primary, ratio: 1 - (tab.ratio ?? 0.5) }
        tabs = list
        changed()
    }
    function setRatio(index: int, ratio: real) {
        const tab = tabs[index]
        if (!tab || Math.abs((tab.ratio ?? 0.5) - ratio) < 0.001)
            return
        const list = tabs.slice()
        list[index] = { primary: tab.primary, secondary: tab.secondary, ratio: ratio }
        tabs = list
        changed()
    }
    // Tabs without instrument `id` (a split tab keeps its other panel).
    function removed(id: string): var {
        const list = []
        for (const t of tabs) {
            if (t.primary === id && t.secondary.length > 0)
                list.push({ primary: t.secondary, secondary: "", ratio: 0.5 })
            else if (t.secondary === id)
                list.push({ primary: t.primary, secondary: "", ratio: t.ratio })
            else if (t.primary !== id)
                list.push(t)
        }
        return list
    }
    // Every instrument shown in `list` (tabs).
    function shownIds(list: var): var {
        const ids = []
        for (const t of list) {
            ids.push(t.primary)
            if (t.secondary.length > 0)
                ids.push(t.secondary)
        }
        return ids
    }
    // Tells the controllers whose last view disappeared (stop, outputs to their safe state).
    function notifyClosed(before: var) {
        const after = shownIds(tabs)
        for (const id of shownIds(before))
            if (after.indexOf(id) < 0)
                InstrumentRegistry.notifyPanelClosed(id)
    }
    function closeInstrument(id: string) {
        const current = currentTab
        const before = tabs
        tabs = removed(id)
        if (current === null)
            currentIndex = -1
        else
            currentIndex = Math.min(indexOf(current.primary === id ? current.secondary : current.primary), tabs.length - 1)
        if (focused === id)
            focused = currentTab ? currentTab.primary : ""
        notifyClosed(before)
        changed()
    }
    function closeTab(index: int) {
        if (index < 0 || index >= tabs.length)
            return
        const before = tabs
        const list = tabs.slice()
        list.splice(index, 1)
        tabs = list
        if (currentIndex >= tabs.length)
            currentIndex = tabs.length - 1
        else if (currentIndex > index)
            currentIndex -= 1
        focused = currentTab ? currentTab.primary : ""
        notifyClosed(before)
        changed()
    }
    function select(index: int) {
        currentIndex = Math.max(-1, Math.min(index, tabs.length - 1))
        if (currentTab) {
            markOpened(currentTab.primary)
            if (currentTab.secondary.length > 0)
                markOpened(currentTab.secondary)
            if (!isVisible(focused))
                focused = currentTab.primary
        }
        changed()
    }
    function showHome() {
        select(-1)
    }
    function toggleDashboard() {
        dashboardPinned = !dashboardPinned
        changed()
    }
    function addDashboardTile(id: string) {
        if (!InstrumentRegistry.contains(id) || dashboardTiles.indexOf(id) >= 0)
            return
        dashboardTiles = dashboardTiles.concat([id])
        changed()
    }
    function removeDashboardTile(id: string) {
        if (dashboardTiles.indexOf(id) < 0)
            return
        dashboardTiles = dashboardTiles.filter(t => t !== id)
        changed()
    }
    function moveDashboardTile(from: int, to: int) {
        if (from < 0 || to < 0 || from >= dashboardTiles.length || to >= dashboardTiles.length || from === to)
            return
        const list = dashboardTiles.slice()
        list.splice(to, 0, list.splice(from, 1)[0])
        dashboardTiles = list
        changed()
    }
    // Stored tiles without unknown/duplicate ids; the default when absent (older sessions).
    function validDashboardTiles(value: var): var {
        if (value === undefined || value === null || typeof value === "string" || typeof value.length !== "number")
            return defaultDashboardTiles.filter(id => InstrumentRegistry.contains(id))
        const list = []
        for (let i = 0; i < value.length; ++i) {
            const id = String(value[i])
            if (InstrumentRegistry.contains(id) && list.indexOf(id) < 0)
                list.push(id)
        }
        return list
    }
    function showHelp(topic: string) {
        helpTopic = topic.length > 0 ? topic : "app"
        helpOpen = true
        changed()
    }
    function toggleHelp() {
        if (!helpOpen)
            helpTopic = focused.length > 0 && !home ? (InstrumentRegistry.info(focused).helpId ?? "app") : "app"
        helpOpen = !helpOpen
        changed()
    }

    // --- Tab drag and "other pane" (app.md 2.2, 2.4) ---------------------------------------
    // Moves tab `from` to position `to` (tab strip drag); the current tab stays current.
    function moveTab(from: int, to: int) {
        if (from < 0 || from >= tabs.length)
            return
        to = Math.max(0, Math.min(to, tabs.length - 1))
        if (to === from)
            return
        const current = currentTab
        const list = tabs.slice()
        const moved = list.splice(from, 1)[0]
        list.splice(to, 0, moved)
        tabs = list
        if (current !== null)
            currentIndex = list.findIndex(t => t.primary === current.primary)
        changed()
    }
    // Shows `id` in pane `side` ("left" or "right") of the current tab, creating the split if
    // needed; the instrument shown there before moves to its own tab next to it.
    function openInPane(id: string, side: string) {
        if (!InstrumentRegistry.contains(id))
            return
        const cur = currentTab
        if (cur === null) {
            open(id)
            return
        }
        const right = side !== "left"
        const here = right ? cur.secondary : cur.primary
        const there = right ? cur.primary : cur.secondary
        if (here === id || (!right && cur.secondary.length === 0 && cur.primary === id)) {
            focused = id
            changed()
            return
        }
        if (there === id) {   // already in the other pane of this tab: swap sides
            if (cur.secondary.length > 0)
                swap(currentIndex)
            focused = id
            changed()
            return
        }
        const list = removed(id)   // the current tab does not show `id`: it stays as it is
        const index = list.findIndex(t => t.primary === cur.primary)
        if (index < 0)
            return
        let tab
        let displaced = ""
        if (cur.secondary.length === 0)   // single pane: split it
            tab = right ? { primary: cur.primary, secondary: id, ratio: 0.5 } : { primary: id, secondary: cur.primary, ratio: 0.5 }
        else {
            displaced = here
            tab = right ? { primary: cur.primary, secondary: id, ratio: cur.ratio ?? 0.5 }
                        : { primary: id, secondary: cur.secondary, ratio: cur.ratio ?? 0.5 }
        }
        list[index] = tab
        if (displaced.length > 0)
            list.splice(index + 1, 0, { primary: displaced, secondary: "", ratio: 0.5 })
        tabs = list
        currentIndex = index
        markOpened(id)
        focused = id
        changed()
    }
    // Ctrl/⌘+click in the nav rail: `id` in the pane that is not focused (right when unsplit).
    function openInOtherPane(id: string) {
        const cur = currentTab
        if (cur === null) {
            open(id)
            return
        }
        const focusedRight = cur.secondary.length > 0 && focused === cur.secondary
        openInPane(id, focusedRight ? "left" : "right")
    }

    function toMap(): var {
        return { version: 1, tabs: tabs, current: currentIndex, focused: focused,
                 dashboardPinned: dashboardPinned, dashboardTiles: dashboardTiles, helpOpen: helpOpen, helpTopic: helpTopic }
    }
    function fromMap(map: var) {
        const list = []
        for (const t of (map && map.tabs) ? map.tabs : []) {
            if (!t || !InstrumentRegistry.contains(t.primary))
                continue
            const secondary = InstrumentRegistry.contains(t.secondary ?? "") ? t.secondary : ""
            list.push({ primary: t.primary, secondary: secondary, ratio: Number(t.ratio ?? 0.5) })
        }
        tabs = list
        currentIndex = Math.max(-1, Math.min(Number(map && map.current !== undefined ? map.current : -1), tabs.length - 1))
        focused = map && map.focused ? String(map.focused) : ""
        dashboardPinned = !!(map && map.dashboardPinned)
        dashboardTiles = validDashboardTiles(map ? map.dashboardTiles : undefined)
        helpOpen = !!(map && map.helpOpen)
        helpTopic = map && map.helpTopic ? String(map.helpTopic) : "app"
        select(currentIndex)
    }
}
