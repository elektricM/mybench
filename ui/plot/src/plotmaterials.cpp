#include "plotmaterials.h"

#include <QSGMaterialShader>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cstring>

namespace mybench::plot {

quint32 packColor(const QColor &color, float opacity)
{
    const float a = float(color.alphaF()) * std::clamp(opacity, 0.0f, 1.0f);
    const auto channel = [a](float v) { return quint32(std::clamp(v * a, 0.0f, 1.0f) * 255.0f + 0.5f); };
    const quint32 r = channel(float(color.redF()));
    const quint32 g = channel(float(color.greenF()));
    const quint32 b = channel(float(color.blueF()));
    const quint32 al = quint32(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
    return r | (g << 8) | (b << 16) | (al << 24);
}

// ---------------------------------------------------------------------------------------------
// Line material
// ---------------------------------------------------------------------------------------------

const QSGGeometry::AttributeSet &lineAttributes()
{
    static const QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 3, QSGGeometry::FloatType, QSGGeometry::TexCoord1Attribute),
        QSGGeometry::Attribute::createWithAttributeType(3, 4, QSGGeometry::UnsignedByteType, QSGGeometry::ColorAttribute),
    };
    static const QSGGeometry::AttributeSet set = {4, int(sizeof(LineVertex)), attributes};
    return set;
}

namespace {

class LineShader : public QSGMaterialShader
{
public:
    LineShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/mybench/plot/shaders/line.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/mybench/plot/shaders/line.frag.qsb"));
        setFlag(UpdatesGraphicsPipelineState);
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= 88);
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data(), m.constData(), 64);
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buf->data() + 64, &opacity, 4);
        }
        const auto *material = static_cast<LineMaterial *>(newMaterial);
        const float values[5] = {1.0f / std::max(0.25f, state.devicePixelRatio()), material->dashOn,
                                 material->dashOff, material->dotLength, material->dots};
        std::memcpy(buf->data() + 68, values, sizeof(values));
        return true;
    }

    bool updateGraphicsPipelineState(RenderState &, GraphicsPipelineState *ps, QSGMaterial *newMaterial,
                                     QSGMaterial *) override
    {
        if (!static_cast<LineMaterial *>(newMaterial)->additive)
            return false;
        ps->blendEnable = true;
        ps->srcColor = GraphicsPipelineState::One;
        ps->dstColor = GraphicsPipelineState::One;
        return true;
    }
};

class ShapeShader : public QSGMaterialShader
{
public:
    ShapeShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/mybench/plot/shaders/shape.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/mybench/plot/shaders/shape.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *, QSGMaterial *) override
    {
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= 72);
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data(), m.constData(), 64);
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buf->data() + 64, &opacity, 4);
        }
        const float aa = 1.0f / std::max(0.25f, state.devicePixelRatio());
        std::memcpy(buf->data() + 68, &aa, 4);
        return true;
    }
};

}  // namespace

LineMaterial::LineMaterial()
{
    setFlag(Blending);
}

QSGMaterialType *LineMaterial::type() const
{
    static QSGMaterialType type;
    return &type;
}

QSGMaterialShader *LineMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new LineShader;
}

int LineMaterial::compare(const QSGMaterial *other) const
{
    const auto *o = static_cast<const LineMaterial *>(other);
    if (dashOn != o->dashOn)
        return dashOn < o->dashOn ? -1 : 1;
    if (dashOff != o->dashOff)
        return dashOff < o->dashOff ? -1 : 1;
    if (dotLength != o->dotLength)
        return dotLength < o->dotLength ? -1 : 1;
    if (dots != o->dots)
        return dots < o->dots ? -1 : 1;
    if (additive != o->additive)
        return additive ? 1 : -1;
    return 0;
}

DashPattern dashPattern(int style, float width)
{
    const float w = std::max(1.0f, width);
    DashPattern p;
    switch (style) {
    case 1:  // Dash
        p.on = 5.0f * w;
        p.off = 3.5f * w;
        break;
    case 2:  // Dot
        p.on = 1.0f * w;
        p.off = 2.5f * w;
        break;
    case 3:  // DashDot
    case 4:  // DashDotDot
        p.on = 6.0f * w;
        p.off = 2.5f * w;
        p.dotLength = 1.0f * w;
        p.dots = style == 3 ? 1.0f : 2.0f;
        break;
    default:
        break;
    }
    return p;
}

