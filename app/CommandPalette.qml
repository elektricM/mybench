pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// The command palette (⌘K, docs/UI.md §9): a centered popup with a search field over every
// command. `commands()` returns [{ title, subtitle, icon, keys, keywords, argumentLabel, run }]
// (AppActions.paletteCommands()). A command with `argumentLabel` ("Voltage (V)") asks for a value
// first: the field switches to that prompt and Return calls run(text).
//     palette.openFor(command)   // opens directly in argument mode (menu entries with a value)
Popup {
    id: palette

    property var commands: function() { return [] }
    property var all: []
    property var results: []
    property int current: 0
    // The command waiting for its argument (argument mode), or null.
    property var pending: null
    readonly property int rowHeight: Theme.controlHeight + Theme.spacingXs + 2
    readonly property int visibleRows: 9

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
    function openFor(command: var) {
        open()
        pending = command
        field.text = ""
    }
    function move(step: int) {
        if (results.length > 0)
            current = Math.max(0, Math.min(results.length - 1, current + step))
    }

    readonly property Item host: parent as Item
    x: Math.round(((host ? host.width : 800) - width) / 2)
    y: Math.round(Math.min((host ? host.height : 600) * 0.16, 120))
    width: Math.min(640, (host ? host.width : 800) - 2 * Theme.spacingXl)
    height: Math.min(content.implicitHeight, (host ? host.height : 600) - y - Theme.spacingXl)
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onAboutToShow: {
        all = commands()
        pending = null
        field.text = ""
        refresh("")
    }
    onOpened: field.forceActiveFocus()

    background: PopupFrame {}
    T.Overlay.modal: Rectangle { color: Theme.alpha(Theme.overlay, 0.5) }

    contentItem: ColumnLayout {
        id: content
        spacing: 0

        // The search field (or the argument prompt). Built from the template: a borderless field
        // in the popup's header row (a native SearchField refuses a custom look).
        T.TextField {
            id: field
            objectName: "paletteField"
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeightL + Theme.spacingS
            leftPadding: Theme.spacingL + Theme.iconSize + Theme.spacingS
            rightPadding: Theme.spacingL
            color: Theme.text
            font.pixelSize: Theme.fontSizeHeading
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: palette.pending ? palette.pending.title + " — " + palette.pending.argumentLabel
                                             : qsTr("Search commands and instruments")
            Accessible.name: placeholderText
            onTextChanged: {
                if (!palette.pending)
                    palette.refresh(text)
            }
            Icon {
                x: Theme.spacingL
                y: Math.round((field.height - height) / 2)
                name: palette.pending ? "chevron-right" : "search"
                color: palette.pending ? Theme.accent : Theme.textSecondary
            }
            // The template draws no placeholder.
            Text {
                x: field.leftPadding
                width: field.width - field.leftPadding - field.rightPadding
                height: field.height
                visible: field.text.length === 0 && !field.preeditText
                text: field.placeholderText
                color: Theme.textTertiary
                font: field.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            Keys.onDownPressed: palette.move(1)
            Keys.onUpPressed: palette.move(-1)
            Keys.onReturnPressed: palette.runCurrent()
            Keys.onEnterPressed: palette.runCurrent()
            Keys.onEscapePressed: palette.close()
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.borderWidth
            color: Theme.separator
        }

        ListView {
            id: list
            objectName: "paletteList"
            Layout.fillWidth: true
            visible: palette.pending === null
            Layout.preferredHeight: visible ? Math.min(Math.max(contentHeight, palette.rowHeight), palette.visibleRows * palette.rowHeight)
                                              + 2 * Theme.spacingXs : 0
            topMargin: Theme.spacingXs
            bottomMargin: Theme.spacingXs
            clip: true
            model: palette.results
            currentIndex: palette.current
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: T.ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: palette.rowHeight
                leftPadding: Theme.spacingL
                rightPadding: Theme.spacingL
                highlighted: index === palette.current
                hoverEnabled: true
                onClicked: {
                    palette.current = index
                    palette.runCurrent()
                }
                onHoveredChanged: {
                    if (hovered)
                        palette.current = index
                }
                background: Rectangle {
                    x: Theme.spacingXs + 2
                    width: parent.width - 2 * x
                    radius: Theme.radiusControl
                    color: row.highlighted ? Theme.selection : "transparent"
                }
                contentItem: RowLayout {
                    spacing: Theme.spacingS + 2
                    Icon {
                        name: row.modelData.icon ?? ""
                        color: row.highlighted ? Theme.selectionText : Theme.textSecondary
                    }
                    Text {
                        Layout.preferredWidth: implicitWidth
                        Layout.minimumWidth: Math.min(implicitWidth, row.width / 3)
                        Layout.fillWidth: false
                        text: row.modelData.title
                        color: row.highlighted ? Theme.selectionText : Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.subtitle ?? ""
                        color: row.highlighted ? Theme.selectionText : Theme.textTertiary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideMiddle
                    }
                    Text {
                        visible: text.length > 0
                        text: Theme.keys(row.modelData.keys ?? "")
                        color: row.highlighted ? Theme.selectionText : Theme.textTertiary
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: palette.results.length === 0
                text: qsTr("No matching commands")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeBody
            }
        }

        // Argument mode: what Return does.
        Text {
            visible: palette.pending !== null
            Layout.fillWidth: true
            Layout.margins: Theme.spacingL
            text: palette.pending ? qsTr("Type a value and press Return. Esc cancels.") : ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
    }
}
