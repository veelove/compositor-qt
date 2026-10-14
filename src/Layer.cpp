#include "Layer.h"

#include <QtGlobal>

Layer::Layer(const QString &name, const QSize &size, const QColor &fill, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(name)
{
    m_pixels = QImage(size, QImage::Format_ARGB32_Premultiplied);
    m_pixels.fill(fill);
}

void Layer::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    emit changed();
}

void Layer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    emit changed();
}

void Layer::setOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_opacity, opacity))
        return;
    m_opacity = opacity;
    emit changed();
}
