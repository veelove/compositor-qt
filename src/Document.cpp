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

void Document::setVignette(double amount, double midpoint, double roundness, double feather)
{
    if (qFuzzyCompare(m_vignette.amount, amount)
        && qFuzzyCompare(m_vignette.midpoint, midpoint)
        && qFuzzyCompare(m_vignette.roundness, roundness)
        && qFuzzyCompare(m_vignette.feather, feather))
        return;
    m_vignette.amount = amount;
    m_vignette.midpoint = midpoint;
    m_vignette.roundness = roundness;
    m_vignette.feather = feather;
    emit contentChanged();
}

void Document::clearVignette()
{
    if (qFuzzyIsNull(m_vignette.amount))
        return;
    m_vignette.amount = 0.0;
    emit contentChanged();
}

void Document::setMotionBlur(double distance, double angle)
{
    if (qFuzzyCompare(m_motionBlur.distance, distance)
        && qFuzzyCompare(m_motionBlur.angle, angle))
        return;
    m_motionBlur.distance = distance;
    m_motionBlur.angle = angle;
    emit contentChanged();
}

void Document::clearMotionBlur()
{
    if (qFuzzyIsNull(m_motionBlur.distance))
        return;
    m_motionBlur.distance = 0.0;
    emit contentChanged();
}

void Document::setAddNoise(double amount, bool gaussian, bool monochromatic, quint32 seed)
{
    if (qFuzzyCompare(m_noise.amount, amount)
        && m_noise.gaussian == gaussian
        && m_noise.monochromatic == monochromatic
        && m_noise.seed == seed)
        return;
    m_noise.amount = amount;
    m_noise.gaussian = gaussian;
    m_noise.monochromatic = monochromatic;
    m_noise.seed = seed;
    emit contentChanged();
}

void Document::clearAddNoise()
{
    if (qFuzzyIsNull(m_noise.amount))
        return;
    m_noise.amount = 0.0;
    emit contentChanged();
}

void Document::setSize(const QSize &size)
{
    if (m_size == size)
        return;
    m_size = size;
    emit contentChanged();
}
