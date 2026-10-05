import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// Cursor readout of the Spectrum Analyzer (measurement strip): C1 and C2 in their cursor colors,
// then the differences (Δx, Δy and, on the time record, 1/Δx). Styled like MeasurementCard.
//     DsaCursorCard { c1: ctl.cursor1Text; c2: ctl.cursor2Text; delta: ctl.cursorDeltaText }
Item {
    id: card

    property string c1
    property string c2
    property string delta

    implicitWidth: Math.max(220, grid.implicitWidth + 11 + 10)
    implicitHeight: Math.max(64, grid.implicitHeight + 2 * 7 + 2)
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Cursors: C1 %1, C2 %2, %3").arg(c1).arg(c2).arg(delta)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.cardFill
        border.width: 1
        border.color: Theme.cardBorder
        // The pair's colors down the left edge (top half C1, bottom half C2).
        Column {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; leftMargin: 4; topMargin: 8; bottomMargin: 8 }
            Rectangle { width: 3; height: parent.height / 2; radius: 1.5; color: Theme.cursor1 }
            Rectangle { width: 3; height: parent.height / 2; radius: 1.5; color: Theme.cursor2 }
        }
    }

    GridLayout {
        id: grid
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 11; rightMargin: 10 }
        columns: 2
        columnSpacing: 8
        rowSpacing: 1
        component Name: Text {
            font.pixelSize: Theme.fontSizeCaption
            font.weight: Font.DemiBold
        }
        component Value: Text {
            Layout.fillWidth: true
            color: Theme.text
            font.pixelSize: Theme.fontSizeS
            font.features: Theme.tabularFigures
            elide: Text.ElideRight
        }
        Name { text: "C1"; color: Theme.cursor1 }
        Value { objectName: "cursorCardC1"; text: card.c1.length > 0 ? card.c1 : "—" }
        Name { text: "C2"; color: Theme.cursor2 }
        Value { objectName: "cursorCardC2"; text: card.c2.length > 0 ? card.c2 : "—" }
        Value { objectName: "cursorCardDelta"; visible: card.delta.length > 0; Layout.columnSpan: 2; text: card.delta; color: Theme.textSecondary }
    }
}