void dashPattern(int style, float width, float *on, float *off)
{
    const DashPattern p = dashPattern(style, width);
    *on = p.on;
    *off = p.off;
}

namespace {

// Writes the 4 vertices and 6 indices of one segment quad.
inline void writeSegment(LineVertex *v, quint32 *idx, quint32 base, float x0, float y0, float x1, float y1,
                         float halfWidth, quint32 color, float arc, float aa)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    float ux = 1.0f, uy = 0.0f;
    if (len > 1e-6f) {
        ux = dx / len;
        uy = dy / len;
    }
    const float nx = -uy, ny = ux;
    const float w = std::max(halfWidth, 0.5f * aa);
    const float e = w + aa;
    v[0] = {x0 - ux * e - nx * e, y0 - uy * e - ny * e, -e, -e, len, arc, halfWidth, color};
    v[1] = {x0 - ux * e + nx * e, y0 - uy * e + ny * e, -e, e, len, arc, halfWidth, color};
    v[2] = {x1 + ux * e + nx * e, y1 + uy * e + ny * e, len + e, e, len, arc, halfWidth, color};
    v[3] = {x1 + ux * e - nx * e, y1 + uy * e - ny * e, len + e, -e, len, arc, halfWidth, color};
    idx[0] = base;
    idx[1] = base + 1;
    idx[2] = base + 2;
    idx[3] = base;
    idx[4] = base + 2;
    idx[5] = base + 3;
}

}  // namespace

void LineGeometry::segment(float x0, float y0, float x1, float y1, float halfWidth, quint32 color, float arc)
{
    const size_t vb = vertices.size(), ib = indices.size();
    vertices.resize(vb + 4);
    indices.resize(ib + 6);
    writeSegment(vertices.data() + vb, indices.data() + ib, quint32(vb), x0, y0, x1, y1, halfWidth, color, arc, aa);
}

float LineGeometry::polyline(const PixelPoint *points, size_t count, float halfWidth, quint32 color, float arc)
{
    // Count the quads first (segments between consecutive points, dots for isolated points).
    size_t quads = 0;
    for (size_t i = 0; i < count; ++i) {
        if (points[i].isGap())
            continue;
        const bool prev = i > 0 && !points[i - 1].isGap();
        const bool next = i + 1 < count && !points[i + 1].isGap();
        quads += prev ? 1 : (next ? 0 : 1);
    }
    const size_t vb = vertices.size(), ib = indices.size();
    vertices.resize(vb + quads * 4);
    indices.resize(ib + quads * 6);
    LineVertex *v = vertices.data() + vb;
    quint32 *idx = indices.data() + ib;
    quint32 base = quint32(vb);
    const float start = arc;
    for (size_t i = 0; i < count; ++i) {
        const PixelPoint &p = points[i];
        if (p.isGap())
            continue;
        const bool prev = i > 0 && !points[i - 1].isGap();
        if (prev) {
            const PixelPoint &q = points[i - 1];
            writeSegment(v, idx, base, q.x, q.y, p.x, p.y, halfWidth, color, arc, aa);
            const float dx = p.x - q.x, dy = p.y - q.y;
            arc += std::sqrt(dx * dx + dy * dy);
        } else if (i + 1 >= count || points[i + 1].isGap()) {
            writeSegment(v, idx, base, p.x, p.y, p.x, p.y, halfWidth, color, arc, aa);  // lone sample: a dot
        } else {
            continue;
        }
        v += 4;
        idx += 6;
        base += 4;
    }
    return arc - start;
}

void LineGeometry::append(const LineGeometry &other)
{
    const quint32 base = quint32(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    indices.reserve(indices.size() + other.indices.size());
    for (quint32 i : other.indices)
        indices.push_back(base + i);
}

// ---------------------------------------------------------------------------------------------
// Shape material
// ---------------------------------------------------------------------------------------------

const QSGGeometry::AttributeSet &shapeAttributes()
{
    static const QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 2, QSGGeometry::FloatType, QSGGeometry::TexCoord1Attribute),
        QSGGeometry::Attribute::createWithAttributeType(3, 4, QSGGeometry::FloatType, QSGGeometry::TexCoord2Attribute),
        QSGGeometry::Attribute::createWithAttributeType(4, 4, QSGGeometry::UnsignedByteType, QSGGeometry::ColorAttribute),
        QSGGeometry::Attribute::createWithAttributeType(5, 4, QSGGeometry::UnsignedByteType, QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {6, int(sizeof(ShapeVertex)), attributes};
    return set;
}

ShapeMaterial::ShapeMaterial()
{
    setFlag(Blending);
}

QSGMaterialType *ShapeMaterial::type() const
{
    static QSGMaterialType type;
    return &type;
}

QSGMaterialShader *ShapeMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new ShapeShader;
}

