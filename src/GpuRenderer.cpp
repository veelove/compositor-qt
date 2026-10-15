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

// 暗角 shader:按到中心的归一化距离做径向衰减,向黑色混合
// uAmount:强度 0..1,uMid:中点 0..1,uRound:圆度 -1..1,uFeather:羽化 0..1
constexpr const char *kFragVignetteSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform float uAmount;
uniform float uMid;
uniform float uRound;
uniform float uFeather;
uniform vec2 uSize;
out vec4 fragColor;
void main() {
    vec4 c = texture(uScene, vUV);
    if (uAmount <= 0.0) { fragColor = c; return; }
    vec2 ndc = (vUV - 0.5) * 2.0;
    float rx = ndc.x * (1.0 + uRound * 0.3);
    float ry = ndc.y * (1.0 - uRound * 0.3);
    float dist = length(vec2(rx, ry)) / sqrt(2.0);
    float falloff = 0.0;
    if (dist > uMid) {
        float t = clamp((dist - uMid) / max(0.01, 1.0 - uMid), 0.0, 1.0);
        falloff = t * t;
        if (uFeather > 0.0)
            falloff = pow(falloff, 0.5 + uFeather * 0.5);
    }
    float mix_ = clamp(falloff * uAmount, 0.0, 1.0);
    c.rgb = c.rgb * (1.0 - mix_);
    fragColor = c;
}
)";

// 动感模糊 shader:沿角度方向采样 N 个点平均
// uDistance:距离(像素),uAngle:角度(度)
constexpr const char *kFragMotionBlurSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform float uDistance;
uniform float uAngle;  // 度,顺时针
uniform vec2 uTexel;
out vec4 fragColor;
void main() {
    if (uDistance <= 0.0) { fragColor = texture(uScene, vUV); return; }
    float rad = radians(uAngle);
    vec2 dir = vec2(cos(rad), sin(rad));
    int samples = int(ceil(uDistance)) + 1;
    vec4 sum = vec4(0.0);
    int count = 0;
    for (int s = -32; s <= 32; ++s) {
        if (s < -samples / 2 || s > samples / 2) continue;
        float t = (float(s) / float(samples)) * uDistance;
        vec2 ofs = dir * t * uTexel;
        sum += texture(uScene, vUV + ofs);
        count++;
    }
    fragColor = sum / float(count);
}
)";

