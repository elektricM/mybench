import QtQuick
import QtQuick.Layouts

// v1 container of side-by-side Fields (alias, docs/UI.md §13.3): it now stacks its Fields vertically
// like the other rows of the form.
//     FieldRow { Field { label: "Probe" ... } Field { label: "Coupling" ... } }
ColumnLayout {
    Layout.fillWidth: true
    spacing: Theme.formRowSpacing
}