int ShapeMaterial::compare(const QSGMaterial *) const
{
    return 0;
}

void ShapeGeometry::shape(const QRectF &rect, ShapeKind kind, float radius, quint32 fill, float border,
                          quint32 stroke)
{
    const float cx = float(rect.center().x()), cy = float(rect.center().y());
    const float hw = float(rect.width() * 0.5), hh = float(rect.height() * 0.5);
    const float ex = hw + aa, ey = hh + aa;
    const float k = float(int(kind));
    const quint32 base = quint32(vertices.size());
    vertices.push_back({cx - ex, cy - ey, -ex, -ey, hw, hh, radius, border, k, 0, fill, stroke});
    vertices.push_back({cx + ex, cy - ey, ex, -ey, hw, hh, radius, border, k, 0, fill, stroke});
    vertices.push_back({cx + ex, cy + ey, ex, ey, hw, hh, radius, border, k, 0, fill, stroke});
    vertices.push_back({cx - ex, cy + ey, -ex, ey, hw, hh, radius, border, k, 0, fill, stroke});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// ---------------------------------------------------------------------------------------------
// Color geometry
// ---------------------------------------------------------------------------------------------

namespace {

void setColored(QSGGeometry::ColoredPoint2D &v, QPointF p, const QColor &c)
{
    const float a = float(c.alphaF());
    v.set(float(p.x()), float(p.y()), uchar(c.red() * a + 0.5f), uchar(c.green() * a + 0.5f),
          uchar(c.blue() * a + 0.5f), uchar(c.alpha()));
}

}  // namespace

void ColorGeometry::rect(const QRectF &r, const QColor &color)
{
    quad(r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), color, color);
}

void ColorGeometry::quad(QPointF topLeft, QPointF topRight, QPointF bottomRight, QPointF bottomLeft,
                         const QColor &top, const QColor &bottom)
{
    const quint32 base = quint32(vertices.size());
    vertices.resize(vertices.size() + 4);
    setColored(vertices[base], topLeft, top);
    setColored(vertices[base + 1], topRight, top);
    setColored(vertices[base + 2], bottomRight, bottom);
    setColored(vertices[base + 3], bottomLeft, bottom);
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// ---------------------------------------------------------------------------------------------
// Nodes
// ---------------------------------------------------------------------------------------------

namespace {

QSGGeometryNode *makeNode(const QSGGeometry::AttributeSet &attributes, QSGMaterial *material)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(attributes, 0, 0, QSGGeometry::UnsignedIntType);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
    geometry->setIndexDataPattern(QSGGeometry::StreamPattern);
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}

template <typename V>
void uploadRaw(QSGGeometryNode *node, const std::vector<V> &vertices, const std::vector<quint32> &indices)
{
    QSGGeometry *g = node->geometry();
    if (g->vertexCount() != int(vertices.size()) || g->indexCount() != int(indices.size()))
        g->allocate(int(vertices.size()), int(indices.size()));
    if (!vertices.empty())
        std::memcpy(g->vertexData(), vertices.data(), vertices.size() * sizeof(V));
    if (!indices.empty())
        std::memcpy(g->indexData(), indices.data(), indices.size() * sizeof(quint32));
    node->markDirty(QSGNode::DirtyGeometry);
}

}  // namespace

QSGGeometryNode *createLineNode(LineMaterial *material)
{
    return makeNode(lineAttributes(), material ? material : new LineMaterial);
}

QSGGeometryNode *createShapeNode()
{
    return makeNode(shapeAttributes(), new ShapeMaterial);
}

QSGGeometryNode *createColorNode()
{
    return makeNode(QSGGeometry::defaultAttributes_ColoredPoint2D(), new QSGVertexColorMaterial);
}

void upload(QSGGeometryNode *node, const LineGeometry &g)
{
    uploadRaw(node, g.vertices, g.indices);
}

void upload(QSGGeometryNode *node, const ShapeGeometry &g)
{
    uploadRaw(node, g.vertices, g.indices);
}

void upload(QSGGeometryNode *node, const ColorGeometry &g)
{
    uploadRaw(node, g.vertices, g.indices);
}

}  // namespace mybench::plot
