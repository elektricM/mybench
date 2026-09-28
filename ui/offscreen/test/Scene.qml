// Offscreen self-test scene: text, Shapes, MultiEffect and a custom QSGGeometryNode.
// Pixel positions are checked by main.cpp (--self-check); keep them in sync.
import QtQuick
import QtQuick.Shapes
import QtQuick.Effects
import MyBench.OffscreenTest

Rectangle {
    id: root
    width: 640
    height: 400
    color: "#101214"

    Text {
        x: 20; y: 16
        text: "Offscreen ✓ — text, Shapes, MultiEffect, QSGGeometryNode"
        color: "#e8eaed"
        font.pixelSize: 18
    }

    // Shapes: filled green circle centered at (130, 220), radius 80.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "#00c853"
            strokeColor: "#b9f6ca"
            strokeWidth: 3
            PathAngleArc { centerX: 130; centerY: 220; radiusX: 80; radiusY: 80; startAngle: 0; sweepAngle: 360 }
        }
    }

    // MultiEffect: blue square (270..370, 170..270) with a large yellow shadow/glow.
    Rectangle {
        id: source
        x: 270; y: 170; width: 100; height: 100
        color: "#2979ff"
        radius: 8
        visible: false
    }
    MultiEffect {
        source: source
        anchors.fill: source
        shadowEnabled: true
        shadowColor: "#ffd600"
        shadowBlur: 1.0
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
        paddingRect: Qt.rect(40, 40, 180, 180)
        autoPaddingEnabled: false
    }

    // Custom geometry node: red triangle in (440..600, 140..300).
    TestGeometryItem {
        x: 440; y: 140; width: 160; height: 160
        color: "#ff1744"
    }

    // Animated item: after the --delay it must have moved (checks that time runs offscreen).
    Rectangle {
        id: mover
        y: 350; width: 20; height: 20
        color: "white"
        NumberAnimation on x { from: 0; to: 600; duration: 150; running: true }
    }
}
