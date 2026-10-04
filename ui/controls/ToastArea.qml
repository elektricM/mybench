pragma ComponentBehavior: Bound
import QtQuick
import MyBench.Framework

// Stack of toasts (the Notifications model), newest at the bottom (docs/UI.md §10: global
// transient events only). Anchor it to the bottom-right of the window content; it only takes the
// space of its toasts.
//     ToastArea { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16 }
ListView {
    id: list

    width: 360
    height: Math.min(contentHeight, 600)
    interactive: false
    spacing: Theme.spacingS
    verticalLayoutDirection: ListView.BottomToTop
    model: Notifications
    z: 1000

    delegate: Toast {
        id: toast
        required toastId
        required level
        required title
        required message
        required actionText
        required repeat
        width: list.width
        onDismissRequested: Notifications.dismiss(toast.toastId)
        onActionRequested: Notifications.trigger(toast.toastId)
    }

    // `Details` of an error toast: the shell opens the event log (ShellActions.eventLogRequested).
    Connections {
        target: Notifications
        function onDetailsRequested() { ShellActions.eventLogRequested() }
    }

    add: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal }
            NumberAnimation { property: "x"; from: 24; to: 0; duration: Theme.durationNormal; easing.type: Theme.easing }
        }
    }
    remove: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
        }
    }
    displaced: Transition {
        NumberAnimation { properties: "y"; duration: Theme.durationNormal; easing.type: Theme.easing }
    }
}
