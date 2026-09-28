// SDF shapes (rounded rectangles, circles, triangles, diamonds) with fill and border: handles,
// chips, markers, legend and tooltip panels. One quad per shape.
#version 440
layout(location = 0) in vec4 vertexCoord;   // quad corner, item px
layout(location = 1) in vec2 localCoord;    // relative to the shape center, px
layout(location = 2) in vec2 halfSize;      // half extents, px
layout(location = 3) in vec4 style;         // (corner radius, border width, kind, unused)
layout(location = 4) in vec4 fillColor;     // premultiplied
layout(location = 5) in vec4 borderColor;   // premultiplied

layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec2 vHalf;
layout(location = 2) out vec4 vStyle;
layout(location = 3) out vec4 vFill;
layout(location = 4) out vec4 vBorder;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float aa;
};

void main()
{
    vLocal = localCoord;
    vHalf = halfSize;
    vStyle = style;
    vFill = fillColor;
    vBorder = borderColor;
    gl_Position = qt_Matrix * vertexCoord;
}
