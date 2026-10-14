#pragma once

#include "Renderer.h"

// QPainter(CPU)实现的渲染器:按顺序合成可见图层(透明度,普通混合)。
class PainterRenderer : public Renderer
{
public:
    QImage render(const Document &document) const override;
};
