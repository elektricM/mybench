import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Scrollable column of Sections on the right of an instrument panel.
//     Inspector { Section { title: "Timebase" } Section { title: "Trigger" } }
Rectangle {
    id: root

    default property alias sections: column.data
    property int padding: Theme.spacingM
    property int spacing: Theme.spacingS + 2
    property alias contentY: flick.contentY

    implicitWidth: Theme.inspectorWidth
    color: Theme.surface

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * root.padding
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        WheelScroll { flickable: flick }

        ColumnLayout {
            id: column
            x: root.padding
            y: root.padding
            width: flick.width - 2 * root.padding
            spacing: root.spacing
        }
    }

    // soft fade under the scrolled content at the top
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 10
        visible: flick.contentY > 2
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.alpha(Theme.shadow, 0.35) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
}
