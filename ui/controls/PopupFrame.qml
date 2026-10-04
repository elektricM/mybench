import QtQuick

// The surface of every popup, menu, dialog and toast (docs/UI.md §4): `base` fill, a hairline
// border, Theme.radiusPopup and the one popup shadow. The shadow is drawn only for in-window
// popups (`shadow`); popup windows and native menus get the platform's own.
//     background: PopupFrame { shadow: control.popupType === Popup.Item }
Rectangle {
    id: frame

    property bool shadow: true

    color: Theme.base
    radius: Theme.radiusPopup
    border.width: Theme.borderWidth
    border.color: Theme.dark ? Theme.alpha(Theme.text, 0.2) : Theme.alpha(Theme.text, 0.14)

    layer.enabled: shadow
    layer.effect: PopupShadow {}
}
