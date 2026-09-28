// Anti-aliased thick line segments: each segment is a quad (expanded on the CPU by half width +
// 1 px); the fragment shader computes the exact distance to the segment (round caps and joins).
#version 440
layout(location = 0) in vec4 vertexCoord;   // quad corner, item px
layout(location = 1) in vec2 localCoord;    // (along, across) relative to the segment start, px
layout(location = 2) in vec3 segInfo;       // (length, arc length at start, half width), px
layout(location = 3) in vec4 vertexColor;   // premultiplied

layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec3 vSeg;
layout(location = 2) out vec4 vColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float aa;        // item px per device px
    float dashOn;    // px, 0 = solid
    float dashOff;   // px
    float dotLength; // px, dots between dashes (dash-dot styles)
    float dots;      // 0, 1 or 2
};

void main()
{
    vLocal = localCoord;
    vSeg = segInfo;
    vColor = vertexColor;
    gl_Position = qt_Matrix * vertexCoord;
}
