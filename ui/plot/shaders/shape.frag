#version 440
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec2 vHalf;
layout(location = 2) in vec4 vStyle;
layout(location = 3) in vec4 vFill;
layout(location = 4) in vec4 vBorder;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float aa;
};

float roundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// Triangle with its tip at +x: vertices (-b.x, -b.y), (-b.x, b.y), (b.x, 0).
float triangleRight(vec2 p, vec2 b)
{
    vec2 n1 = normalize(vec2(b.y, 2.0 * b.x));
    vec2 n2 = normalize(vec2(b.y, -2.0 * b.x));
    vec2 q = p - vec2(b.x, 0.0);
    return max(-b.x - p.x, max(dot(q, n1), dot(q, n2)));
}

void main()
{
    float kind = vStyle.z;
    vec2 p = vLocal;
    vec2 b = vHalf;
    float d;
    if (kind < 0.5) {                       // 0: rounded rectangle (circle when r = half size)
        d = roundBox(p, b, min(vStyle.x, min(b.x, b.y)));
    } else if (kind < 1.5) {                // 1: triangle pointing right
        d = triangleRight(p, b);
    } else if (kind < 2.5) {                // 2: left
        d = triangleRight(vec2(-p.x, p.y), b);
    } else if (kind < 3.5) {                // 3: down
        d = triangleRight(vec2(p.y, p.x), vec2(b.y, b.x));
    } else if (kind < 4.5) {                // 4: up
        d = triangleRight(vec2(-p.y, p.x), vec2(b.y, b.x));
    } else if (kind < 5.5) {                // 5: diamond
        vec2 q = abs(p) / b;
        d = (q.x + q.y - 1.0) * min(b.x, b.y) * 0.7071;
    } else {                                // 6: plus, 7: x (arm half-thickness = style.x)
        if (kind > 6.5)
            p = vec2(p.x + p.y, p.y - p.x) * 0.7071;
        float t = vStyle.x;
        d = min(roundBox(p, vec2(b.x, t), 0.0), roundBox(p, vec2(t, b.y), 0.0));
    }
    float fill = clamp(0.5 - d / aa, 0.0, 1.0);
    float bw = vStyle.y;
    vec4 color = vFill;
    if (bw > 0.0) {
        float inner = clamp(0.5 - (d + bw) / aa, 0.0, 1.0);
        color = mix(vBorder, vFill, inner);
    }
    fragColor = color * (fill * qt_Opacity);
}
