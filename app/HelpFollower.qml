import QtQuick
import MyBench.Framework

// Keeps the help drawer on the focused panel's page while it is open (app.md 2.5): focus change,
// tab switch, split pane, Home ("app").
QtObject {
    id: follower

    required property WorkspaceState workspace

    function follow() {
        const ws = workspace
        if (!ws || !ws.helpOpen)
            return
        const topic = !ws.home && ws.focused.length > 0 ? (InstrumentRegistry.info(ws.focused).helpId ?? "app") : "app"
        if (topic !== ws.helpTopic)
            ws.showHelp(topic)
    }

    property Connections connections: Connections {
        target: follower.workspace
        function onFocusedChanged() { follower.follow() }
        function onCurrentIndexChanged() { follower.follow() }
    }
}
