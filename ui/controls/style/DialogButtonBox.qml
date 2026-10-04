pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// The button row of a dialog: right-aligned, the accept button is the primary (accent) button,
// dialog buttons are Theme.controlHeightL tall and at least 80 px wide.
T.DialogButtonBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            (control.count === 1 ? implicitContentWidth * 2 : implicitContentWidth) + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    contentWidth: (contentItem as ListView)?.contentWidth ?? 0

    spacing: Theme.spacingS
    padding: Theme.spacingL + 4
    topPadding: Theme.spacingM
    bottomPadding: Theme.spacingL
    alignment: Qt.AlignRight

    delegate: Button {
        width: Math.max(implicitWidth, 80)
        implicitHeight: Theme.controlHeightL
        highlighted: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
    }

    contentItem: ListView {
        implicitWidth: contentWidth
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
    }

    background: Item {}
}
