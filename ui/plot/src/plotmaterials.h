// Scene-graph materials and geometry builders of the plot (private to MyBench.Plot).
#pragma once

#include "mybench/plot/decimation.h"

#include <QColor>
#include <QRectF>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>

#include <vector>

namespace mybench::plot {

// Premultiplied RGBA8 in memory order r, g, b, a.
quint32 packColor(const QColor &color, float opacity = 1.0f);

// ---- lines ------------------------------------------------------------------------------------

struct LineVertex {
    float x, y;            // quad corner (item px)
    float along, across;   // local segment coordinates (px)
    float length, arc, halfWidth;
    quint32 color;
};
static_assert(sizeof(LineVertex) == 32);

const QSGGeometry::AttributeSet &lineAttributes();

class LineMaterial : public QSGMaterial
{
public:
    LineMaterial();
    QSGMaterialType *type() const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override;

    float dashOn = 0.0f;    // px; 0 = solid
    float dashOff = 0.0f;
    bool additive = false;  // additive blending (persistence glow)
};

// Dash pattern (on, off) in px for a style and width.
void dashPattern(int style, float width, float *on, float *off);

// Geometry accumulator for LineMaterial (indexed triangles, 4 vertices per segment).
struct LineGeometry {
    std::vector<LineVertex> vertices;
    std::vector<quint32> indices;
    float aa = 1.0f;       // item px per device px

    void clear()
    {
        vertices.clear();
        indices.clear();
    }
    bool empty() const { return vertices.empty(); }
    // One segment; `arc` is the dash phase at p0. Zero-length segments draw a dot.
    void segment(float x0, float y0, float x1, float y1, float halfWidth, quint32 color, float arc = 0.0f);
    // Connected segments (gaps in `points` restart the line). Returns the total length.
    float polyline(const PixelPoint *points, size_t count, float halfWidth, quint32 color, float arc = 0.0f);
    void append(const LineGeometry &other);
};

// ---- shapes -----------------------------------------------------------------------------------

enum class ShapeKind { Box = 0, TriangleRight = 1, TriangleLeft = 2, TriangleDown = 3, TriangleUp = 4, Diamond = 5 };

struct ShapeVertex {
    float x, y;
    float localX, localY;
    float halfW, halfH;
    float radius, border, kind, unused;
    quint32 fill;
    quint32 stroke;
};
static_assert(sizeof(ShapeVertex) == 48);

const QSGGeometry::AttributeSet &shapeAttributes();

class ShapeMaterial : public QSGMaterial
{
public:
    ShapeMaterial();
    QSGMaterialType *type() const override;
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override;
};

struct ShapeGeometry {
    std::vector<ShapeVertex> vertices;
    std::vector<quint32> indices;
    float aa = 1.0f;

    void clear()
    {
        vertices.clear();
        indices.clear();
    }
    bool empty() const { return vertices.empty(); }
    void shape(const QRectF &rect, ShapeKind kind, float radius, quint32 fill, float border = 0.0f,
               quint32 stroke = 0);
};

// ---- flat colored triangles (fills, regions) ----------------------------------------------------

struct ColorGeometry {
    std::vector<QSGGeometry::ColoredPoint2D> vertices;
    std::vector<quint32> indices;
    void clear()
    {
        vertices.clear();
        indices.clear();
    }
    bool empty() const { return vertices.empty(); }
    void rect(const QRectF &r, const QColor &color);
    // Quad with top and bottom colors (premultiplied inside).
    void quad(QPointF topLeft, QPointF topRight, QPointF bottomRight, QPointF bottomLeft,
              const QColor &top, const QColor &bottom);
};

// ---- node helpers --------------------------------------------------------------------------------

QSGGeometryNode *createLineNode(LineMaterial *material = nullptr);
QSGGeometryNode *createShapeNode();
QSGGeometryNode *createColorNode();
void upload(QSGGeometryNode *node, const LineGeometry &g);
void upload(QSGGeometryNode *node, const ShapeGeometry &g);
void upload(QSGGeometryNode *node, const ColorGeometry &g);

}  // namespace mybench::plot
