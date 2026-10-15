#include "PainterRenderer.h"

#include "AdjustLut.h"
#include "Adjustments.h"
#include "Document.h"
#include "Filters.h"
#include "Layer.h"

#include <QPainter>

QImage PainterRenderer::render(const Document &document) const
{
    const QSize size = document.size();
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    for (Layer *layer : document.layers()) {
        if (!layer->isVisible() || layer->opacity() <= 0.0)
            continue;
        p.setOpacity(layer->opacity());
        p.drawImage(layer->offset(), layer->pixels());
        p.setOpacity(1.0);
    }
    p.end();

    // 渲染期高斯模糊(CPU fallback,GpuRenderer 也有等价 GPU path)
    if (document.gaussianBlurRadius() > 0.0) {
        QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
        flat = filt::gaussianBlur(flat, document.gaussianBlurRadius());
        canvas = flat;
    }

    // 渲染期动感模糊(CPU fallback)
    if (document.motionBlurDistance() > 0.0) {
        QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
        flat = filt::motionBlur(flat, document.motionBlurDistance(), document.motionBlurAngle());
        canvas = flat;
    }

    // 渲染期暗角(CPU fallback)
    if (document.vignetteAmount() > 0.0) {
        QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
        flat = filt::vignette(flat, document.vignetteAmount(),
                              Qt::black,
                              document.vignetteMidpoint(), document.vignetteRoundness(),
                              document.vignetteFeather());
        canvas = flat;
    }

    // 渲染期杂色(CPU fallback)
    if (document.noiseAmount() > 0.0) {
        QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
        flat = filt::addNoise(flat, document.noiseAmount(),
                              document.noiseGaussian(), document.noiseMonochromatic(),
                              document.noiseSeed());
        canvas = flat;
    }

    // 渲染期 HSV/黑白/色彩平衡(CPU fallback,跨通道计算)
    {
        const adj::HsvParams &hp = document.hsv();
        const bool hasHsv = hp.hue != 0.0 || hp.saturation != 0.0 || hp.lightness != 0.0 || hp.colorize;
        if (hasHsv) {
            QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
            adj::applyHsv(flat, hp);
            canvas = flat;
        }
    }
    {
        const adj::BwParams &bp = document.blackWhite();
        const adj::BwParams def;
        const bool hasBw = bp.reds != def.reds || bp.yellows != def.yellows
                        || bp.greens != def.greens || bp.cyans != def.cyans
                        || bp.blues != def.blues || bp.magentas != def.magentas
                        || bp.tint;
        if (hasBw) {
            QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
            adj::applyBlackWhite(flat, bp);
            canvas = flat;
        }
    }
    {
        const adj::CbParams &cb = document.colorBalance();
        const adj::CbParams def;
        const bool hasCb = cb.shadowCyanRed != def.shadowCyanRed
                        || cb.shadowMagentaGreen != def.shadowMagentaGreen
                        || cb.shadowYellowBlue != def.shadowYellowBlue
                        || cb.midCyanRed != def.midCyanRed
                        || cb.midMagentaGreen != def.midMagentaGreen
                        || cb.midYellowBlue != def.midYellowBlue
                        || cb.highlightCyanRed != def.highlightCyanRed
                        || cb.highlightMagentaGreen != def.highlightMagentaGreen
                        || cb.highlightYellowBlue != def.highlightYellowBlue;
        if (hasCb) {
            QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
            adj::applyColorBalance(flat, cb);
            canvas = flat;
        }
    }

    // 渲染期全局调色(非破坏性后处理)
    if (!document.renderLut().isNull()) {
        QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
        adj::applyLutToImage(flat, document.renderLut());
        return flat;
    }
    return canvas;
}
