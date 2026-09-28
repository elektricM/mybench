import QtQuick
import QtQuick.Templates as T

// Keyboard routing rules of the shell (app.md 6.2).
QtObject {
    // Space runs/stops the focused instrument, except when the focused control uses Space itself
    // (buttons, toggles, checkboxes, segmented controls, combo boxes, sliders, spin boxes, text).
    function spaceAllowed(item: Item): bool {
        if (!item)
            return true
        return !(item instanceof T.AbstractButton || item instanceof T.ComboBox || item instanceof T.Slider
                 || item instanceof T.SpinBox || item instanceof TextInput || item instanceof TextEdit)
    }
}
