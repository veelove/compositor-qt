#include "GpuRenderer.h"

#include "Document.h"
#include "Layer.h"
#include "PainterRenderer.h"

#include <QImage>
#include <QLoggingCategory>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QSurfaceFormat>

#include <memory>

Q_LOGGING_CATEGORY(lcGpuRenderer, "compositorqt.gpurenderer")

namespace {

// 简单全屏四边形 + 纹理采样的 fragment shader
// 输入:图层纹理,纹理坐标来自顶点属性(已预乘)
constexpr const char *kVertSrc = R"(
#version 330 core
in vec2 aPos;
in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char *kFragSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform float uOpacity;
out vec4 fragColor;
void main() {
    vec4 c = texture(uTex, vUV);
    c.a *= uOpacity;
    fragColor = c;
}
)";

// 调色后处理:对合成结果逐像素查 256x1 LUT(Levels/Curves/Exposure/BC 等)
// LUT 纹理坐标 s = 通道值/255,Nearest 过滤保证取整精确,与 CPU 查表一致
constexpr const char *kFragPostSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uLut;
uniform int uHasLut;
out vec4 fragColor;
void main() {
    vec4 c = texture(uScene, vUV);
    if (uHasLut == 1) {
        c.r = texture(uLut, vec2(c.r, 0.5)).r;
        c.g = texture(uLut, vec2(c.g, 0.5)).g;
        c.b = texture(uLut, vec2(c.b, 0.5)).b;
    }
    fragColor = c;
}
)";

} // namespace

struct GpuRenderer::Impl
{
    std::unique_ptr<QOpenGLContext> ctx;
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;      // 层合成
    std::unique_ptr<QOpenGLFramebufferObject> fbo2;     // 调色后处理
    std::unique_ptr<QOpenGLShaderProgram> program;      // 层合成
    std::unique_ptr<QOpenGLShaderProgram> postProgram;  // 调色 LUT
    QSize lastSize;
    bool ready = false;
};

GpuRenderer::GpuRenderer()
    : d(new Impl)
{
}

GpuRenderer::~GpuRenderer()
{
    delete d;
}

