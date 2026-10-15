#include "PainterRenderer.h"

#include "AdjustLut.h"
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
                              Qt::black, // GPU shader 也只向黑色混合;颜色参数 MVP 简化
                              document.vignetteMidpoint(), document.vignetteRoundness(),
                              document.vignetteFeather());
        canvas = flat;
    }

    // 渲染期全局调色(非破坏性后处理)
    if (!document.renderLut().isNull()) {
        QImage flat = canvas.convertToFormat(QImage::Format_ARGB32);
        adj::applyLutToImage(flat, document.renderLut());
        return flat;
    }
    return canvas;
}
