#pragma once

#include "Renderer.h"

// GPU 渲染器(用 OpenGL FBO + 简单 fragment shader 在 GPU 上合成多图层)
//
// MVP 范围:OpenGL 3.3+,简单纹理采样 + alpha-over 合成到单个 FBO,然后 readback 为 QImage
// 不支持:多 Pass、滤镜、blend modes(全部按 normal 混合)
//
// 运行时检测:如当前没有 QOpenGLContext,会 fall back 到 PainterRenderer
class GpuRenderer : public Renderer
{
public:
    GpuRenderer();
    ~GpuRenderer() override;

    QImage render(const Document &document) const override;

private:
    struct Impl;
    Impl *d; // 持有 OpenGL 资源,延迟初始化避免 headless 测试时报错
};