// 杂色 shader:每像素基于 LCG 随机数加扰动
// uAmount:0-100 强度;uGaussian:1=双 LCG 平均(高斯);uMono:1=单色通道;uSeed:种子
constexpr const char *kFragNoiseSrc = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform float uAmount;
uniform int uGaussian;
uniform int uMono;
uniform uint uSeed;
out vec4 fragColor;
uint lcg(uint s) { return s * 1664525u + 1013904223u; }
float lcgUnit(uint s) {
    uint v = lcg(s);
    return (float(v) / 4294967295.0) * 2.0 - 1.0;
}
float noiseSample(uint s) {
    if (uGaussian == 1) return (lcgUnit(s) + lcgUnit(lcg(s))) * 0.5;
    return lcgUnit(s);
}
void main() {
    vec4 c = texture(uScene, vUV);
    if (uAmount <= 0.0) { fragColor = c; return; }
    float strength = (uAmount / 100.0) * 255.0 / 2.0;
    ivec2 pix = ivec2(gl_FragCoord.xy);
    uint base = uSeed + uint(pix.y) * 1973u + uint(pix.x) * 9277u;
    if (uMono == 1) {
        float n = noiseSample(base);
        float d = n * strength;
        c.r = clamp(c.r + d, 0.0, 1.0);
        c.g = clamp(c.g + d, 0.0, 1.0);
        c.b = clamp(c.b + d, 0.0, 1.0);
    } else {
        float n1 = noiseSample(base);
        float n2 = noiseSample(base + 0x9E3779B9u);
        float n3 = noiseSample(base + 0x85EBCA77u);
        c.r = clamp(c.r + n1 * strength, 0.0, 1.0);
        c.g = clamp(c.g + n2 * strength, 0.0, 1.0);
        c.b = clamp(c.b + n3 * strength, 0.0, 1.0);
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
    std::unique_ptr<QOpenGLFramebufferObject> fbo2;     // 调色/滤镜后处理
    std::unique_ptr<QOpenGLFramebufferObject> fbo3;     // 模糊中间帧
    std::unique_ptr<QOpenGLShaderProgram> program;      // 层合成
    std::unique_ptr<QOpenGLShaderProgram> postProgram;  // 调色 LUT
    std::unique_ptr<QOpenGLShaderProgram> blurProgram;  // 模糊 box blur
    std::unique_ptr<QOpenGLShaderProgram> vignetteProgram; // 暗角
    std::unique_ptr<QOpenGLShaderProgram> motionProgram;   // 动感模糊
    std::unique_ptr<QOpenGLShaderProgram> noiseProgram;    // 杂色
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
        d->vignetteProgram = std::make_unique<QOpenGLShaderProgram>();
        d->vignetteProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
        d->vignetteProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragVignetteSrc);
        if (!d->vignetteProgram->link()) {
            qCDebug(lcGpuRenderer) << "Vignette shader link failed, fallback";
            d->ctx->doneCurrent();
            return PainterRenderer().render(document);
        }
        d->motionProgram = std::make_unique<QOpenGLShaderProgram>();
        d->motionProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
        d->motionProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragMotionBlurSrc);
        if (!d->motionProgram->link()) {
            qCDebug(lcGpuRenderer) << "Motion blur shader link failed, fallback";
            d->ctx->doneCurrent();
            return PainterRenderer().render(document);
        }
        d->noiseProgram = std::make_unique<QOpenGLShaderProgram>();
        d->noiseProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
        d->noiseProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragNoiseSrc);
        if (!d->noiseProgram->link()) {
            qCDebug(lcGpuRenderer) << "Noise shader link failed, fallback";
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
    const bool hasVignette = document.vignetteAmount() > 0.0;
    const double mbDistance = document.motionBlurDistance();
    const bool hasMotion = mbDistance > 0.0;

    // 当前"输入"纹理;mid/dst 在多个 pass 中 ping-pong
    QOpenGLFramebufferObject *src = d->fbo.get();
    QOpenGLFramebufferObject *fboA = d->fbo2.get();
    QOpenGLFramebufferObject *fboB = d->fbo3.get();

    // 通用单 pass 模板:把 src 写到 dst,返回 dst
    auto runPass = [&](QOpenGLFramebufferObject *dst) {
        d->ctx->makeCurrent(d->surface.get());
        f->glBindFramebuffer(GL_FRAMEBUFFER, dst->handle());
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glDisable(GL_BLEND);
    };

    // 通用:在 fboA/fboB 之间 ping-pong,执行 fragment program(p 已绑好 uniforms)
    auto runScreenPass = [&](QOpenGLShaderProgram *prog, QOpenGLFramebufferObject *dst,
                             const std::function<void()> &bindUniforms) {
        d->ctx->makeCurrent(d->surface.get());
        f->glBindFramebuffer(GL_FRAMEBUFFER, dst->handle());
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glDisable(GL_BLEND);
        prog->bind();
        prog->enableAttributeArray(0);
        prog->enableAttributeArray(1);
        prog->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        prog->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
        bindUniforms();
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        prog->disableAttributeArray(0);
        prog->disableAttributeArray(1);
        prog->release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glEnable(GL_BLEND);
        src = dst;
    };

    // 3.4) 模糊(可分离 box blur,2 pass):fbo → fboA(H) → fboB(V)
    if (blurRadius > 0 && d->blurProgram) {
        const int locScene  = d->blurProgram->uniformLocation("uScene");
        const int locTexel  = d->blurProgram->uniformLocation("uTexel");
        const int locRadius = d->blurProgram->uniformLocation("uRadius");
        const int locHor    = d->blurProgram->uniformLocation("uHorizontal");
        d->blurProgram->bind();
        d->blurProgram->enableAttributeArray(0);
        d->blurProgram->enableAttributeArray(1);
        d->blurProgram->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        d->blurProgram->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));
        f->glDisable(GL_BLEND);
        // Pass 1:水平
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboA->handle());
        f->glClearColor(0, 0, 0, 0); f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
        d->blurProgram->setUniformValue(locScene, 0);
        d->blurProgram->setUniformValue(locTexel, QVector2D(1.0f / float(targetSize.width()), 0.0f));
        d->blurProgram->setUniformValue(locRadius, blurRadius);
        d->blurProgram->setUniformValue(locHor, 1);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        // Pass 2:垂直
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboB->handle());
        f->glClearColor(0, 0, 0, 0); f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, fboA->texture());
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
        src = fboB;
    }

    // 3.5) 动感模糊 src → 另一 fbo
    if (hasMotion && d->motionProgram) {
        const int locScene = d->motionProgram->uniformLocation("uScene");
        const int locDist  = d->motionProgram->uniformLocation("uDistance");
        const int locAng   = d->motionProgram->uniformLocation("uAngle");
        const int locTexel = d->motionProgram->uniformLocation("uTexel");
        d->motionProgram->bind();
        d->motionProgram->enableAttributeArray(0);
        d->motionProgram->enableAttributeArray(1);
        d->motionProgram->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        d->motionProgram->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));
        f->glDisable(GL_BLEND);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboA->handle());
        f->glClearColor(0, 0, 0, 0); f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
        d->motionProgram->setUniformValue(locScene, 0);
        d->motionProgram->setUniformValue(locDist, float(mbDistance));
        d->motionProgram->setUniformValue(locAng, float(document.motionBlurAngle()));
        d->motionProgram->setUniformValue(locTexel, QVector2D(1.0f / float(targetSize.width()),
                                                              1.0f / float(targetSize.height())));
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        d->motionProgram->disableAttributeArray(0);
        d->motionProgram->disableAttributeArray(1);
        d->motionProgram->release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glEnable(GL_BLEND);
        src = fboA;
    }

    // 3.6) 暗角 src → 另一 fbo
    if (hasVignette && d->vignetteProgram) {
        const int locScene = d->vignetteProgram->uniformLocation("uScene");
        const int locAmt   = d->vignetteProgram->uniformLocation("uAmount");
        const int locMid   = d->vignetteProgram->uniformLocation("uMid");
        const int locRound = d->vignetteProgram->uniformLocation("uRound");
        const int locFeat  = d->vignetteProgram->uniformLocation("uFeather");
        d->vignetteProgram->bind();
        d->vignetteProgram->enableAttributeArray(0);
        d->vignetteProgram->enableAttributeArray(1);
        d->vignetteProgram->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        d->vignetteProgram->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));
        f->glDisable(GL_BLEND);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboA->handle());
        f->glClearColor(0, 0, 0, 0); f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
        d->vignetteProgram->setUniformValue(locScene, 0);
        d->vignetteProgram->setUniformValue(locAmt, float(document.vignetteAmount()) / 100.0f);
        d->vignetteProgram->setUniformValue(locMid, float(document.vignetteMidpoint()) / 100.0f);
        d->vignetteProgram->setUniformValue(locRound, float(document.vignetteRoundness()) / 100.0f);
        d->vignetteProgram->setUniformValue(locFeat, float(document.vignetteFeather()) / 100.0f);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        d->vignetteProgram->disableAttributeArray(0);
        d->vignetteProgram->disableAttributeArray(1);
        d->vignetteProgram->release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glEnable(GL_BLEND);
        src = fboA;
    }

    // 3.65) 杂色 src → fboA
    if (document.noiseAmount() > 0.0 && d->noiseProgram) {
        const int locScene = d->noiseProgram->uniformLocation("uScene");
        const int locAmt   = d->noiseProgram->uniformLocation("uAmount");
        const int locGauss = d->noiseProgram->uniformLocation("uGaussian");
        const int locMono   = d->noiseProgram->uniformLocation("uMono");
        const int locSeed   = d->noiseProgram->uniformLocation("uSeed");
        d->noiseProgram->bind();
        d->noiseProgram->enableAttributeArray(0);
        d->noiseProgram->enableAttributeArray(1);
        d->noiseProgram->setAttributeArray(0, GL_FLOAT, verts, 4, 4 * sizeof(float));
        d->noiseProgram->setAttributeArray(1, GL_FLOAT, verts + 2, 4, 4 * sizeof(float));
        f->glDisable(GL_BLEND);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboA->handle());
        f->glClearColor(0, 0, 0, 0); f->glClear(GL_COLOR_BUFFER_BIT);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, src->texture());
        d->noiseProgram->setUniformValue(locScene, 0);
        d->noiseProgram->setUniformValue(locAmt, float(document.noiseAmount()));
        d->noiseProgram->setUniformValue(locGauss, document.noiseGaussian() ? 1 : 0);
        d->noiseProgram->setUniformValue(locMono, document.noiseMonochromatic() ? 1 : 0);
        d->noiseProgram->setUniformValue(locSeed, document.noiseSeed());
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        d->noiseProgram->disableAttributeArray(0);
        d->noiseProgram->disableAttributeArray(1);
        d->noiseProgram->release();
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glEnable(GL_BLEND);
        src = fboA;
    }

    // 3.7) 调色后处理 pass
    if (hasLut && d->postProgram) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboA->handle());
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

        src = fboA;
    }

    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    d->ctx->doneCurrent();

    // 4) Readback:src 指向最终图
    QImage result = src->toImage(true); // true = 翻转 Y
    if (result.format() == QImage::Format_RGBA8888)
        return result.convertToFormat(QImage::Format_ARGB32);
    return result.convertToFormat(QImage::Format_ARGB32);
}
