import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// The instrument's big Run / Stop toggle (≈ 96 × 52). Stopped: accent fill "▶ Run". Running:
// positive-tinted fill, positive text "■ Stop" and `stateText` below ("Triggered", "Waiting for
// trigger"). `busy` (starting/stopping) dims it and ignores clicks. Emits toggled(); the owner
// starts or stops the instrument and binds `running`. Space/Enter when focused.
//     RunButton { running: ctl.running; stateText: ctl.stateText; onToggled: ctl.running ? ctl.stop() : ctl.start() }
T.AbstractButton {
    id: control

    property bool running: false
    property string stateText
    property bool busy: false
    property string runText: qsTr("Run")
    property string stopText: qsTr("Stop")
    property string shortcutText                    // shown in the tooltip, e.g. "⌘R"
    // toggled() (inherited from AbstractButton) is emitted on click, Space and Enter.

    implicitWidth: Math.max(96, label.implicitWidth + 24)
    implicitHeight: Math.max(52, label.implicitHeight + 16)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    checkable: false
    enabled: true
    Accessible.role: Accessible.Button
    Accessible.name: running ? stopText : runText
    Accessible.description: stateText
    Accessible.checkable: true
    Accessible.checked: running

    onClicked: {
        if (!busy)
            control.toggled()
    }
    Keys.onReturnPressed: control.click()
    Keys.onEnterPressed: control.click()

    readonly property color fill: running
                                  ? Theme.mix(Theme.base, Theme.positive, pressed ? 0.26 : hovered ? 0.21 : 0.16)
                                  : pressed ? Theme.accentPressed : hovered ? Theme.accentHover : Theme.accent
    readonly property color ink: running ? Theme.positive : Theme.accentText

    background: Rectangle {
        radius: Theme.radiusPopup
        color: control.fill
        border.width: control.running ? 1 : 0
        border.color: Theme.alpha(Theme.positive, 0.45)
        opacity: control.busy || !control.enabled ? 0.6 : 1
        FocusRing {
            shown: control.visualFocus
            radius: Theme.radiusPopup + Theme.focusWidth + 1
        }
    }

    contentItem: Item {
        Column {
            id: label
            anchors.centerIn: parent
            spacing: 1
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                // Play triangle / stop square, drawn so they match the text size.
                Item {
                    width: 11
                    height: 12
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        visible: control.running
                        anchors.centerIn: parent
                        width: 10
                        height: 10
                        radius: 2
                        color: control.ink
                    }
                    Canvas {
                        id: triangle
                        visible: !control.running
                        anchors.fill: parent
                        property color ink: control.ink
                        onInkChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = ink
                            ctx.beginPath()
                            ctx.moveTo(1, 0.5)
                            ctx.lineTo(width, height / 2)
                            ctx.lineTo(1, height - 0.5)
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                }
                Text {
                    text: control.running ? control.stopText : control.runText
                    color: control.ink
                    font.pixelSize: Theme.fontSizeHeading + 1
                    font.weight: Font.DemiBold
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: text.length > 0
                text: control.stateText
                color: control.ink
                opacity: 0.85
                font.pixelSize: Theme.fontSizeCaption
                font.weight: Font.Medium
                width: Math.min(implicitWidth, control.width - 12)
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    ToolTip.visible: hovered && ToolTip.text.length > 0
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: shortcutText.length > 0 ? (running ? stopText : runText) + "  " + shortcutText : ""
}
