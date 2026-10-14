#include "Document.h"
#include "Layer.h"

#include <QtGlobal>

Document::Document(const QSize &size, QObject *parent)
    : QObject(parent)
    , m_size(size)
{
}

Layer *Document::layerAt(int index) const
{
    return (index >= 0 && index < m_layers.size()) ? m_layers.at(index) : nullptr;
}

int Document::indexOf(Layer *layer) const
{
    return m_layers.indexOf(layer);
}

void Document::addLayer(Layer *layer, int index)
{
    if (!layer || m_layers.contains(layer))
        return;
    if (index < 0 || index > m_layers.size())
        index = m_layers.size();
    m_layers.insert(index, layer);
    layer->setParent(this);
    connect(layer, &Layer::changed, this, &Document::contentChanged);
    if (!m_activeLayer)
        m_activeLayer = layer;
    emit structureChanged();
    emit contentChanged();
}

Layer *Document::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size())
        return nullptr;
    Layer *layer = m_layers.takeAt(index);
    disconnect(layer, nullptr, this, nullptr);
    if (m_activeLayer == layer)
        m_activeLayer = m_layers.isEmpty() ? nullptr : m_layers.last();
    emit structureChanged();
    emit contentChanged();
    return layer;
}

void Document::removeLayer(Layer *layer)
{
    const int index = indexOf(layer);
    if (index >= 0)
        removeLayer(index);
}

void Document::setActiveLayer(Layer *layer)
{
    if (m_activeLayer == layer)
        return;
    m_activeLayer = layer;
    emit activeLayerChanged();
}

void Document::setRenderLut(const adj::AdjustLut &lut)
{
    if (m_renderLut.isNull() == lut.isNull()
        && (m_renderLut.isNull() || m_renderLut.data == lut.data))
        return;
    m_renderLut = lut;
    emit contentChanged();
}

void Document::setGaussianBlurRadius(double radius)
{
    if (qFuzzyCompare(m_gaussianBlurRadius, radius))
        return;
    m_gaussianBlurRadius = radius;
    emit contentChanged();
}

void Document::setSize(const QSize &size)
{
    if (m_size == size)
        return;
    m_size = size;
    emit contentChanged();
}
