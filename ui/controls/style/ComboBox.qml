pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Pop-up button (docs/UI.md §6 "Choices"): looks like a push button with a chevron; editable
// combos look like a text field. The list opens below as a popup window (in-window popup in
// screenshot/test mode, Theme.inlinePopups), the current entry carries a check mark.
// - Press-drag-release: press the combo, drag onto the list, release on an entry to pick it.
// - Wheel: only when the combo has focus or the wheel gesture starts over it (WheelGuard); never
//   on macOS, where pop-up buttons ignore the wheel. Type-ahead selects by the first letters.
T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    // Stable width from the widest entry (a combo never resizes when its value changes).
    implicitContentWidthPolicy: T.ComboBox.WidestTextWhenCompleted
    leftPadding: Theme.spacingS + (!mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    rightPadding: Theme.spacingS + (mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    topPadding: 0
    bottomPadding: 0
    spacing: Theme.spacingXs
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody

    // --- Wheel (WheelGuard etiquette) ----------------------------------------------------------
    // T.ComboBox's own wheel handling stays off: it would take the events unconditionally.
    property bool wheelChanges: !Theme.macShortcuts
    wheelEnabled: false
    property real wheelAccumulator: 0
    WheelHandler {
        // A disabled handler lets the event through to the enclosing scroll view.
        enabled: control.wheelChanges && control.enabled && !control.popup.visible
                 && (control.activeFocus || WheelGuard.freshGesture)
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            control.wheelAccumulator += event.angleDelta.y
            const steps = Math.trunc(control.wheelAccumulator / 120)
            if (steps === 0)
                return
            control.wheelAccumulator -= steps * 120
            // wheel up = previous entry (the C++ steppers keep bindings on currentIndex intact)
            for (let i = 0; i < Math.abs(steps); ++i) {
                if (steps > 0)
                    control.decrementCurrentIndex()
                else
                    control.incrementCurrentIndex()
            }
        }
    }

    // --- Press-drag-release --------------------------------------------------------------------
    property int dragIndex: -1
    readonly property ListView dragList: popup.contentItem as ListView
    function delegateAt(scenePoint: point): var {
        if (!popup.visible || !dragList)
            return null
        const local = dragList.mapFromItem(null, scenePoint.x, scenePoint.y)
        if (local.x < 0 || local.y < 0 || local.x > dragList.width || local.y > dragList.height)
            return null
        return dragList.itemAt(local.x + dragList.contentX, local.y + dragList.contentY)
    }
    PointHandler {
        id: dragSelect
        acceptedButtons: Qt.LeftButton
        enabled: control.enabled
        property bool openedByDrag: false
        function overCombo(): bool {
            return control.contains(control.mapFromItem(null, point.scenePosition.x, point.scenePosition.y))
        }
        onActiveChanged: {
            if (active) {
                openedByDrag = false
                return
            }
            const item = overCombo() ? null : control.delegateAt(point.scenePosition)
            control.dragIndex = -1
            if (openedByDrag && !overCombo()) {
                if (item !== null)
                    item.click()
                else
                    control.popup.close()
            }
            openedByDrag = false
        }
        onPointChanged: {
            if (!active)
                return
            const below = control.mapFromItem(null, point.scenePosition.x, point.scenePosition.y).y > control.height
            if (!overCombo() && below && !control.popup.visible) {
                openedByDrag = true
                control.popup.open()
            }
            const item = openedByDrag && !overCombo() ? control.delegateAt(point.scenePosition) : null
            control.dragIndex = item ? item.index : -1
        }
    }

    delegate: T.ItemDelegate {
        id: row
        required property var model
        required property int index
        readonly property bool current: control.currentIndex === index
        readonly property bool lit: control.highlightedIndex === index || control.dragIndex === index

        width: ListView.view.width
        implicitHeight: Theme.controlHeight - 2
        leftPadding: Theme.spacingS + 16
        rightPadding: Theme.spacingS
        text: model[control.textRole]
        font: control.font
        hoverEnabled: control.hoverEnabled
        highlighted: lit
        checkable: true
        checked: current
        Accessible.name: text

        contentItem: Text {
            text: row.text
            font: row.font
            color: !row.enabled ? Theme.textDisabled : row.lit ? Theme.selectionText : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusControl - 1
            color: row.lit ? Theme.selection : "transparent"
            Icon {
                visible: row.current
                x: (Theme.spacingS + 16 - width) / 2
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: 12
                color: row.lit ? Theme.selectionText : Theme.text
            }
        }
    }

    indicator: Icon {
        x: control.mirrored ? Theme.spacingS : control.width - width - Theme.spacingS
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 12
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
    }

    contentItem: T.TextField {
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionText
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl
        color: control.editable ? Theme.base : control.flat ? "transparent" : Theme.controlFill
        border.width: control.flat && !control.editable ? 0 : Theme.borderWidth
        border.color: control.editable && control.activeFocus ? Theme.accent : Theme.controlBorder
        opacity: control.enabled ? 1 : 0.55
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: !control.editable
            color: control.down || control.popup.visible ? Theme.pressedFill
                 : control.hovered ? Theme.hoverFill : "transparent"
        }
        FocusRing { shown: control.visualFocus }
    }

    popup: T.Popup {
        popupType: Theme.inlinePopups ? T.Popup.Item : T.Popup.Window
        y: control.height + 2
        width: Math.max(control.width, 140)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 420)
        topMargin: Theme.spacingS
        bottomMargin: Theme.spacingS
        padding: Theme.spacingXs
        font: control.font
        palette: control.palette

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: PopupFrame {
            shadow: control.popup.popupType === T.Popup.Item
        }
    }
}
