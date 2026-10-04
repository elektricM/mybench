import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// A flat disclosure group of an Inspector (docs/UI.md §13.2): a header (chevron, optional channel
// swatch, Title Case title, the summary while collapsed, trailing controls) over a column of
// FormRows. No background or border of its own; sections are separated by a hairline.
//     Section {
//         sectionId: "channel0"; title: qsTr("Channel 0"); swatchColor: Theme.signalCh0
//         summary: "AI 0 · 1 V/div"
//         trailing: Switch { checked: ctl.ch0Enabled; onToggled: ctl.ch0Enabled = checked }
//         FormRow { label: qsTr("Source"); ComboBox { Layout.fillWidth: true } }
//     }
Item {
    id: root

    // Stable, untranslated id ("channel0", "trigger"); InstrumentPanel keys the collapsed state
    // with it (by the title when empty).
    property string sectionId
    property string title
    property string summary                     // shown on the right while collapsed
    property color swatchColor: "transparent"   // channel swatch before the title
    property bool collapsible: true
    property bool collapsed: false
    // Set by InstrumentPanel.keySections() to "section/<paneId>/<sectionId>"; the collapsed state
    // is kept in the session's view state under exactly this key.
    property string stateKey
    default property alias content: body.data
    property alias trailing: tools.data
    readonly property bool hasSwatch: swatchColor.a > 0

    // --- v1 names (aliases; `iconName`, `contentSpacing` and `padding` are ignored) ------------
    property alias accentColor: root.swatchColor
    property alias headerTools: tools.data
    readonly property bool hasAccent: hasSwatch
    property string iconName
    property int contentSpacing: Theme.formRowSpacing
    property int padding: Theme.spacingM

    function toggle() {
        if (!collapsible)
            return
        collapsed = !collapsed
        if (stateKey.length > 0)
            SessionManager.setViewValue(stateKey, collapsed)
    }
    function restoreState() {
        if (stateKey.length === 0)
            return
        const stored = SessionManager.viewValue(stateKey)
        if (stored !== undefined && stored !== null && Boolean(stored) !== collapsed)
            collapsed = Boolean(stored)
    }
    onStateKeyChanged: restoreState()
    Connections {
        target: SessionManager
        enabled: root.stateKey.length > 0
        function onViewStateChanged() { root.restoreState() }
    }

    readonly property real expandedHeight: header.height + body.implicitHeight + Theme.spacingM
    Layout.fillWidth: true
    implicitWidth: Math.max(Theme.inspectorMinWidth, body.implicitWidth + 2 * Theme.spacingM)
    implicitHeight: (collapsed ? header.height : expandedHeight) + Theme.borderWidth
    clip: heightAnimation.running
    Behavior on implicitHeight {
        enabled: Theme.animations && root.visible
        NumberAnimation { id: heightAnimation; duration: Theme.durationNormal; easing.type: Theme.easing }
    }

    // Header: the whole row toggles; trailing controls sit on top and take their own clicks.
    T.AbstractButton {
        id: header
        width: root.width
        height: Theme.controlHeight + Theme.spacingS
        enabled: root.collapsible
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: root.toggle()
        Accessible.role: Accessible.Button
        Accessible.name: root.title
        Accessible.description: root.collapsed ? qsTr("Collapsed") : qsTr("Expanded")
        Keys.onLeftPressed: if (!root.collapsed) root.toggle()
        Keys.onRightPressed: if (root.collapsed) root.toggle()

        background: FocusRing {
            anchors.margins: 2
            radius: Theme.radiusControl
            shown: header.visualFocus
        }
        contentItem: Item {}
    }

    RowLayout {
        id: headerRow
        x: Theme.spacingS
        width: root.width - Theme.spacingS - Theme.spacingM
        height: header.height
        spacing: Theme.spacingXs + 2

        Icon {
            visible: root.collapsible
            Layout.preferredWidth: 12
            name: "chevron-down"
            size: 12
            color: header.hovered ? Theme.text : Theme.textTertiary
            rotation: root.collapsed ? -90 : 0
            Behavior on rotation {
                enabled: Theme.animations
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }
        Item {
            visible: !root.collapsible
            Layout.preferredWidth: Theme.spacingXs
        }
        Rectangle {
            visible: root.hasSwatch
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.spacingXs / 2
            implicitWidth: 10
            implicitHeight: 10
            radius: 2
            color: root.swatchColor
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: root.hasSwatch ? 2 : 0
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontSizeHeading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            visible: root.collapsed && root.summary.length > 0
            Layout.maximumWidth: root.width * 0.5
            text: root.summary
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            font.features: Theme.tabularFigures
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
        }
        Row {
            id: tools
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingS
        }
    }

    ColumnLayout {
        id: body
        x: Theme.spacingM
        y: header.height
        width: root.width - 2 * Theme.spacingM
        spacing: Theme.formRowSpacing
        visible: !root.collapsed || heightAnimation.running
    }

    // Hairline under the section.
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.separator
    }
}
