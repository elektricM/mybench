import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// Text button with an optional icon and a tone:
//   "neutral" (default, outlined), "accent" (primary, filled), "success"/"danger" (filled),
//   "ghost" (no background until hovered), "subtle" (tinted with `color`).
//     ActionButton { text: qsTr("Autoscale"); iconName: "autoscale"; onClicked: ... }
T.Button {
    id: control

    property string tone: "neutral"
    property string iconName
    property string toolTip
    property string shortcutText
    property color color: Theme.accent          // tint of the "subtle" tone
    property bool compact: false
    property bool large: false

    readonly property bool filled: tone === "accent" || tone === "success" || tone === "danger"
    readonly property color fillColor: tone === "success" ? (hovered ? Theme.runHover : Theme.run)
                                     : tone === "danger" ? (hovered ? Theme.stopHover : Theme.stop)
                                     : (down ? Theme.accentPressed : hovered ? Theme.accentHover : Theme.accent)
    readonly property color foreground: !enabled ? Theme.textDisabled
                                      : filled ? Theme.textOnAccent
                                      : tone === "subtle" ? color
                                      : Theme.text

    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: large ? Theme.controlHeightL : compact ? Theme.controlHeightS : Theme.controlHeight
    leftPadding: compact ? 8 : large ? 16 : 12
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    spacing: 6
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.pixelSize: compact ? Theme.fontSizeM : Theme.fontSizeBody
    font.weight: filled ? Font.DemiBold : Font.Medium

    background: Rectangle {
        implicitWidth: control.compact ? 48 : 64
        radius: Theme.radiusM
        opacity: control.enabled ? 1 : 0.55
        color: control.filled ? control.fillColor
             : control.tone === "subtle" ? Theme.alpha(control.color, control.down ? 0.26 : control.hovered ? 0.2 : 0.13)
             : control.tone === "ghost" ? (control.down ? Theme.surfacePressed : control.hovered ? Theme.surfaceHover : "transparent")
             : (control.down ? Theme.surfacePressed : control.hovered ? Theme.surfaceHover : Theme.surfaceRaised)
        border.width: control.tone === "neutral" || control.checked ? 1 : 0
        border.color: control.checked ? Theme.accent : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        // subtle top highlight on filled buttons
        Rectangle {
            visible: control.filled
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusM + 3 }
    }

    contentItem: Row {
        spacing: control.spacing
        Icon {
            visible: control.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: control.compact ? Theme.iconSizeS : Theme.iconSize
            color: control.foreground
        }
        Text {
            visible: control.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.foreground
            elide: Text.ElideRight
        }
    }

    ToolTip.visible: hovered && toolTip.length > 0 && !down
    ToolTip.delay: 500
    ToolTip.text: shortcutText.length > 0 ? toolTip + "   " + shortcutText : toolTip
}
