pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The cursor readout at the end of the result row (round 5), drawn like a MeasurementCard.
// Cursors on: a small table — C1 and C2 (frequency, gain, phase) in the cursor colors and their
// difference C2 − C1 with the gain slope in dB/decade. Cursors off: the last measured point.
Item {
    id: root

    required property BodeController controller
    property bool linearGain: false
    property color cursor1Color: Theme.cursor1
    property color cursor2Color: Theme.cursor2

    readonly property bool cursorsShown: controller.cursorsOn && controller.cursorIndex >= 0
    // Texts as shown (tests read them).
    readonly property string slopeText: isFinite(controller.cursorSlope)
                                        ? qsTr("%1 dB/dec").arg(signed(controller.cursorSlope, 1)) : "—"
    readonly property string deltaFrequencyText: isFinite(controller.cursor2Frequency - controller.cursorFrequency)
                                                  ? sign(controller.cursor2Frequency - controller.cursorFrequency)
                                                    + Units.format(Math.abs(controller.cursor2Frequency - controller.cursorFrequency), "Hz", 4)
                                                  : "—"

    function sign(v: real): string { return v < 0 ? "−" : v > 0 ? "+" : "" }
    function signed(v: real, decimals: int): string { return sign(v) + Math.abs(v).toFixed(decimals) }
    function frequencyText(f: real): string { return isFinite(f) && f > 0 ? Units.format(f, "Hz", 4) : "—" }
    function gainText(db: real): string {
        if (!isFinite(db))
            return "—"
        return linearGain ? Math.pow(10, db / 20).toPrecision(3) : (db < 0 ? "−" : "") + Math.abs(db).toFixed(2) + " dB"
    }
    function phaseText(deg: real): string { return isFinite(deg) ? (deg < 0 ? "−" : "") + Math.abs(deg).toFixed(1) + "°" : "—" }
    function deltaGainText(): string {
        const g1 = controller.cursorGainDb, g2 = controller.cursor2GainDb
        if (!isFinite(g1) || !isFinite(g2))
            return "—"
        return linearGain ? "×" + Math.pow(10, (g2 - g1) / 20).toPrecision(3) : signed(g2 - g1, 2) + " dB"
    }
    function deltaPhaseText(): string {
        const p1 = controller.cursorPhase, p2 = controller.cursor2Phase
        return isFinite(p1) && isFinite(p2) ? signed(p2 - p1, 1) + "°" : "—"
    }

    implicitWidth: 360
    implicitHeight: 64
    Accessible.role: Accessible.StaticText
    Accessible.name: cursorsShown ? qsTr("Cursors") : qsTr("Last point")

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.cardFill
        border.width: 1
        border.color: Theme.cardBorder
        Rectangle {   // the source color down the left edge, as on the MeasurementCards
            width: 3
            radius: 1.5
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; leftMargin: 4; topMargin: 8; bottomMargin: 8 }
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.cursorsShown ? root.cursor1Color : Theme.textSecondary }
                GradientStop { position: 1.0; color: root.cursorsShown ? root.cursor2Color : Theme.textSecondary }
            }
        }
    }

    // --- Cursors on: C1, C2, Δ ----------------------------------------------------------------------
    GridLayout {
        objectName: "cursorTable"
        visible: root.cursorsShown
        anchors { fill: parent; leftMargin: 13; rightMargin: 8; topMargin: 5; bottomMargin: 5 }
        columns: 5
        rowSpacing: 0
        columnSpacing: 8

        component Cell: Text {
            property bool strong: false
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            color: Theme.text
            font.pixelSize: Theme.fontSizeS
            font.weight: strong ? Font.DemiBold : Font.Normal
            font.features: Theme.tabularFigures
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
        component Tag: Text {
            Layout.preferredWidth: 18
            font.pixelSize: Theme.fontSizeS
            font.weight: Font.DemiBold
        }

        Tag { text: "C1"; color: root.cursor1Color }
        Cell { text: root.frequencyText(root.controller.cursorFrequency) }
        Cell { text: root.gainText(root.controller.cursorGainDb) }
        Cell { text: root.phaseText(root.controller.cursorPhase) }
        Item { Layout.fillWidth: true; Layout.preferredWidth: 1.3 }

        Tag { text: "C2"; color: root.cursor2Color }
        Cell { text: root.frequencyText(root.controller.cursor2Frequency) }
        Cell { text: root.gainText(root.controller.cursor2GainDb) }
        Cell { text: root.phaseText(root.controller.cursor2Phase) }
        Item { Layout.fillWidth: true; Layout.preferredWidth: 1.3 }

        Tag { text: "Δ"; color: Theme.textSecondary }
        Cell { text: root.deltaFrequencyText; strong: true }
        Cell { text: root.deltaGainText(); strong: true }
        Cell { text: root.deltaPhaseText(); strong: true }
        Cell {
            objectName: "slopeText"
            Layout.preferredWidth: 1.3
            text: root.slopeText
            strong: true
            ToolTip.visible: slopeHover.hovered
            ToolTip.text: qsTr("Gain slope from C1 to C2")
            HoverHandler { id: slopeHover }
        }
    }

    // --- Cursors off: the last measured point ---------------------------------------------------------
    ColumnLayout {
        visible: !root.cursorsShown
        anchors { fill: parent; leftMargin: 11; rightMargin: 6; topMargin: 6; bottomMargin: 7 }
        spacing: 1
        RowLayout {
            spacing: 6
            Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 2; color: Theme.textSecondary }
            Text {
                text: qsTr("Last point")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
            }
        }
        Text {
            objectName: "lastPointText"
            Layout.fillWidth: true
            Layout.fillHeight: true
            verticalAlignment: Text.AlignVCenter
            text: root.controller.readoutValid
                  ? [root.frequencyText(root.controller.readoutFrequency),
                     root.linearGain ? root.controller.readoutGain.toPrecision(3) : root.gainText(root.controller.readoutGainDb),
                     root.phaseText(root.controller.readoutPhase)].join("   ")
                  : "—"
            color: Theme.text
            font.pixelSize: Theme.fontSizeReadoutM
            font.weight: Font.DemiBold
            font.features: Theme.tabularFigures
            elide: Text.ElideRight
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: Theme.fontSizeBody
        }
    }
}
