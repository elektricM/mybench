#include "testgeometryitem.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

TestGeometryItem::TestGeometryItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void TestGeometryItem::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    emit colorChanged();
    update();
}

QSGNode *TestGeometryItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGGeometryNode *>(oldNode);
    if (!node) {
        node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 3);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        auto *material = new QSGFlatColorMaterial;
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    }
    QSGGeometry::Point2D *v = node->geometry()->vertexDataAsPoint2D();
    const float w = float(width()), h = float(height());
    v[0].set(w * 0.5f, 0.f);
    v[1].set(w, h);
    v[2].set(0.f, h);
    node->markDirty(QSGNode::DirtyGeometry);
    static_cast<QSGFlatColorMaterial *>(node->material())->setColor(m_color);
    node->markDirty(QSGNode::DirtyMaterial);
    return node;
}
