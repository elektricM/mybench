pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.DialogButtonBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            (control.count === 1 ? implicitContentWidth * 2 : implicitContentWidth) + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    contentWidth: (contentItem as ListView)?.contentWidth ?? 0

    spacing: 8
    padding: 16
    topPadding: 8
    alignment: count === 1 ? Qt.AlignRight : undefined

    delegate: Button {
        width: Math.max(implicitWidth, 80)
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
