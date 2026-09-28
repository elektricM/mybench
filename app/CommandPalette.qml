pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Command palette (Ctrl/⌘K): fuzzy search over every command of the app.
// `commands()` returns [{ title, subtitle, icon, keys, keywords, run: function }]. A command with
// `argumentLabel` ("Voltage (V)") asks for a value first: the field switches to that prompt and
// Enter calls run(text).
Popup {
    id: palette

    property var commands: function() { return [] }
    property var all: []
    property var results: []
    property int current: 0
    // The command waiting for its argument (argument mode), or null.
    property var pending: null

    function score(command: var, query: string): real {
        if (query.length === 0)
            return 1
        const text = (command.title + " " + (command.keywords ?? "") + " " + (command.subtitle ?? "")).toLowerCase()
        const q = query.toLowerCase()
        const direct = text.indexOf(q)
        if (direct >= 0)
            return 1000 - direct + (direct === 0 || text[direct - 1] === " " ? 200 : 0)
        // subsequence
        let i = 0
        let gaps = 0
        let last = -1
        for (const ch of q) {
            const found = text.indexOf(ch, i)
            if (found < 0)
                return -1
            if (last >= 0)
                gaps += found - last - 1
            last = found
            i = found + 1
        }
        return 500 - gaps
    }
    function refresh(query: string) {
        const scored = []
        for (let i = 0; i < all.length; ++i) {
            const s = score(all[i], query.trim())
            if (s >= 0)
                scored.push({ command: all[i], score: s, order: i })
        }
        scored.sort((a, b) => b.score !== a.score ? b.score - a.score : a.order - b.order)
        results = scored.map(e => e.command)
        current = 0
    }
    function runCurrent() {
        if (pending) {
            const command = pending
            const text = field.text
            close()
            Qt.callLater(() => command.run(text))
            return
        }
        const command = results[current]
        if (command && (command.argumentLabel ?? "").length > 0) {
            pending = command
            field.text = ""
            return
        }
        close()
        if (command)
            Qt.callLater(command.run)
    }

    readonly property Item host: parent as Item
    x: Math.round(((host ? host.width : 800) - width) / 2)
    y: 76
    width: Math.min(600, (host ? host.width : 800) - 48)
    height: Math.min(content.implicitHeight, (host ? host.height : 600) - 140)
    padding: 0
    modal: true
    focus: true

    onAboutToShow: {
        all = commands()
        pending = null
        field.text = ""
        refresh("")
    }
    onOpened: field.forceActiveFocus()

    contentItem: ColumnLayout {
        id: content
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingM
            implicitHeight: 52
            spacing: Theme.spacingM
            Icon {
                name: palette.pending ? (palette.pending.icon ?? "chevron-right") : "search"
                size: 18
                color: palette.pending ? Theme.accent : Theme.textMuted
            }
            Text {
                visible: palette.pending !== null
                text: palette.pending ? palette.pending.title + " ›" : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeL + 1
            }
            TextField {
                id: field
                Layout.fillWidth: true
                objectName: "paletteField"
                placeholderText: palette.pending ? palette.pending.argumentLabel : qsTr("Type a command or an instrument…")
                font.pixelSize: Theme.fontSizeL + 1
                background: Item {}
                leftPadding: 0
                onTextEdited: if (!palette.pending) palette.refresh(text)
                Keys.onDownPressed: palette.current = Math.min(palette.current + 1, palette.results.length - 1)
                Keys.onUpPressed: palette.current = Math.max(palette.current - 1, 0)
                Keys.onReturnPressed: palette.runCurrent()
                Keys.onEnterPressed: palette.runCurrent()
                Keys.onEscapePressed: palette.close()
            }
            KeyHint { keys: "Esc" }
        }
        Divider { Layout.fillWidth: true }

        ListView {
            id: list
            Layout.fillWidth: true
            visible: palette.pending === null
            Layout.preferredHeight: visible ? Math.min(contentHeight, 8 * 44) + 12 : 0
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            clip: true
            model: palette.results
            currentIndex: palette.current
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: list.width - 12
                x: 6
                height: 44
                highlighted: index === palette.current
                onClicked: {
                    palette.current = index
                    palette.runCurrent()
                }
                onHoveredChanged: if (hovered) palette.current = index
                contentItem: RowLayout {
                    spacing: Theme.spacingM
                    Rectangle {
                        implicitWidth: 28
                        implicitHeight: 28
                        radius: Theme.radiusM
                        color: row.highlighted ? Theme.accentMuted : Theme.surfaceSunken
                        border.width: 1
                        border.color: row.highlighted ? Theme.alpha(Theme.accent, 0.4) : Theme.border
                        Icon {
                            anchors.centerIn: parent
                            name: row.modelData.icon ?? "chevron-right"
                            size: Theme.iconSizeS
                            color: row.highlighted ? Theme.accent : Theme.textSecondary
                        }
                    }
                    Text {
                        text: row.modelData.title
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: row.highlighted ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.subtitle ?? ""
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeM
                        elide: Text.ElideRight
                    }
                    KeyHint {
                        visible: (row.modelData.keys ?? "").length > 0
                        keys: row.modelData.keys ?? ""
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: palette.results.length === 0
                title: qsTr("No matching command")
            }
        }

        Divider { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            implicitHeight: 32
            spacing: Theme.spacingM
            Text { text: qsTr("↑↓ navigate"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeS }
            Text { text: palette.pending ? qsTr("↵ apply") : qsTr("↵ run"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeS }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("%n command(s)", "", palette.results.length); color: Theme.textMuted; font.pixelSize: Theme.fontSizeS }
        }
    }
}
