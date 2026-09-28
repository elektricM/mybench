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
    function closeInstrument(id: string) {
        const current = currentTab
        tabs = removed(id)
        if (current === null)
            currentIndex = -1
        else
            currentIndex = Math.min(indexOf(current.primary === id ? current.secondary : current.primary), tabs.length - 1)
        if (focused === id)
            focused = currentTab ? currentTab.primary : ""
        changed()
    }
    function closeTab(index: int) {
        if (index < 0 || index >= tabs.length)
            return
        const list = tabs.slice()
        list.splice(index, 1)
        tabs = list
        if (currentIndex >= tabs.length)
            currentIndex = tabs.length - 1
        else if (currentIndex > index)
            currentIndex -= 1
        focused = currentTab ? currentTab.primary : ""
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

    function toMap(): var {
        return { version: 1, tabs: tabs, current: currentIndex, focused: focused,
                 dashboardPinned: dashboardPinned, helpOpen: helpOpen, helpTopic: helpTopic }
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
        helpOpen = !!(map && map.helpOpen)
        helpTopic = map && map.helpTopic ? String(map.helpTopic) : "app"
        select(currentIndex)
    }
}
