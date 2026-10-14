#include "PainterRenderer.h"

#include "Document.h"
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
    return canvas;
}
