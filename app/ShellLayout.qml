import QtQuick
import MyBench.Controls

// The adaptive layout of the window (docs/UI.md §8), from its width, the workspace and the user's
// toggles. The breakpoints are Theme.layoutClass(): wide (sidebar expanded, inspector docked),
// medium (sidebar compact, inspector docked 280), narrow (sidebar compact, inspector overlay, no
// split), compact (sidebar drawer, inspector overlay, no split). A split compacts the expanded
// sidebar when that is what it takes to reach Theme.splitMinWidth of pane area; if even that is
// not enough, the split is suspended (only the focused pane shows, the split is kept).
QtObject {
    id: layout

    required property WorkspaceState workspace
    property real width: 1440
    property real preferredInspectorWidth: Theme.inspectorWidth
    // --sidebar full|compact|hidden and --inspector shown|hidden for this run ("" = none). The
    // shell clears them when the user toggles.
    property string sidebarOverride: ""
    property string inspectorOverride: ""

    readonly property string layoutClass: Theme.layoutClass(width)
    // Sidebar: a drawer opened from the toolbar (compact class), or docked (expanded / compact).
    readonly property bool sidebarDrawer: layoutClass === "compact" && sidebarOverride.length === 0
    readonly property bool sidebarVisible: sidebarOverride.length > 0 ? sidebarOverride !== "hidden"
                                                                     : !sidebarDrawer && workspace.sidebarShown
    readonly property bool sidebarBaseCompact: sidebarOverride === "compact"
                                               || (sidebarOverride.length === 0 && layoutClass !== "wide")
    readonly property bool sidebarAutoCompact: workspace.split && sidebarVisible && !sidebarBaseCompact
                                               && sidebarOverride.length === 0
                                               && width - Theme.sidebarWidth - dockWidth < Theme.splitMinWidth
                                               && width - Theme.sidebarCompactWidth - dockWidth >= Theme.splitMinWidth
    readonly property bool sidebarCompact: sidebarBaseCompact || sidebarAutoCompact
    readonly property real sidebarWidth: !sidebarVisible ? 0 : sidebarCompact ? Theme.sidebarCompactWidth : Theme.sidebarWidth
    // Inspector: docked on the right, or a sheet over the panes; nothing to show on Overview or
    // for a panel without inspector unless the help page is open.
    readonly property bool inspectorWanted: inspectorOverride.length > 0 ? inspectorOverride === "shown"
                                                                       : workspace.inspectorShown
    readonly property bool inspectorOverlay: layoutClass === "narrow" || layoutClass === "compact"
    // The focused panel has no inspector (its controls sit beside the display): no column either.
    // The focused instrument's panel (null on Overview and while the panel is still loading).
    property InstrumentPanel focusedPanel: null
    // Nothing to dock for the focused pane: a panel without inspector, or no panel yet (so the
    // dock column never flashes while a panel without inspector loads).
    property bool panelInspectorEmpty: focusedPanel === null || !focusedPanel.hasInspector
    readonly property bool inspectorHasContent: (!workspace.overview && !panelInspectorEmpty) || workspace.helpShown
    readonly property bool inspectorDocked: inspectorWanted && !inspectorOverlay && inspectorHasContent
    readonly property bool inspectorSheet: inspectorWanted && inspectorOverlay && inspectorHasContent
    // docs/UI.md §8: the docked inspector is at most 280 wide in the medium class.
    readonly property int mediumInspectorWidth: 280
    readonly property real inspectorWidth: layoutClass === "medium" ? Math.min(preferredInspectorWidth, mediumInspectorWidth)
                                                                    : preferredInspectorWidth
    readonly property real dockWidth: inspectorDocked ? inspectorWidth : 0
    // Panes.
    readonly property real paneArea: width - sidebarWidth - dockWidth
    readonly property bool splitSuspended: workspace.split && (inspectorOverlay || paneArea < Theme.splitMinWidth)
}
