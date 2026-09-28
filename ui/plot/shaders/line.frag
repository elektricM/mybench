#version 440
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec3 vSeg;
layout(location = 2) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float aa;
    float dashOn;
    float dashOff;
};

void main()
{
    float len = vSeg.x;
    float hw = vSeg.z;
    float dx = max(0.0, max(-vLocal.x, vLocal.x - len));
    float d = length(vec2(dx, vLocal.y));
    // Lines thinner than a device pixel keep a 1 px footprint and fade instead (hairlines).
    float w = max(hw, 0.5 * aa);
    float alpha = clamp((w - d) / aa + 0.5, 0.0, 1.0) * (hw / w);
    if (dashOn > 0.0) {
        float s = vSeg.y + clamp(vLocal.x, 0.0, len);
        float m = mod(s, dashOn + dashOff);
        float outside = abs(m - 0.5 * dashOn) - 0.5 * dashOn;   // < 0 inside a dash
        alpha *= clamp(0.5 - outside / aa, 0.0, 1.0);
    }
    fragColor = vColor * (alpha * qt_Opacity);
}