QImage GpuRenderer::render(const Document &document) const
{
    if (document.size().isEmpty() || document.layerCount() == 0)
        return PainterRenderer().render(document);

    // 1) 检查 OpenGL 可用
    if (!QOpenGLContext::supportsThreadedOpenGL()) {
        qCDebug(lcGpuRenderer) << "OpenGL not supported, falling back to PainterRenderer";
        return PainterRenderer().render(document);
    }

    // 2) 初始化(只一次)
    if (!d->ready) {
        d->ctx = std::make_unique<QOpenGLContext>();
        QSurfaceFormat fmt;
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setVersion(3, 3);
        d->ctx->setFormat(fmt);
        if (!d->ctx->create()) {
            qCDebug(lcGpuRenderer) << "OpenGL context creation failed, fallback";
            return PainterRenderer().render(document);
        }
        d->surface = std::make_unique<QOffscreenSurface>();
        d->surface->setFormat(fmt);
        d->surface->create();
        if (!d->surface->isValid()) {
            qCDebug(lcGpuRenderer) << "OffscreenSurface invalid, fallback";
            return PainterRenderer().render(document);
        }
        d->ctx->makeCurrent(d->surface.get());

        d->program = std::make_unique<QOpenGLShaderProgram>();
        d->program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
        d->program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
        if (!d->program->link()) {
            qCDebug(lcGpuRenderer) << "Shader link failed, fallback";
            d->ctx->doneCurrent();
            return PainterRenderer().render(document);
        }
        d->postProgram = std::make_unique<QOpenGLShaderProgram>();
        d->postProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
        d->postProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragPostSrc);
        if (!d->postProgram->link()) {
            qCDebug(lcGpuRenderer) << "Post shader link failed, fallback";
            d->ctx->doneCurrent();
            return PainterRenderer().render(document);
        }
        d->ready = true;
    }

    // 3) 重建 FBO(尺寸可能变化)
    const QSize targetSize = document.size();
    if (d->lastSize != targetSize) {
        d->fbo = std::make_unique<QOpenGLFramebufferObject>(targetSize, QOpenGLFramebufferObject::CombinedDepthStencil);
        d->fbo2 = std::make_unique<QOpenGLFramebufferObject>(targetSize, QOpenGLFramebufferObject::CombinedDepthStencil);
        d->lastSize = targetSize;
    }
    if (!d->fbo) {
        return PainterRenderer().render(document);
    }

    QOpenGLFunctions *f = d->ctx->functions();
    f->initializeOpenGLFunctions();

    d->ctx->makeCurrent(d->surface.get());
    f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo->handle());

    // 清屏(透明)
    f->glClearColor(0, 0, 0, 0);
    f->glClear(GL_COLOR_BUFFER_BIT);

    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    f->glDisable(GL_DEPTH_TEST);

    d->program->bind();
    int locOpacity = d->program->uniformLocation("uOpacity");
    int locTex = d->program->uniformLocation("uTex");

    // 全屏四边形顶点(直接传 NDC)
    static const float verts[] = {
        // x,    y,    u,    v
        -1.f, -1.f, 0.f, 1.f, // 左下
         1.f, -1.f, 1.f, 1.f, // 右下
        -1.f,  1.f, 0.f, 0.f, // 左上
         1.f,  1.f, 1.f, 0.f, // 右上
    };
    d->program->enableAttributeArray(0);
    d->program->enableAttributeArray(1);
    d->program->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
    d->program->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));

    for (Layer *layer : document.layers()) {
        if (!layer->isVisible() || layer->opacity() <= 0.0f)
            continue;
        // 上传图层像素到纹理(QImage 需为 Format_RGBA8888)
        const QImage &img = layer->pixels();
        QOpenGLTexture tex(QOpenGLTexture::Target2D);
        tex.setData(img, QOpenGLTexture::DontGenerateMipMaps);
        tex.setMinificationFilter(QOpenGLTexture::Linear);
        tex.setMagnificationFilter(QOpenGLTexture::Linear);
        tex.bind(0);
        d->program->setUniformValue(locTex, 0);
        d->program->setUniformValue(locOpacity, static_cast<float>(layer->opacity()));
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        tex.release();
    }

    d->program->disableAttributeArray(0);
    d->program->disableAttributeArray(1);
    d->program->release();

    const bool hasLut = !document.renderLut().isNull();

    // 3.5) 调色后处理 pass:合成结果 → LUT 查表 → fbo2
    if (hasLut && d->fbo2) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo2->handle());
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glDisable(GL_BLEND); // 覆盖式拷贝

        QImage lutImg = document.renderLut().data.convertToFormat(QImage::Format_RGBA8888);
        QOpenGLTexture lutTex(QOpenGLTexture::Target2D);
        lutTex.setData(lutImg, QOpenGLTexture::DontGenerateMipMaps);
        lutTex.setMinificationFilter(QOpenGLTexture::Nearest); // 取整精确,不插值
        lutTex.setMagnificationFilter(QOpenGLTexture::Nearest);

        d->postProgram->bind();
        const int locScene  = d->postProgram->uniformLocation("uScene");
        const int locLut    = d->postProgram->uniformLocation("uLut");
        const int locHasLut = d->postProgram->uniformLocation("uHasLut");
        d->postProgram->enableAttributeArray(0);
        d->postProgram->enableAttributeArray(1);
        d->postProgram->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        d->postProgram->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));

        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, d->fbo->texture()); // 合成结果
        d->postProgram->setUniformValue(locScene, 0);
        lutTex.bind(1);
        d->postProgram->setUniformValue(locLut, 1);
        d->postProgram->setUniformValue(locHasLut, 1);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        d->postProgram->disableAttributeArray(0);
        d->postProgram->disableAttributeArray(1);
        d->postProgram->release();
        lutTex.release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glEnable(GL_BLEND);
    }

    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    d->ctx->doneCurrent();

    // 4) Readback(QImage 不能直接给 RGBA,需换格式)
    QImage result = (hasLut && d->fbo2) ? d->fbo2->toImage(true) : d->fbo->toImage(true);
    if (result.format() == QImage::Format_RGBA8888)
        return result.convertToFormat(QImage::Format_ARGB32);
    return result.convertToFormat(QImage::Format_ARGB32);
}
