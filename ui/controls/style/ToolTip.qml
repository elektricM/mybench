import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Tooltip: a small neutral surface with a hairline border, secondary-size text, sentence case.
// One delay everywhere (Theme.toolTipDelay): a positive delay set by a caller is normalized to it;
// 0 still means "show now" (validation messages).
T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - 4
    implicitWidth: Math.min(360, Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                          implicitContentWidth + leftPadding + rightPadding))
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: Theme.spacingS
    topPadding: 4
    bottomPadding: 4
    leftPadding: Theme.spacingS
    rightPadding: Theme.spacingS
    font.pixelSize: Theme.fontSizeSmall
    delay: Theme.toolTipDelay
    onDelayChanged: {
        if (delay > 0 && delay !== Theme.toolTipDelay)
            delay = Theme.toolTipDelay
    }
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        color: Theme.text
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.dark ? Theme.mix(Theme.window, "#FFFFFF", 0.06) : Theme.base
        border.width: Theme.borderWidth
        border.color: Theme.alpha(Theme.text, Theme.dark ? 0.2 : 0.16)
    }
}
