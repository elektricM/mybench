import QtQuick
import MyBench.Framework

// What the window shows (docs/UI.md §7, §8): Overview or one pane, optionally a second pane
// beside it, the focused pane, the sidebar and inspector toggles per layout class, and the page
// of the inspector dock. Serialized into SessionManager.workspace (sessions and the autosave);
// fromMap() also reads the v1 layout (tabs, dashboard, help drawer) of older sessions.
//
// Showing another instrument, closing the split or going to Overview never stops an instrument:
// stopping is always explicit (Run/Stop, Stop All, a context menu).
QtObject {
    id: state

    // "" = Overview; otherwise a pane id: an instrument ("scope") or a view ("gen/editor").
    property string selected: ""
    // The pane shown beside `selected` ("" = no split) and the width share of `selected`.
    property string beside: ""
    property real ratio: 0.5
    // The pane that gets the keyboard, the menu commands and the inspector ("" on Overview).
    property string focused: ""
    // User toggles per layout class ("wide", "medium", "narrow", "compact"); absent = default.
    property var sidebarCollapsed: ({})
    property var inspectorVisible: ({})
    // Page of the inspector dock: "inspector" (the focused pane's settings) or "help".
    property string dockPage: "inspector"
    property string helpTopic: "app"

    // Not saved: the window's current layout class (set by the shell) and the panes created so far
    // (panels are created on first show, then kept).
    property string layoutClass: "wide"
    property var opened: ({})

    readonly property bool overview: selected.length === 0
    readonly property bool split: !overview && beside.length > 0
    readonly property bool helpShown: dockPage === "help"
    readonly property bool sidebarShown: !sidebarHiddenIn(layoutClass)
    readonly property bool inspectorShown: inspectorShownIn(layoutClass)

    signal changed()

    // --- Pane ids --------------------------------------------------------------------------------
    function isPane(paneId: string): bool {
        if (paneId.indexOf("/") >= 0)
            return (InstrumentRegistry.viewInfo(paneId).id ?? "").length > 0
        return paneId.length > 0 && InstrumentRegistry.contains(paneId)
    }
    function instrumentOf(paneId: string): string {
        return paneId.split("/")[0]
    }
    // Every pane in sidebar order: each instrument followed by its views.
    function paneOrder(): var {
        const list = []
        for (const id of InstrumentRegistry.ids) {
            list.push(id)
            for (const view of (InstrumentRegistry.info(id).views ?? []))
                list.push(view.id)
        }
        return list
    }
    function isShown(paneId: string): bool {
        return paneId.length > 0 && (selected === paneId || (split && beside === paneId))
    }
    function helpTopicOf(paneId: string): string {
        if (paneId.length === 0)
            return "app"
        const id = instrumentOf(paneId)
        return InstrumentRegistry.info(id).helpId ?? id
    }
    function markOpened(paneId: string) {
        if (paneId.length === 0 || opened[paneId])
            return
        const copy = Object.assign({}, opened)
        copy[paneId] = true
        opened = copy
    }

    // --- Showing -----------------------------------------------------------------------------------
    // Shows `paneId` in the focused pane (or focuses it where it is already shown).
    function show(paneId: string) {
        if (!isPane(paneId))
            return
        markOpened(paneId)
        if (!isShown(paneId)) {
            if (split && focused === beside)
                beside = paneId
            else
                selected = paneId
        }
        focused = paneId
        changed()
    }
    // Shows `paneId` in a second pane: beside the focused one, or instead of the pane that is not
    // focused when already split. From Overview it is a plain show().
    function showBeside(paneId: string) {
        if (!isPane(paneId))
            return
        if (overview || isShown(paneId)) {
            show(paneId)
            return
        }
        markOpened(paneId)
        if (split && focused === beside)
            selected = paneId
        else
            beside = paneId
        focused = paneId
        changed()
    }
    // Ends the split and keeps the focused pane. The other instrument keeps running.
    function closeSplit() {
        if (!split)
            return
        if (focused === beside)
            selected = beside
        beside = ""
        focused = selected
        changed()
    }
    // Removes one pane of a split (the × of its header); keeps the other one.
    function closePane(paneId: string) {
        if (!split || !isShown(paneId))
            return
        if (paneId === selected)
            selected = beside
        beside = ""
        focused = selected
        changed()
    }
    // ⌘\ : closes the split, or shows the next pane (sidebar order) beside the focused one.
    function toggleSplit() {
        if (split) {
            closeSplit()
            return
        }
        if (overview)
            return
        const order = paneOrder().filter(id => id.indexOf("/") < 0 || id === selected)
        const start = order.indexOf(selected)
        for (let i = 1; i < order.length; ++i) {
            const candidate = order[(start + i) % order.length]
            if (candidate !== selected) {
                showBeside(candidate)
                return
            }
        }
    }
    function swapPanes() {
        if (!split)
            return
        const left = selected
        selected = beside
        beside = left
        ratio = 1 - ratio
        changed()
    }
    function showOverview() {
        selected = ""
        beside = ""
        focused = ""
        changed()
    }
    function focus(paneId: string) {
        if (!isShown(paneId) || focused === paneId)
            return
        focused = paneId
        changed()
    }
    function setRatio(value: real) {
        const clamped = Math.max(0.1, Math.min(0.9, value))
        if (Math.abs(clamped - ratio) < 0.001)
            return
        ratio = clamped
        changed()
    }
    // Next (+1) or previous (-1) entry of the sidebar, Overview included, in the focused pane.
    function cycle(step: int) {
        const order = [""].concat(paneOrder())
        const current = Math.max(0, order.indexOf(overview ? "" : focused))
        const next = order[(current + step + order.length) % order.length]
        if (next.length === 0)
            showOverview()
        else
            show(next)
    }

    // --- Sidebar, inspector, help -----------------------------------------------------------------
    function sidebarHiddenIn(cls: string): bool {
        const value = sidebarCollapsed[cls]
        return value === undefined ? cls === "compact" : value === true
    }
    function inspectorShownIn(cls: string): bool {
        const value = inspectorVisible[cls]
        return value === undefined ? cls === "wide" || cls === "medium" : value === true
    }
    function setSidebarShown(shown: bool) {
        const copy = Object.assign({}, sidebarCollapsed)
        copy[layoutClass] = !shown
        sidebarCollapsed = copy
        changed()
    }
    function setInspectorShown(shown: bool) {
        const copy = Object.assign({}, inspectorVisible)
        copy[layoutClass] = shown
        inspectorVisible = copy
        changed()
    }
    function showHelp(topic: string) {
        helpTopic = topic.length > 0 ? topic : helpTopicOf(focused)
        dockPage = "help"
        if (!inspectorShown)
            setInspectorShown(true)
        changed()
    }
    function showSettings() {
        dockPage = "inspector"
        if (!inspectorShown)
            setInspectorShown(true)
        changed()
    }
    // F1: the help of the focused pane, or back to its settings.
    function toggleHelp() {
        if (helpShown && inspectorShown)
            showSettings()
        else
            showHelp(helpTopicOf(focused))
    }
    // While help is shown it follows the focused pane.
    onFocusedChanged: {
        if (helpShown)
            helpTopic = helpTopicOf(focused)
    }

    // --- Serialization -----------------------------------------------------------------------------
    function toMap(): var {
        return { version: 2, selected: selected, beside: beside, ratio: ratio, focused: focused,
                 sidebarCollapsed: sidebarCollapsed, inspectorVisible: inspectorVisible,
                 dockPage: dockPage, helpTopic: helpTopic }
    }
    function validPane(value: var): string {
        const id = value === undefined || value === null ? "" : String(value)
        return isPane(id) ? id : ""
    }
    function validToggles(value: var): var {
        const map = {}
        if (value && typeof value === "object")
            for (const cls of ["wide", "medium", "narrow", "compact"])
                if (value[cls] !== undefined)
                    map[cls] = value[cls] === true
        return map
    }
    // Reads a v2 map, or migrates a v1 one ({tabs: [{primary, secondary, ratio}], current,
    // focused, dashboard*, helpOpen, helpTopic}): the current tab becomes selected/beside/ratio,
    // current -1 is Overview, the dashboard keys are dropped and helpOpen opens the help page.
    function fromMap(map: var) {
        const m = map ?? {}
        let first = ""
        let second = ""
        let share = 0.5
        let page = "inspector"
        if (Number(m.version ?? 1) >= 2 || m.selected !== undefined) {
            first = validPane(m.selected)
            second = validPane(m.beside)
            share = Number(m.ratio ?? 0.5)
            page = m.dockPage === "help" ? "help" : "inspector"
            sidebarCollapsed = validToggles(m.sidebarCollapsed)
            inspectorVisible = validToggles(m.inspectorVisible)
        } else {
            const tabs = Array.isArray(m.tabs) ? m.tabs : []
            const index = Number(m.current ?? -1)
            const tab = index >= 0 && index < tabs.length ? tabs[index] : null
            if (tab) {
                first = validPane(tab.primary)
                second = validPane(tab.secondary)
                share = Number(tab.ratio ?? 0.5)
            }
            page = m.helpOpen === true ? "help" : "inspector"
            sidebarCollapsed = {}
            inspectorVisible = {}
        }
        if (first.length === 0) {
            first = second
            second = ""
        }
        selected = first
        beside = second !== first ? second : ""
        ratio = isFinite(share) ? Math.max(0.1, Math.min(0.9, share)) : 0.5
        const wanted = validPane(m.focused)
        focused = isShown(wanted) ? wanted : selected
        dockPage = page
        helpTopic = m.helpTopic ? String(m.helpTopic) : "app"
        if (dockPage === "help" && !m.helpTopic)
            helpTopic = helpTopicOf(focused)
        markOpened(selected)
        markOpened(beside)
        changed()
    }
}
