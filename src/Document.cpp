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

void Document::setHsv(const adj::HsvParams &p)
{
    if (qFuzzyCompare(m_hsv.hue, p.hue)
        && qFuzzyCompare(m_hsv.saturation, p.saturation)
        && qFuzzyCompare(m_hsv.lightness, p.lightness)
        && m_hsv.colorize == p.colorize)
        return;
    m_hsv = p;
    emit contentChanged();
}

void Document::clearHsv()
{
    if (qFuzzyIsNull(m_hsv.hue) && qFuzzyIsNull(m_hsv.saturation)
        && qFuzzyIsNull(m_hsv.lightness) && !m_hsv.colorize)
        return;
    m_hsv = adj::HsvParams{};
    emit contentChanged();
}

void Document::setBlackWhite(const adj::BwParams &p)
{
    const bool sameTint = m_bw.tint == p.tint
        && m_bw.tintHue == p.tintHue
        && m_bw.tintSaturation == p.tintSaturation;
    if (m_bw.reds == p.reds && m_bw.yellows == p.yellows
        && m_bw.greens == p.greens && m_bw.cyans == p.cyans
        && m_bw.blues == p.blues && m_bw.magentas == p.magentas && sameTint)
        return;
    m_bw = p;
    emit contentChanged();
}

void Document::clearBlackWhite()
{
    const adj::BwParams def;
    const bool sameTint = m_bw.tint == def.tint
        && m_bw.tintHue == def.tintHue
        && m_bw.tintSaturation == def.tintSaturation;
    if (m_bw.reds == def.reds && m_bw.yellows == def.yellows
        && m_bw.greens == def.greens && m_bw.cyans == def.cyans
        && m_bw.blues == def.blues && m_bw.magentas == def.magentas && sameTint)
        return;
    m_bw = def;
    emit contentChanged();
}

void Document::setColorBalance(const adj::CbParams &p)
{
    if (m_cb.shadowCyanRed == p.shadowCyanRed
        && m_cb.shadowMagentaGreen == p.shadowMagentaGreen
        && m_cb.shadowYellowBlue == p.shadowYellowBlue
        && m_cb.midCyanRed == p.midCyanRed
        && m_cb.midMagentaGreen == p.midMagentaGreen
        && m_cb.midYellowBlue == p.midYellowBlue
        && m_cb.highlightCyanRed == p.highlightCyanRed
        && m_cb.highlightMagentaGreen == p.highlightMagentaGreen
        && m_cb.highlightYellowBlue == p.highlightYellowBlue
        && m_cb.preserveLuminosity == p.preserveLuminosity)
        return;
    m_cb = p;
    emit contentChanged();
}

void Document::clearColorBalance()
{
    const adj::CbParams def;
    if (m_cb.shadowCyanRed == def.shadowCyanRed
        && m_cb.shadowMagentaGreen == def.shadowMagentaGreen
        && m_cb.shadowYellowBlue == def.shadowYellowBlue
        && m_cb.midCyanRed == def.midCyanRed
        && m_cb.midMagentaGreen == def.midMagentaGreen
        && m_cb.midYellowBlue == def.midYellowBlue
        && m_cb.highlightCyanRed == def.highlightCyanRed
        && m_cb.highlightMagentaGreen == def.highlightMagentaGreen
        && m_cb.highlightYellowBlue == def.highlightYellowBlue
        && m_cb.preserveLuminosity == def.preserveLuminosity)
        return;
    m_cb = def;
    emit contentChanged();
}

void Document::setSize(const QSize &size)
{
    if (m_size == size)
        return;
    m_size = size;
    emit contentChanged();
}
