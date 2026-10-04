pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import MyBench.Framework

// A value from a fixed sequence (1-2-5 V/div, s/div, sample rates…), docs/UI.md §6 and §13.8:
// ‹ value ▾ ›. The arrows and ↑/↓ (→/←) step to the next/previous entry, Home/End jump to the ends,
// a click on the value (or Space) opens the list, the wheel steps per WheelGuard (focused, or
// at the start of a gesture). Controlled component: bind currentIndex and apply activated(index).
//     StepSelector {
//         model: ctl.timeDivValues; unit: "s"; currentIndex: ctl.timeDivIndex
//         onActivated: index => ctl.timeDivIndex = index
//     }
// `model`: ascending numbers, or objects { value, text }. `formatter(value)` overrides the text of
// numbers (default: Units.format with `digits` significant digits, trailing zeros trimmed).
T.Control {
    id: control

    property var model: []
    property int currentIndex: 0
    property string unit
    property var formatter: null
    property int digits: 3
    property string label                // accessible name, e.g. "Time/Div"
    readonly property int count: model ? model.length : 0
    readonly property string currentText: currentIndex >= 0 && currentIndex < count ? textAt(currentIndex) : "—"
    readonly property bool atStart: currentIndex <= 0
    readonly property bool atEnd: currentIndex >= count - 1
    readonly property alias popup: listPopup

    signal activated(int index)

    function valueAt(index: int): var {
        const entry = model[index]
        return entry !== null && typeof entry === "object" ? entry.value : entry
    }
    function textAt(index: int): string {
        const entry = model[index]
        if (entry !== null && typeof entry === "object" && entry.text !== undefined)
            return String(entry.text)
        if (typeof formatter === "function")
            return formatter(valueAt(index))
        return Units.format(Number(valueAt(index)), unit, digits, true)
    }
    function select(index: int) {
        const i = Math.max(0, Math.min(count - 1, index))
        if (count > 0 && i !== currentIndex)
            activated(i)
    }
    function step(n: int) {
        if (enabled)
            select(currentIndex + n)
    }

    // Stable width: the widest entry.
    readonly property real widestText: {
        let widest = 0
        for (let i = 0; i < count; ++i)
            widest = Math.max(widest, metrics.advanceWidth(textAt(i)))
        return Math.ceil(widest)
    }
    FontMetrics {
        id: metrics
        font: control.font
    }

    implicitWidth: arrowWidth * 2 + widestText + 2 * Theme.spacingS + 12 + Theme.spacingXs
    implicitHeight: Theme.controlHeight
    readonly property real arrowWidth: Theme.controlHeight - 6
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontSizeBody
    font.features: Theme.tabularFigures
    Accessible.role: Accessible.ComboBox
    Accessible.name: label.length > 0 ? label : currentText
    Accessible.description: currentText

    Keys.onUpPressed: step(1)
    Keys.onRightPressed: step(1)
    Keys.onDownPressed: step(-1)
    Keys.onLeftPressed: step(-1)
    Keys.onSpacePressed: listPopup.open()
    Keys.onReturnPressed: listPopup.open()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) {
            select(0)
            event.accepted = true
        } else if (event.key === Qt.Key_End) {
            select(count - 1)
            event.accepted = true
        }
    }

    // A wheel gesture that starts over the selector keeps stepping it until the gesture ends; one
    // that started elsewhere passes over it (WheelGuard).
    property real wheelAccumulator: 0
    property bool wheelOwner: false
    Connections {
        target: WheelGuard
        function onGestureStarted() {
            control.wheelOwner = false
        }
    }
    WheelHandler {
        enabled: control.enabled && !listPopup.visible && (control.activeFocus || WheelGuard.freshGesture || control.wheelOwner)
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            control.wheelOwner = true
            control.wheelAccumulator += event.angleDelta.y
            const steps = Math.trunc(control.wheelAccumulator / 120)
            if (steps === 0)
                return
            control.wheelAccumulator -= steps * 120
            control.step(steps)        // wheel up = next (larger) entry
        }
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.controlFill
        border.width: Theme.borderWidth
        border.color: Theme.controlBorder
        opacity: control.enabled ? 1 : 0.55
        FocusRing { shown: control.visualFocus }
    }

    component Arrow: T.AbstractButton {
        id: arrow
        required property int direction
        width: control.arrowWidth
        height: control.height
        enabled: control.enabled && (direction < 0 ? !control.atStart : !control.atEnd)
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        autoRepeat: true
        autoRepeatDelay: 400
        autoRepeatInterval: 120
        onClicked: control.step(direction)
        Accessible.name: direction < 0 ? qsTr("Previous") : qsTr("Next")
        background: Rectangle {
            radius: Theme.radiusControl
            color: arrow.down ? Theme.pressedFill : arrow.hovered ? Theme.hoverFill : "transparent"
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: arrow.direction < 0 ? "chevron-left" : "chevron-right"
                size: 12
                color: arrow.enabled ? (arrow.hovered ? Theme.text : Theme.textSecondary) : Theme.textDisabled
            }
        }
    }

    contentItem: Item {
        Arrow {
            id: previous
            direction: -1
        }
        Rectangle {
            x: previous.width
            y: 4
            width: Theme.borderWidth
            height: parent.height - 8
            color: Theme.separator
        }
        T.AbstractButton {
            id: center
            x: previous.width + 1
            width: parent.width - 2 * control.arrowWidth - 2
            height: parent.height
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            onClicked: listPopup.visible ? listPopup.close() : listPopup.open()
            background: Rectangle {
                color: center.down || listPopup.visible ? Theme.pressedFill : center.hovered ? Theme.hoverFill : "transparent"
            }
            contentItem: Item {
                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingXs
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: control.currentText
                        font: control.font
                        color: control.enabled ? Theme.text : Theme.textDisabled
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-down"
                        size: 10
                        color: control.enabled ? Theme.textTertiary : Theme.textDisabled
                    }
                }
            }
        }
        Rectangle {
            x: parent.width - control.arrowWidth - 1
            y: 4
            width: Theme.borderWidth
            height: parent.height - 8
            color: Theme.separator
        }
        Arrow {
            x: parent.width - width
            direction: 1
        }
    }

    T.Popup {
        id: listPopup
        popupType: Theme.inlinePopups ? T.Popup.Item : T.Popup.Window
        y: control.height + 2
        x: Math.round((control.width - width) / 2)
        width: Math.max(control.width, 120)
        height: Math.min(list.contentHeight + topPadding + bottomPadding, 360)
        padding: Theme.spacingXs
        topMargin: Theme.spacingS
        bottomMargin: Theme.spacingS
        onOpened: list.positionViewAtIndex(control.currentIndex, ListView.Center)

        contentItem: ListView {
            id: list
            clip: true
            model: control.count
            currentIndex: control.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: ScrollIndicator {}
            delegate: T.ItemDelegate {
                id: row
                required property int index
                readonly property bool current: index === control.currentIndex
                width: ListView.view.width
                implicitHeight: Theme.controlHeight - 2
                leftPadding: Theme.spacingS + 16
                rightPadding: Theme.spacingS
                hoverEnabled: true
                text: control.textAt(index)
                onClicked: {
                    control.select(index)
                    listPopup.close()
                }
                contentItem: Text {
                    text: row.text
                    font: control.font
                    color: row.hovered ? Theme.selectionText : Theme.text
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusControl - 1
                    color: row.hovered ? Theme.selection : "transparent"
                    Icon {
                        visible: row.current
                        x: (Theme.spacingS + 16 - width) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        name: "check"
                        size: 12
                        color: row.hovered ? Theme.selectionText : Theme.text
                    }
                }
            }
        }
        background: PopupFrame {
            shadow: listPopup.popupType === T.Popup.Item
        }
    }
}
