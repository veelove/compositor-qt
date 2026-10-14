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
#include <QVector2D>

#include <algorithm>
#include <cmath>
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

// 高斯模糊 shader(MVP:用可分离 box blur 近似,单方向 1D 卷积)。
// uniform uRadius:卷积半径(像素);uTexel:1/纹理尺寸。
// uHorizontal:1=水平方向,0=垂直方向;各跑一次得 2D blur。
constexpr const char *kFragBlurSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform vec2 uTexel;
uniform int uRadius;
uniform int uHorizontal;
out vec4 fragColor;
void main() {
    vec4 sum = texture(uScene, vUV);
    float weight = 1.0;
    for (int i = 1; i <= 64; ++i) {
        if (i > uRadius) break;
        vec2 offset = uHorizontal
            ? vec2(float(i) * uTexel.x, 0.0)
            : vec2(0.0, float(i) * uTexel.y);
        vec4 s1 = texture(uScene, vUV + offset);
        vec4 s2 = texture(uScene, vUV - offset);
        sum += s1 + s2;
        weight += 2.0;
    }
    fragColor = sum / weight;
}
)";

} // namespace

struct GpuRenderer::Impl
{
    std::unique_ptr<QOpenGLContext> ctx;
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;      // 层合成
    std::unique_ptr<QOpenGLFramebufferObject> fbo2;     // 调色/滤镜后处理
    std::unique_ptr<QOpenGLFramebufferObject> fbo3;     // 模糊中间帧
    std::unique_ptr<QOpenGLShaderProgram> program;      // 层合成
    std::unique_ptr<QOpenGLShaderProgram> postProgram;  // 调色 LUT
    std::unique_ptr<QOpenGLShaderProgram> blurProgram;  // 模糊 box blur
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
        d->blurProgram = std::make_unique<QOpenGLShaderProgram>();
        d->blurProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
        d->blurProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragBlurSrc);
        if (!d->blurProgram->link()) {
            qCDebug(lcGpuRenderer) << "Blur shader link failed, fallback";
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
        d->fbo3 = std::make_unique<QOpenGLFramebufferObject>(targetSize, QOpenGLFramebufferObject::CombinedDepthStencil);
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
    const int blurRadius = qBound(0, int(std::ceil(document.gaussianBlurRadius())), 64);

    // 当前"输入"纹理:先合成 fbo,经 blur 后变成 fbo2,经 LUT 后变成 fbo3
    QOpenGLFramebufferObject *src = d->fbo.get();
    QOpenGLFramebufferObject *mid = d->fbo2.get();
    QOpenGLFramebufferObject *dst = d->fbo3.get();

    // 3.4) 模糊(可分离 box blur,2 pass):fbo → fbo2(H) → fbo3(V)
    if (blurRadius > 0 && d->blurProgram) {
        d->blurProgram->bind();
        const int locScene  = d->blurProgram->uniformLocation("uScene");
        const int locTexel  = d->blurProgram->uniformLocation("uTexel");
        const int locRadius = d->blurProgram->uniformLocation("uRadius");
        const int locHor    = d->blurProgram->uniformLocation("uHorizontal");
        d->blurProgram->enableAttributeArray(0);
        d->blurProgram->enableAttributeArray(1);
        d->blurProgram->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        d->blurProgram->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));

        f->glDisable(GL_BLEND); // 覆盖式
        // Pass 1:水平方向
        f->glBindFramebuffer(GL_FRAMEBUFFER, mid->handle());
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
        d->blurProgram->setUniformValue(locScene, 0);
        d->blurProgram->setUniformValue(locTexel, QVector2D(1.0f / float(targetSize.width()), 0.0f));
        d->blurProgram->setUniformValue(locRadius, blurRadius);
        d->blurProgram->setUniformValue(locHor, 1);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        // Pass 2:垂直方向
        f->glBindFramebuffer(GL_FRAMEBUFFER, dst->handle());
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, mid->texture());
        d->blurProgram->setUniformValue(locScene, 0);
        d->blurProgram->setUniformValue(locTexel, QVector2D(0.0f, 1.0f / float(targetSize.height())));
        d->blurProgram->setUniformValue(locRadius, blurRadius);
        d->blurProgram->setUniformValue(locHor, 0);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        d->blurProgram->disableAttributeArray(0);
        d->blurProgram->disableAttributeArray(1);
        d->blurProgram->release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glEnable(GL_BLEND);

        src = dst; // 模糊结果作为下一阶段输入
        // 若无 LUT,把结果换回 mid 以统一"dst 即最终"
        if (!hasLut) {
            // 把 dst 内容(blit)到 mid,后面 readback 从 mid
            // 简化:直接让 src = dst,readback 时检测 hasLut
        }
    }

    // 3.5) 调色后处理 pass:src → mid(dst 用于调色)
    if (hasLut && d->postProgram) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, mid->handle());
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glDisable(GL_BLEND);

        QImage lutImg = document.renderLut().data.convertToFormat(QImage::Format_RGBA8888);
        QOpenGLTexture lutTex(QOpenGLTexture::Target2D);
        lutTex.setData(lutImg, QOpenGLTexture::DontGenerateMipMaps);
        lutTex.setMinificationFilter(QOpenGLTexture::Nearest);
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
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
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

        src = mid; // LUT 后结果在 mid
    }

    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    d->ctx->doneCurrent();

    // 4) Readback:src 指向最终图
    QImage result = src->toImage(true); // true = 翻转 Y
    if (result.format() == QImage::Format_RGBA8888)
        return result.convertToFormat(QImage::Format_ARGB32);
    return result.convertToFormat(QImage::Format_ARGB32);
}
