// Custom scene-graph item for the offscreen self-test: a triangle drawn with a QSGGeometryNode.
#pragma once

#include <QColor>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class TestGeometryItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit TestGeometryItem(QQuickItem *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor &color);

signals:
    void colorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    QColor m_color = Qt::red;
};
