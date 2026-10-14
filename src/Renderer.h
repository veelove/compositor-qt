#pragma once

#include <QImage>

class Document;

// 渲染抽象:把文档合成到一张 ARGB 图。
// 原型实现用 QPainter(PainterRenderer);后续性能阶段可替换为 QRhi GPU 实现。
class Renderer
{
public:
    virtual ~Renderer() = default;
    virtual QImage render(const Document &document) const = 0;
};
