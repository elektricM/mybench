import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import MyBench.Controls

T.BusyIndicator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2

    contentItem: Item {
        implicitWidth: 20
        implicitHeight: 20
        visible: control.running
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            RotationAnimator on rotation {
                running: control.running && control.visible
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
            }
            ShapePath {
                strokeColor: Theme.accent
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: control.contentItem.width / 2
                    centerY: control.contentItem.height / 2
                    radiusX: control.contentItem.width / 2 - 2
                    radiusY: control.contentItem.height / 2 - 2
                    startAngle: 0
                    sweepAngle: 270
                }
            }
        }
    }
}
