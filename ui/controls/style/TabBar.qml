import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Underlined tab bar (instrument sub-pages such as "Basic Settings" / "Advanced Settings").
T.TabBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    spacing: 2

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
                x: 8
                width: parent.width - 16
                height: 2
                radius: 1
                color: Theme.accent
            }
        }
    }

    background: Item {
        implicitHeight: 34
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.divider
        }
    }
}
