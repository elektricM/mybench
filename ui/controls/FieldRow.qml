import QtQuick
import QtQuick.Layouts

// Equal-width columns of Fields (two or three compact controls side by side).
//     FieldRow { Field { label: "Probe" ... } Field { label: "Coupling" ... } }
RowLayout {
    Layout.fillWidth: true
    spacing: Theme.spacingS + 2
    uniformCellSizes: true
}
