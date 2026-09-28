import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// Collapsible inspector card with a title (ELVISmx group names: "Channel 0 Settings",
// "Timebase", "Trigger"...), an optional channel color, a summary shown when collapsed, and
// header tools (e.g. an Enabled switch). Children are laid out in a column; give them
// Layout.fillWidth: true.
//     Section {
//         title: qsTr("Channel 0 Settings"); accentColor: Theme.ch0
//         summary: "AI 0 · 1 V/div"
//         headerTools: ToggleSwitch { checked: ctl.ch0Enabled; onToggled: ctl.ch0Enabled = checked }
//         Field { label: qsTr("Source"); ComboBox { Layout.fillWidth: true } }
//     }
Item {
    id: root

    property string title
    property string summary                     // short state, shown on the right of the header
    property string iconName
    property color accentColor: "transparent"   // channel color (dot + tinted header)
    property bool collapsible: true
    property bool collapsed: false
    // When set (InstrumentPanel does it for its inspector: "<instrument>/<title>"), the collapsed
    // state the user chooses is kept in the session's view state and restored (app.md 11.1).
    property string stateKey
    property int contentSpacing: Theme.spacingS + 2
    property int padding: Theme.spacingM
    default property alias content: body.data
    property alias headerTools: tools.data
    readonly property bool hasAccent: accentColor.a > 0

    function restoreState() {
        if (stateKey.length === 0)
            return
        const stored = SessionManager.viewValue("collapsed/" + stateKey)
        if (stored !== undefined && stored !== null && Boolean(stored) !== collapsed)
            collapsed = Boolean(stored)
    }
    onStateKeyChanged: restoreState()
    Connections {
        target: SessionManager
        enabled: root.stateKey.length > 0
        function onViewStateChanged() { root.restoreState() }
    }

    Layout.fillWidth: true
    implicitWidth: Math.max(240, body.implicitWidth + 2 * padding)
    implicitHeight: header.height + (collapsed ? 0 : body.implicitHeight + padding + Theme.spacingXs) + 1
    clip: true
    Behavior on implicitHeight {
        enabled: Theme.animations
        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.border
    }

    // Header (click to collapse).
    T.AbstractButton {
        id: header
        width: parent.width
        height: 38
        enabled: root.collapsible
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: {
            root.collapsed = !root.collapsed
            if (root.stateKey.length > 0)
                SessionManager.setViewValue("collapsed/" + root.stateKey, root.collapsed)
        }
        Accessible.role: Accessible.Button
        Accessible.name: root.title
        Accessible.description: root.collapsed ? qsTr("collapsed") : qsTr("expanded")

        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Theme.radiusL - 1
                color: header.hovered ? Theme.surfaceHover : "transparent"
                opacity: 0.6
            }
            FocusRing { shown: header.visualFocus; anchors.margins: 1; radius: Theme.radiusL - 1 }
        }

        contentItem: Item {
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingS

                Rectangle {
                    visible: root.hasAccent
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: root.accentColor
                }
                Icon {
                    visible: root.iconName.length > 0 && !root.hasAccent
                    name: root.iconName
                    size: Theme.iconSizeS
                    color: Theme.textSecondary
                }
                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    visible: root.summary.length > 0 && (root.collapsed || tools.children.length === 0)
                    Layout.maximumWidth: root.width * 0.45
                    text: root.summary
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeM
                    font.features: Theme.tabularFigures
                    elide: Text.ElideRight
                }
                Row {
                    id: tools
                    spacing: Theme.spacingS
                    Layout.alignment: Qt.AlignVCenter
                }
                Icon {
                    visible: root.collapsible
                    name: "chevron-down-line"
                    size: Theme.iconSizeS
                    color: header.hovered ? Theme.text : Theme.textMuted
                    rotation: root.collapsed ? -90 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
                }
            }
        }
    }

    // Colored hairline under the header of channel sections.
    Rectangle {
        visible: root.hasAccent
        x: Theme.spacingM
        y: header.height - 1
        width: parent.width - 2 * Theme.spacingM
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.alpha(root.accentColor, 0.6) }
            GradientStop { position: 1; color: Theme.alpha(root.accentColor, 0.0) }
        }
        opacity: root.collapsed ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    ColumnLayout {
        id: body
        x: root.padding
        y: header.height + Theme.spacingXs
        width: root.width - 2 * root.padding
        spacing: root.contentSpacing
        opacity: root.collapsed ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
}
