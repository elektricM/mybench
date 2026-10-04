import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Underlined tab bar: the current tab carries a 2 px accent line; a hairline under the bar.
T.TabBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    spacing: 0

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: Theme.durationNormal
        highlightResizeDuration: Theme.durationNormal
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 40
        preferredHighlightEnd: width - 40
        highlight: Item {
            z: 2
            Rectangle {
                anchors.bottom: parent.bottom
                x: Theme.spacingS
                width: parent.width - 2 * Theme.spacingS
                height: 2
                color: Theme.accent
            }
        }
    }

    background: Item {
        implicitHeight: Theme.controlHeight + 6
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.separator
        }
    }
}
