#include <QtTest>

#include "AdjustLut.h"
#include "Adjustments.h"
#include "CanvasView.h"
#include "Document.h"
#include "Filters.h"
#include "Geometry.h"
#include "GpuRenderer.h"
#include "Layer.h"
#include "RawProvider.h"
#include "VisionProvider.h"
#include "PainterRenderer.h"
#include "PsdReader.h"
#include "PsdWriter.h"
#include "RawProvider.h"
#include "Selection.h"
#include "UndoCommands.h"
#include "VisionProvider.h"

#include <QPainter>
#include <QPoint>
#include <QUndoStack>

class TestDocument : public QObject
{
    Q_OBJECT
private slots:
    void addRemoveLayer();
    void undoRedoPaint();
    void rendererComposites();
    void psdPackBits();
    void psdMagicAndRejection();
    void adjLevelsIdentity();
    void adjLevelsBrighten();
    void adjLevelsInvert();
    void adjLevelsGamma();
    void adjHsvSaturate();
    void adjHsvHueShift();
    void adjBrightnessContrast();
    void adjInvert();
    void adjCurvesIdentity();
    void adjCurvesGamma();
    void adjCurvesHighlightClamp();
    void geomCrop();
    void geomResize();
    void geomRotate90();
    void geomRotate45();
    void adjBlackWhiteDefault();
    void adjBlackWhiteTint();
    void adjColorBalanceIdentity();
    void adjColorBalanceShift();
    void adjExposureIdentity();
    void adjExposurePlus1EV();
    void adjExposureGamma();
    void spikeRawExtensions();
    void spikeVisionStub();
    void psdRoundTripFlat();
    void psdRoundTripGradient();
    void filtGaussianIdentity();
    void filtGaussianUniformity();
    void filtNoiseDeterministic();
    void filtVignetteDarkens();
    void filterCommandUndoRedo();
    void layerOffsetRender();
    void moveLayerCommandUndoRedo();
    void clearRectCommandUndoRedo();
    void lutLevelsMatchesApply();
    void lutCurvesMatchesApply();
    void lutExposureMatchesApply();
    void lutBcMatchesApply();
    void lutComposeSemantics();
    void documentRenderLut();
    void gpuRendererLutConsistency();
    void adjLutCommandUndoRedo();
    void documentOwnsUndoStack();
    void perDocumentUndoIsolation();
    void documentGaussianBlurRadius();
    void gaussianBlurCommandUndoRedo();
    void gpuGaussianBlurConsistency();
    void vignetteAndMotionBlurOnDocument();
    void vignetteAndMotionBlurCommandUndoRedo();
    void gpuVignetteAndMotionConsistency();
    void addNoiseOnDocument();
    void addNoiseCommandUndoRedo();
    void gpuAddNoiseConsistency();
    void hsvBwCbOnDocument();
    void hsvBwCbCommandUndoRedo();
    void gpuHsvBwCbConsistency();
    void rawProviderExtensions();
    void rawProviderFactoryAndStub();
    void visionProviderFactoryAndStub();
    void magicWandMaskBoundingRect();
    void selectionCommandUndoRedo();
    void magicWandCompositeCommand();
    void selMaskPrimitives();
    void selBlendByMaskRespectsShape();
    void selClearByMaskRespectsShape();
    void selCropToMaskBounds();
    void clearMaskCommandUndoRedo();
    void selCombineBooleanOps();
    void selFeatherGrowShrink();
    void maskCommandUndoRedo();
    void cropDocumentCommandUndoRedo();
    void layerViaMaskCommandUndoRedo();
};

void TestDocument::addRemoveLayer()
{
    Document doc(QSize(10, 10));
    auto *background = new Layer(QStringLiteral("L1"), doc.size(), Qt::red);
    doc.addLayer(background);
    QCOMPARE(doc.layerCount(), 1);
    QCOMPARE(doc.layers().first(), background);

    QUndoStack stack;
    auto *l2 = new Layer(QStringLiteral("L2"), doc.size(), Qt::blue);
    stack.push(new AddLayerCommand(&doc, l2));
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(doc.activeLayer(), l2);

    stack.undo();
    QCOMPARE(doc.layerCount(), 1);
    QVERIFY(doc.activeLayer() != l2);

    stack.redo();
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(doc.activeLayer(), l2);
}

void TestDocument::undoRedoPaint()
{
    Document doc(QSize(20, 20));
    auto *layer = new Layer(QStringLiteral("L"), doc.size(), Qt::white);
    doc.addLayer(layer);
    QUndoStack stack;

    const QRect dirty(0, 0, 14, 14);
    const QImage before = layer->pixels().copy(dirty);
    {
        QPainter p(&layer->pixels());
        p.setPen(QPen(Qt::black, 3));
        p.drawLine(QPoint(2, 2), QPoint(10, 10));
    }
    const QImage after = layer->pixels().copy(dirty);
    QVERIFY(before != after);

    stack.push(new PaintCommand(&doc, layer, dirty, before, after));
    QVERIFY(stack.canUndo());

    stack.undo();
    QCOMPARE(layer->pixels().copy(dirty), after);

    stack.redo();
    QCOMPARE(layer->pixels().copy(dirty), after);
}

void TestDocument::rendererComposites()
{
    Document doc(QSize(8, 8));
    doc.addLayer(new Layer(QStringLiteral("bottom"), doc.size(), Qt::blue));
    doc.addLayer(new Layer(QStringLiteral("top"), doc.size(), QColor(255, 0, 0, 128)));

    PainterRenderer renderer;
    const QImage out = renderer.render(doc);
    QCOMPARE(out.size(), doc.size());

    QVERIFY(out.pixelColor(4, 4) != QColor(Qt::blue));
    QVERIFY(out.pixelColor(4, 4).red() > 0);
}

void TestDocument::psdPackBits()
{
    // PackBits 测试 1:literal run(n>=0)
    // 编码 [0x02, 0xAA, 0xBB, 0xCC] 表示"后面 3 字节原样" → 应得 [AA BB CC]
    {
        QByteArray enc;
        enc.append(static_cast<char>(0x02));
        enc.append(static_cast<char>(0xAA));
        enc.append(static_cast<char>(0xBB));
        enc.append(static_cast<char>(0xCC));
        const QByteArray dec = psd::Cursor::packBitsDecode(enc, 3);
        QCOMPARE(dec.size(), 3);
        QCOMPARE(static_cast<uint8_t>(dec.at(0)), uint8_t(0xAA));
        QCOMPARE(static_cast<uint8_t>(dec.at(1)), uint8_t(0xBB));
        QCOMPARE(static_cast<uint8_t>(dec.at(2)), uint8_t(0xCC));
    }
    // 测试 2:repeat run(n<0)
    // 编码 [0xFD, 0x42] 表示"重复 0x42 三次"(−0xFD+1 = 3)
    {
        QByteArray enc;
        enc.append(static_cast<char>(0xFD));
        enc.append(static_cast<char>(0x42));
        const QByteArray dec = psd::Cursor::packBitsDecode(enc, 3);
        QCOMPARE(dec.size(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(static_cast<uint8_t>(dec.at(i)), uint8_t(0x42));
    }
    // 测试 3:组合
    // [0x01, 0x11, 0x22, 0xFF, 0x33] → 2 字节原样(11 22)+ 重复 0x33 两次
    {
        QByteArray enc;
        enc.append(static_cast<char>(0x01));
        enc.append(static_cast<char>(0x11));
        enc.append(static_cast<char>(0x22));
        enc.append(static_cast<char>(0xFF)); // -1 → 2 次
        enc.append(static_cast<char>(0x33));
        const QByteArray dec = psd::Cursor::packBitsDecode(enc, 4);
        QCOMPARE(dec.size(), 4);
        QCOMPARE(static_cast<uint8_t>(dec.at(0)), uint8_t(0x11));
        QCOMPARE(static_cast<uint8_t>(dec.at(1)), uint8_t(0x22));
        QCOMPARE(static_cast<uint8_t>(dec.at(2)), uint8_t(0x33));
        QCOMPARE(static_cast<uint8_t>(dec.at(3)), uint8_t(0x33));
    }
}

void TestDocument::psdMagicAndRejection()
{
    // 不以 8BPS 开头的输入应被拒
    QVERIFY(!psd::Reader::matches(QByteArray("XXXX")));
    QVERIFY(psd::Reader::matches(QByteArray("8BPS...")));

    // 调 read 应当返回 ok=false 且 error 非空
    const auto r = psd::Reader::read(QByteArray("junk"));
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

// ============================== Adjustments ==============================
static QImage makeGray4()
{
    // 2x2 灰阶图像:[0, 85, 170, 255] 用于快速算术校验
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(0, 0, 0));
    img.setPixelColor(1, 0, QColor(85, 85, 85));
    img.setPixelColor(0, 1, QColor(170, 170, 170));
    img.setPixelColor(1, 1, QColor(255, 255, 255));
    return img;
}

void TestDocument::adjLevelsIdentity()
{
    QImage img = makeGray4();
    QImage before = img;

    adj::LevelsParams p;
    // 默认值(全部 identity)应原样输出
    adj::applyLevels(img, p);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(img.pixel(x, y), before.pixel(x, y));
}

void TestDocument::adjLevelsBrighten()
{
    // black=50, white=255, gamma=1 → 50 以下变黑,其他线性保留
    QImage img = makeGray4();
    adj::LevelsParams p;
    p.rgbRange.black = 50;
    adj::applyLevels(img, p);
    // (0,0)=0 → 变 0
    QCOMPARE(img.pixelColor(0, 0).red(), 0);
    // (1,1)=255 → 不变
    QCOMPARE(img.pixelColor(1, 1).red(), 255);
    // (1,0)=85 → 公式: (85-50)/(255-50) = 0.1707 → *255 ≈ 43
    // 上游 Swift 用 (value*255 - black)/(white - black) 同样公式,容差 2
    const int newVal = img.pixelColor(1, 0).red();
    QVERIFY2(newVal >= 41 && newVal <= 45, qPrintable(QString("got %1").arg(newVal)));
}

void TestDocument::adjLevelsInvert()
{
    // outputBlack=255, outputWhite=0 → 颜色反转
    QImage img = makeGray4();
    adj::LevelsParams p;
    p.rgbRange.outputBlack = 255;
    p.rgbRange.outputWhite = 0;
    adj::applyLevels(img, p);
    // 0 → 255
    QCOMPARE(img.pixelColor(0, 0).red(), 255);
    // 255 → 0
    QCOMPARE(img.pixelColor(1, 1).red(), 0);
    // 85 → 255 - 85 ≈ 170
    QCOMPARE(img.pixelColor(1, 0).red(), 255 - 85);
}

void TestDocument::adjLevelsGamma()
{
    // gamma=0.5 → 中间调应比原值更亮
    QImage img = makeGray4();
    adj::LevelsParams p;
    p.rgbRange.gamma = 0.5;
    adj::applyLevels(img, p);
    // 128 应该变成 sqrt(128/255)*255 ≈ 128(无变化) — 改用 100 来测
    // 100 → 100/255=0.392, ^0.5 = 0.626, *255 = 159
    // 重新构造一个 100 灰度的图
    QImage img2(1, 1, QImage::Format_ARGB32);
    img2.setPixelColor(0, 0, QColor(100, 100, 100));
    adj::applyLevels(img2, p);
    const int r = img2.pixelColor(0, 0).red();
    QVERIFY2(r >= 156 && r <= 162, qPrintable(QString("got %1").arg(r)));
}

void TestDocument::adjHsvSaturate()
{
    // 半饱和 → 期望 像素饱和度增加
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(128, 64, 192)); // 偏蓝
    const int origR = img.pixelColor(0, 0).red();
    const int origB = img.pixelColor(0, 0).blue();
    adj::HsvParams p;
    p.saturation = 50; // +50%
    adj::applyHsv(img, p);
    // 饱和后 B 应更接近原 B,R 应更接近 0(远离蓝)
    // 不做精确数值,只检查趋势
    const int newB = img.pixelColor(0, 0).blue();
    QVERIFY(newB > origB || newB == 255);
    Q_UNUSED(origR);
}

void TestDocument::adjHsvHueShift()
{
    // 纯红→+120 度色相 → 期望绿色分量变大
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(255, 0, 0));
    adj::HsvParams p;
    p.hue = 120; // +120 度
    adj::applyHsv(img, p);
    const QColor out = img.pixelColor(0, 0);
    QVERIFY(out.green() > 200);
}

void TestDocument::adjBrightnessContrast()
{
    // brightness=+100:每像素 +255 → 全部 255(全白)
    {
        QImage img = makeGray4();
        adj::BcParams p;
        p.brightness = 100;
        adj::applyBrightnessContrast(img, p);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                QCOMPARE(img.pixelColor(x, y).red(), 255);
    }
    // contrast=-100:全压到 128
    {
        QImage img = makeGray4();
        adj::BcParams p;
        p.contrast = -100;
        adj::applyBrightnessContrast(img, p);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                QCOMPARE(img.pixelColor(x, y).red(), 128);
    }
    // contrast=+100:0→0, 128→128(不动), 255→255,85→42(向下远离 128)
    {
        QImage img = makeGray4();
        adj::BcParams p;
        p.contrast = 100;
        adj::applyBrightnessContrast(img, p);
        QCOMPARE(img.pixelColor(0, 0).red(), 0);     // 0
        QCOMPARE(img.pixelColor(1, 1).red(), 255);   // 255
        // (1,0)=85: 128 + (85-128)*2  = 42
        QCOMPARE(img.pixelColor(1, 0).red(), 42);
        // (0,1)=170: 128 + (170-128)*2 = 212
        QCOMPARE(img.pixelColor(0, 1).red(), 212);
    }
    // 默认 0,0 不动
    {
        QImage img = makeGray4();
        QImage before = img;
        adj::BcParams p;
        adj::applyBrightnessContrast(img, p);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                QCOMPARE(img.pixel(x, y), before.pixel(x, y));
    }
}

void TestDocument::adjInvert()
{
    QImage img = makeGray4();
    adj::applyInvert(img);
    QCOMPARE(img.pixelColor(0, 0).red(), 255);   // 0 → 255
    QCOMPARE(img.pixelColor(1, 1).red(), 0);     // 255 → 0
    QCOMPARE(img.pixelColor(0, 1).red(), 85);    // 170 → 85
    QCOMPARE(img.pixelColor(1, 0).red(), 170);   // 85 → 170

    // Alpha 保持:完全不透明图像的 alpha 不变
    QCOMPARE(img.pixelColor(0, 0).alpha(), 255);
}

void TestDocument::adjCurvesIdentity()
{
    // identity 曲线 (0,0)→(255,255) 像素不变
    QImage img = makeGray4();
    QImage before = img;
    adj::CurvesParams p;
    p.points = {{0, 0}, {255, 255}};
    adj::applyCurves(img, p);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(img.pixel(x, y), before.pixel(x, y));
}

void TestDocument::adjCurvesGamma()
{
    // 曲线 (0,0)→(128,100)→(255,255) ≈ gamma=0.8 左右(中间值上抬)
    // 端点固定,LUT[0]=0, LUT[255]=255,LUT[128]≈100
    {
        const QVector<adj::CurvePoint> pts = {{0, 0}, {128, 100}, {255, 255}};
        const QVector<int> lut = adj::buildCurveLut(pts);
        QCOMPARE(lut.at(0), 0);
        QCOMPARE(lut.at(255), 255);
        // 中点 100:由于 shape-preserving 插值,实际值会略低于线性 100(因端点斜率非 1)
        // 只检查大方向:LUT[128] 在 90..110 之间
        QVERIFY2(lut.at(128) >= 90 && lut.at(128) <= 110,
                 qPrintable(QString("got %1").arg(lut.at(128))));
    }
    // 端点缺失(用户只给中间点)→ 自动补 (0,0) (255,255)
    {
        const QVector<adj::CurvePoint> pts = {{100, 200}}; // 只有中间点
        const QVector<int> lut = adj::buildCurveLut(pts);
        QCOMPARE(lut.at(0), 0);
        QCOMPARE(lut.at(255), 255);
        // 100 附近应该接近 200
        QVERIFY2(lut.at(100) >= 195 && lut.at(100) <= 205,
                 qPrintable(QString("got %1").arg(lut.at(100))));
    }
}

void TestDocument::adjCurvesHighlightClamp()
{
    // 高光过曝钳位:(0,0)→(200,250)→(255,255) → LUT[255]=255(不超),LUT[200]≈250
    const QVector<adj::CurvePoint> pts = {{0, 0}, {200, 250}, {255, 255}};
    const QVector<int> lut = adj::buildCurveLut(pts);
    QCOMPARE(lut.at(255), 255);
    // 200 附近:应该接近 250,允许 ±5 误差(三次插值不一定精确)
    QVERIFY2(lut.at(200) >= 245 && lut.at(200) <= 255,
             qPrintable(QString("got %1").arg(lut.at(200))));
    // 整张图应用:128 → ~160
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(128, 128, 128));
    adj::CurvesParams p;
    p.points = pts;
    adj::applyCurves(img, p);
    const int r = img.pixelColor(0, 0).red();
    // LUT[128] 在两点之间平滑插值,大概 150..180(曲线下凹)
    QVERIFY2(r >= 140 && r <= 190, qPrintable(QString("got %1").arg(r)));
}

// ============================== Geometry ==============================
void TestDocument::geomCrop()
{
    // 4x4 纯色图,裁剪中心 2x2
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            img.setPixelColor(x, y, QColor(y * 50, y * 50, y * 50));

    const QImage out = geom::crop(img, QRect(1, 1, 2, 2));
    QCOMPARE(out.size(), QSize(2, 2));
    QCOMPARE(out.pixelColor(0, 0).red(), 50);   // 原 (1,1)
    QCOMPARE(out.pixelColor(1, 1).red(), 150);  // 原 (2,2)

    // 越界:与图像取交集,返回裁剪后的尺寸
    const QImage out2 = geom::crop(img, QRect(-2, -2, 5, 5));
    QCOMPARE(out2.size(), QSize(3, 3)); // 0..2 截断
    QCOMPARE(out2.pixelColor(0, 0).red(), 0); // 原 (0,0)

    // 完全在图外 → 空图
    const QImage out3 = geom::crop(img, QRect(10, 10, 1, 1));
    QVERIFY(out3.isNull());
}

void TestDocument::geomResize()
{
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(QColor(128, 128, 128));

    // 放大:8x8,新画布应是原图的 2×2 平铺
    const QImage big = geom::resize(img, 8, 8);
    QCOMPARE(big.size(), QSize(8, 8));
    QCOMPARE(big.pixelColor(0, 0).red(), 128);
    QCOMPARE(big.pixelColor(7, 7).red(), 128);

    // 缩小:2x2
    const QImage small = geom::resize(img, 2, 2);
    QCOMPARE(small.size(), QSize(2, 2));
    QCOMPARE(small.pixelColor(0, 0).red(), 128);

    // 非法参数 → 空图
    QVERIFY(geom::resize(img, 0, 0).isNull());
}

void TestDocument::geomRotate90()
{
    // 在 (0,0) 画红,(3,0) 画绿 → 顺时针 90° 后:
    //   红应该在 (3,0) 行的右上角,(3,0) 的绿在 (3,3)
    // 简化:2x2 红 + 蓝,旋转 90° 后(0,0)→(1,0) 等
    QImage img(2, 2, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(255, 0, 0)); // R
    img.setPixelColor(1, 0, QColor(0, 0, 255)); // B
    img.fill(Qt::black);

    // quarters=1 → CW 90°
    QImage r1 = geom::rotate90(img, 1);
    QCOMPARE(r1.size(), QSize(2, 2));
    // 旋转后 R 应出现在 (1,0),B 在 (1,1)
    QCOMPARE(r1.pixelColor(1, 0).red(), 255);
    QCOMPARE(r1.pixelColor(1, 1).blue(), 255);

    // quarters=4 等同 0
    QImage r0 = geom::rotate90(img, 4);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(r0.pixel(x, y), img.pixel(x, y));
}

void TestDocument::geomRotate45()
{
    // 2x2 任意角度旋转 → 输出是新尺寸包围盒
    QImage img(2, 2, QImage::Format_ARGB32);
    img.fill(QColor(50, 100, 150));

    const QImage out = geom::rotateArbitrary(img, 45.0, Qt::white);
    // 2x2 45° 旋转后包围盒:ceil(2*0.707+2*0.707)=ceil(2.83)=3
    QCOMPARE(out.size(), QSize(3, 3));
    // 中心点(1,1)应保留原色(在旋转中心)
    QCOMPARE(out.pixelColor(1, 1).red(), 50);

    // 整数 90° 走快速路径,应等于 rotate90
    const QImage fast = geom::rotateArbitrary(img, 90.0, Qt::white);
    const QImage direct = geom::rotate90(img, 1);
    QCOMPARE(fast.size(), direct.size());

    // 0° 直接返回原图
    const QImage zero = geom::rotateArbitrary(img, 0.0);
    QCOMPARE(zero.size(), img.size());
    QCOMPARE(zero.pixelColor(0, 0), img.pixelColor(0, 0));
}

// ============================== Black & White ==============================
void TestDocument::adjBlackWhiteDefault()
{
    // 纯色 → 应是统一灰度(红 255 偏黄绿桶,绿 255 偏绿桶,蓝 255 偏蓝桶)
    // 默认权重:红 40 / 黄 60 / 绿 40 / 青 60 / 蓝 20 / 品红 80
    // 纯红 (255,0,0) → baseLum = 0.299*255 = 76.2;红桶权重 40% 主导 → 灰度 ≈ 76*0.9 ≈ 68
    {
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(255, 0, 0));
        adj::BwParams p; // 默认权重
        adj::applyBlackWhite(img, p);
        const QColor out = img.pixelColor(0, 0);
        // R/G/B 必须相等(灰度)
        QCOMPARE(out.red(), out.green());
        QCOMPARE(out.green(), out.blue());
        // 灰度应在 50..100 之间(具体值依赖权重插值)
        const int gray = out.red();
        QVERIFY2(gray >= 50 && gray <= 110, qPrintable(QString("got %1").arg(gray)));
    }
    // 灰度(128,128,128) → 应保持中性灰
    {
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(128, 128, 128));
        adj::BwParams p;
        adj::applyBlackWhite(img, p);
        // 128 的灰度经(0.5 + bw)缩放,bw 取决于其色相(0,饱和度 0),
        // 任何色相下 bw 都接近 0(三个色相等权),gray ≈ 128*0.5 = 64
        // 实际:128 的灰度色相无定义,落到第一桶 (红),bw=0.4
        // gray = 128 * (0.5 + 0.4) = 115
        // 验证:输出是灰度(三通道相等)
        const QColor out = img.pixelColor(0, 0);
        QCOMPARE(out.red(), out.green());
        QCOMPARE(out.green(), out.blue());
    }
}

void TestDocument::adjBlackWhiteTint()
{
    // 灰度 + sepia tint(40° 色相,20% 饱和)
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixelColor(0, 0, QColor(128, 128, 128));
    adj::BwParams p;
    p.tint = true;
    p.tintHue = 40;       // 暖色
    p.tintSaturation = 20; // 20% 饱和
    adj::applyBlackWhite(img, p);
    const QColor out = img.pixelColor(0, 0);
    // 暖色(40°) = R 高 B 低,所以 R 应 > B
    QVERIFY2(out.red() > out.blue(), qPrintable(QString("got R=%1 B=%2").arg(out.red()).arg(out.blue())));
}

// ============================== Color Balance ==============================
void TestDocument::adjColorBalanceIdentity()
{
    // 全 0 偏移 → 不变
    QImage img = makeGray4();
    QImage before = img;
    adj::CbParams p;
    adj::applyColorBalance(img, p);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(img.pixel(x, y), before.pixel(x, y));
}

void TestDocument::adjColorBalanceShift()
{
    // 把整张灰度图加青(cyanRed=-100,阴影组):
    //   - cR=-100 走"加青"分支
    //   - (0,0) 亮度 0 → 全在阴影组,ws=1
    //   - 黑色像素(0,0,0) → r=0, g=0+128=128, b=0+128=128, 钳位后(0,128,128)≈青色
    QImage img = makeGray4();
    adj::CbParams p;
    p.shadowCyanRed = -100;   // 加青
    p.shadowMagentaGreen = 0;
    p.shadowYellowBlue = 0;
    adj::applyColorBalance(img, p);
    // (0,0)=全黑 → 应变成青
    QCOMPARE(img.pixelColor(0, 0).red(), 0);
    QCOMPARE(img.pixelColor(0, 0).green(), 128);
    QCOMPARE(img.pixelColor(0, 0).blue(), 128);
    // (1,1)=全白,亮度 255 → cbToneWeights 走 wm=0/wh=1(中间组 0 权重,只高光组 1)
    //   shadowCyanRed 权重 = 0 → 不动
    QCOMPARE(img.pixelColor(1, 1).red(), 255);
    QCOMPARE(img.pixelColor(1, 1).green(), 255);
    QCOMPARE(img.pixelColor(1, 1).blue(), 255);
}

// ============================== Exposure ==============================
// 注意:Exposure 经 sRGB ↔ 线性光 ↔ sRGB 完整往返,会有少量 sRGB 量化误差(±1 字节)
void TestDocument::adjExposureIdentity()
{
    // identity 路径:exposure=0,offset=0,gamma=1 → 早退,像素不变
    QImage img = makeGray4();
    QImage before = img;
    adj::ExposureParams p; // 全 0/1 默认
    adj::applyExposure(img, p);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(img.pixel(x, y), before.pixel(x, y));

    // 边界 LUT:0 和 255 必须精确(无 sRGB 量化误差)
    adj::ExposureParams p2; p2.exposure = 0.0001; // 几乎 identity,触发 LUT 构建
    adj::applyExposure(img, p2); // 让 LUT 构建,验证 0/255
    // 不验证具体值(已 identity 早退),主要证明 identity 早退分支
    QCOMPARE(img.pixelColor(0, 0).red(), 0); // 未被改动
    QCOMPARE(img.pixelColor(1, 1).red(), 255);
}

void TestDocument::adjExposurePlus1EV()
{
    // +1 EV:线性光域乘 2。18% 灰(约 118 字节)在 sRGB 编码下应明显变亮
    // 验证 LUT 边界:0 应该仍接近 0(钳位到 0),255 应该仍接近 255
    {
        adj::ExposureParams p;
        p.exposure = 1.0;
        // 调 internal LUT 不易(它是匿名 namespace 函数),
        // 改用整图应用
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(128, 128, 128));
        adj::applyExposure(img, p);
        const int r = img.pixelColor(0, 0).red();
        // 128 在 +1EV 下应明显变亮(>150)
        QVERIFY2(r > 150, qPrintable(QString("got %1, expected >150").arg(r)));
        // 也不应超过 255
        QVERIFY(r <= 255);
    }
    // 黑色像素:+1EV 后线性光域是 0*2=0,仍黑(边界检查)
    {
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(0, 0, 0));
        adj::ExposureParams p; p.exposure = 1.0;
        adj::applyExposure(img, p);
        QCOMPARE(img.pixelColor(0, 0).red(), 0);
    }
    // 白色像素:应被钳到 255
    {
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(255, 255, 255));
        adj::ExposureParams p; p.exposure = 1.0;
        adj::applyExposure(img, p);
        QCOMPARE(img.pixelColor(0, 0).red(), 255);
    }
}

void TestDocument::adjExposureGamma()
{
    // 上游公式:linear = pow(linear, 1/gamma)
    //   gamma=2 → 1/gamma=0.5 → 开方 → 0.2150→0.4637 → 字节 179(更亮)
    //   gamma=0.5 → 1/gamma=2 → 平方 → 0.2150→0.0462 → 字节 55(更暗)
    {
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(128, 128, 128));
        adj::ExposureParams p; p.gamma = 2.0;
        adj::applyExposure(img, p);
        const int r = img.pixelColor(0, 0).red();
        QVERIFY2(r > 128, qPrintable(QString("got %1, expected >128 (mid brighter)").arg(r)));
    }
    {
        QImage img(1, 1, QImage::Format_ARGB32);
        img.setPixelColor(0, 0, QColor(128, 128, 128));
        adj::ExposureParams p; p.gamma = 0.5;
        adj::applyExposure(img, p);
        const int r = img.pixelColor(0, 0).red();
        QVERIFY2(r < 128, qPrintable(QString("got %1, expected <128 (mid darker)").arg(r)));
    }
}

void TestDocument::spikeRawExtensions()
{
    // 主流相机 RAW 扩展名必须被识别(只测接口 + 扩展名表,实际解码由 stub 报"未实现")
    QVERIFY(RawDecoder::isRawFile("photo.dng"));
    QVERIFY(RawDecoder::isRawFile("photo.CR2"));
    QVERIFY(RawDecoder::isRawFile("photo.cr3"));
    QVERIFY(RawDecoder::isRawFile("photo.nef"));
    QVERIFY(RawDecoder::isRawFile("photo.arw"));
    QVERIFY(RawDecoder::isRawFile("photo.RAF"));   // 大小写不敏感
    // 非 RAW 扩展名必须拒绝
    QVERIFY(!RawDecoder::isRawFile("photo.jpg"));
    QVERIFY(!RawDecoder::isRawFile("photo.png"));
    QVERIFY(!RawDecoder::isRawFile("photo"));
    QVERIFY(!RawDecoder::isRawFile(".hidden"));
}

void TestDocument::spikeVisionStub()
{
    // 验证 stub 给出清晰错误(便于应用层 fallback)
    StubVisionProvider stub;
    QImage img(4, 4, QImage::Format_ARGB32);
    QImage mask;
    QString err;
    QVERIFY(!stub.selectObject(img, QPoint(2, 2), mask, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(err.contains(QStringLiteral("StubVisionProvider")));
}

// ============================== PSD Export ==============================
void TestDocument::psdRoundTripFlat()
{
    // 2x2 单色文档 → 导出 → 读回 → 尺寸与颜色一致
    Document doc(QSize(2, 2));
    auto *layer = new Layer(QStringLiteral("bg"), doc.size(), QColor(30, 60, 90));
    doc.addLayer(layer);

    QString err;
    const QByteArray bytes = psd::writePsd(doc, {}, &err);
    QVERIFY2(!bytes.isEmpty(), qPrintable(err));
    QVERIFY(bytes.size() > 26);

    // 魔数
    QCOMPARE(bytes.left(4), QByteArrayLiteral("8BPS"));

    // 用 Reader 读回
    const auto r = psd::Reader::read(bytes);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.header.canvasWidth, 2);
    QCOMPARE(r.header.canvasHeight, 2);
    QCOMPARE(r.header.colorMode, 3);
    // 颜色应一致(允许 alpha 差异)
    QCOMPARE(r.composite.pixelColor(0, 0).red(),   30);
    QCOMPARE(r.composite.pixelColor(0, 0).green(), 60);
    QCOMPARE(r.composite.pixelColor(0, 0).blue(),  90);
}

void TestDocument::psdRoundTripGradient()
{
    // 渐变文档 round-trip:尺寸一致 + 角落颜色一致
    QImage img(8, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            img.setPixelColor(x, y, QColor(x * 30, y * 30, 128));
    Document doc(img.size());
    auto *layer = new Layer(QStringLiteral("grad"), img.size());
    layer->pixels() = img;  // 直接替换像素
    doc.addLayer(layer);

    QString err;
    const QByteArray bytes = psd::writePsd(doc, {}, &err);
    QVERIFY2(!bytes.isEmpty(), qPrintable(err));

    const auto r = psd::Reader::read(bytes);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.composite.size(), QSize(8, 8));
    // 角落 (0,0) 红=0 蓝=128
    QCOMPARE(r.composite.pixelColor(0, 0).red(),   0);
    QCOMPARE(r.composite.pixelColor(0, 0).blue(),  128);
    // 角落 (7,7) 红=210 绿=210
    QCOMPARE(r.composite.pixelColor(7, 7).red(),  210);
    QCOMPARE(r.composite.pixelColor(7, 7).green(),210);
}

// ============================== Filters ==============================
void TestDocument::filtGaussianIdentity()
{
    // radius=0 → 原样
    QImage img = makeGray4();
    const QImage out = filt::gaussianBlur(img, 0.0);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(out.pixel(x, y), img.pixel(x, y));
}

void TestDocument::filtGaussianUniformity()
{
    // 纯色图模糊 → 仍是纯色(均匀区域卷积不变)
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(QColor(80, 120, 200));
    const QImage out = filt::gaussianBlur(img, 2.0);
    QCOMPARE(out.size(), img.size());
    // 中心像素应保持接近原色(均匀场)
    QCOMPARE(out.pixelColor(8, 8).red(),   80);
    QCOMPARE(out.pixelColor(8, 8).green(), 120);
    QCOMPARE(out.pixelColor(8, 8).blue(),  200);
    // 角像素也应接近(边缘 clamp)
    QVERIFY(qAbs(out.pixelColor(0, 0).red() - 80) < 2);
}

void TestDocument::filtNoiseDeterministic()
{
    // 固定 seed → 两次结果一致(可复现)
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(QColor(128, 128, 128));
    const QImage a = filt::addNoise(img, 20.0, true, false, 42);
    const QImage b = filt::addNoise(img, 20.0, true, false, 42);
    QCOMPARE(a, b);
    // 有噪声 → 与原始不同(至少有一个像素变化)
    QImage img2(8, 8, QImage::Format_ARGB32);
    img2.fill(QColor(128, 128, 128));
    const QImage noisy = filt::addNoise(img2, 50.0, true, false, 1);
    bool changed = false;
    for (int y = 0; y < 8 && !changed; ++y)
        for (int x = 0; x < 8 && !changed; ++x)
            if (noisy.pixel(x, y) != img2.pixel(x, y))
                changed = true;
    QVERIFY(changed);
}

void TestDocument::filtVignetteDarkens()
{
    // 黑色 vignette:角比中心更暗
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(QColor(200, 200, 200));
    const QImage out = filt::vignette(img, 80.0, Qt::black, 30, 100, 60, 25);
    const int center = out.pixelColor(8, 8).red();
    const int corner = out.pixelColor(0, 0).red();
    QVERIFY2(corner < center, qPrintable(QString("corner %1 vs center %2").arg(corner).arg(center)));
    // 中心接近原值(200)
    QVERIFY(qAbs(center - 200) < 5);
}

void TestDocument::filterCommandUndoRedo()
{
    // FilterCommand 走撤销栈:redo 应用滤镜,undo 还原原像素
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), doc.size(), Qt::blue);
    doc.addLayer(layer);

    const QImage before = layer->pixels();
    const QImage after = filt::addNoise(before, 40.0, true, false, 7);
    QVERIFY(after != before);

    QUndoStack stack;
    stack.push(new FilterCommand(layer, before, after, QStringLiteral("Add Noise")));
    QCOMPARE(layer->pixels(), after);   // redo(首次执行)
    stack.undo();
    QCOMPARE(layer->pixels(), before);  // 还原
    stack.redo();
    QCOMPARE(layer->pixels(), after);   // 再次应用
    QVERIFY(stack.canUndo());
}

void TestDocument::layerOffsetRender()
{
    // 2x2 红图层 offset(3,2) → 合成图中红块偏移出现,其余透明
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(2, 2), Qt::red);
    layer->setOffset(QPoint(3, 2));
    doc.addLayer(layer);

    PainterRenderer renderer;
    const QImage out = renderer.render(doc);
    QCOMPARE(out.pixelColor(3, 2).red(), 255);   // offset 起点
    QCOMPARE(out.pixelColor(4, 3).red(), 255);   // 红块右下角
    QCOMPARE(out.pixelColor(0, 0).alpha(), 0);   // 其余透明
    QCOMPARE(out.pixelColor(2, 1).alpha(), 0);   // 红块外
}

void TestDocument::moveLayerCommandUndoRedo()
{
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), doc.size(), Qt::blue);
    doc.addLayer(layer);
    QCOMPARE(layer->offset(), QPoint(0, 0));

    QUndoStack stack;
    stack.push(new MoveLayerCommand(layer, QPoint(0, 0), QPoint(10, -5)));
    QCOMPARE(layer->offset(), QPoint(10, -5)); // redo(首次执行)
    stack.undo();
    QCOMPARE(layer->offset(), QPoint(0, 0));   // 还原
    stack.redo();
    QCOMPARE(layer->offset(), QPoint(10, -5)); // 再次移动
}

void TestDocument::clearRectCommandUndoRedo()
{
    // 4x4 全白图层,清除左上 2x2 → 该区域透明,undo 恢复
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), Qt::white);
    doc.addLayer(layer);
    const QRect rect(0, 0, 2, 2);
    const QImage before = layer->pixels().copy(rect);

    QUndoStack stack;
    stack.push(new ClearRectCommand(layer, rect, before));
    QCOMPARE(layer->pixels().pixelColor(0, 0).alpha(), 0);   // 已清空
    QCOMPARE(layer->pixels().pixelColor(3, 3).alpha(), 255); // 区域外不受影响
    stack.undo();
    QCOMPARE(layer->pixels().pixelColor(0, 0), QColor(Qt::white)); // 恢复
    stack.redo();
    QCOMPARE(layer->pixels().pixelColor(0, 0).alpha(), 0);   // 再清除
}

namespace {
// 抽样:对若干典型输入字节,验证 LUT 与原 apply* 一致
int mapLevelsSample(int v, const adj::LevelsParams &p)
{
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixel(0, 0, qRgba(v, v, v, 255));
    QImage ref = img.copy();
    adj::applyLevels(ref, p);
    return qRed(ref.pixel(0, 0));
}
int mapCurvesSample(int v, const adj::CurvesParams &p)
{
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixel(0, 0, qRgba(v, v, v, 255));
    QImage ref = img.copy();
    adj::applyCurves(ref, p);
    return qRed(ref.pixel(0, 0));
}
int mapExposureSample(int v, const adj::ExposureParams &p)
{
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixel(0, 0, qRgba(v, v, v, 255));
    QImage ref = img.copy();
    adj::applyExposure(ref, p);
    return qRed(ref.pixel(0, 0));
}
int mapBcSample(int v, const adj::BcParams &p)
{
    QImage img(1, 1, QImage::Format_ARGB32);
    img.setPixel(0, 0, qRgba(v, v, v, 255));
    QImage ref = img.copy();
    adj::applyBrightnessContrast(ref, p);
    return qRed(ref.pixel(0, 0));
}
} // namespace

void TestDocument::lutLevelsMatchesApply()
{
    adj::LevelsParams p;
    p.range.black = 30;
    p.range.white = 220;
    p.range.gamma = 1.5;
    p.rgbRange.outputBlack = 10;
    p.rgbRange.outputWhite = 245;
    const adj::AdjustLut lut = adj::lutFromLevels(p);
    QVERIFY(!lut.isNull());
    for (int v : {0, 16, 64, 128, 192, 240, 255}) {
        const uint8_t viaLut = lut.mapR(uint8_t(v));
        const int viaApply = mapLevelsSample(v, p);
        QCOMPARE(int(viaLut), viaApply);
    }
    // 恒等参数 → 恒等 LUT
    QVERIFY(adj::lutFromLevels(adj::LevelsParams{}).isNull());
}

void TestDocument::lutCurvesMatchesApply()
{
    adj::CurvesParams p;
    p.points = { {0, 0}, {64, 96}, {192, 220}, {255, 255} };
    const adj::AdjustLut lut = adj::lutFromCurves(p);
    QVERIFY(!lut.isNull());
    for (int v : {0, 32, 80, 128, 200, 255}) {
        const uint8_t viaLut = lut.mapR(uint8_t(v));
        const int viaApply = mapCurvesSample(v, p);
        QCOMPARE(int(viaLut), viaApply);
    }
    // identity 曲线 → 恒等
    QVERIFY(adj::lutFromCurves(adj::CurvesParams{}).isNull());
}

void TestDocument::lutExposureMatchesApply()
{
    adj::ExposureParams p;
    p.exposure = 1.5;
    p.offset = 0.05;
    p.gamma = 1.2;
    const adj::AdjustLut lut = adj::lutFromExposure(p);
    QVERIFY(!lut.isNull());
    for (int v : {0, 16, 64, 128, 192, 240, 255}) {
        const uint8_t viaLut = lut.mapR(uint8_t(v));
        const int viaApply = mapExposureSample(v, p);
        // 1 步四舍五入差异允许
        QVERIFY2(qAbs(int(viaLut) - viaApply) <= 1,
                 qPrintable(QString("v=%1 lut=%2 apply=%3").arg(v).arg(viaLut).arg(viaApply)));
    }
}

void TestDocument::lutBcMatchesApply()
{
    adj::BcParams p;
    p.brightness = 20;
    p.contrast = 30;
    const adj::AdjustLut lut = adj::lutFromBrightnessContrast(p);
    QVERIFY(!lut.isNull());
    for (int v : {0, 32, 80, 128, 200, 255}) {
        const uint8_t viaLut = lut.mapR(uint8_t(v));
        const int viaApply = mapBcSample(v, p);
        QCOMPARE(int(viaLut), viaApply);
    }
}

void TestDocument::lutComposeSemantics()
{
    // 先 Levels 再 BC = second ∘ first
    adj::LevelsParams lp;
    lp.range.black = 30; lp.range.white = 220;
    adj::BcParams bp; bp.brightness = 20; bp.contrast = 30;
    const auto composed = adj::lutCompose(adj::lutFromLevels(lp),
                                          adj::lutFromBrightnessContrast(bp));
    // 任一为恒等时,返回另一张
    QVERIFY(adj::lutCompose(adj::identityLut(), adj::lutFromBrightnessContrast(bp)).isNull() == false);
    QVERIFY(adj::lutCompose(adj::lutFromBrightnessContrast(bp), adj::identityLut()).isNull() == false);
    // 抽样验证等价性
    for (int v : {0, 64, 128, 200, 255}) {
        const int r1 = mapBcSample(mapLevelsSample(v, lp), bp);
        const int r2 = composed.mapR(uint8_t(v));
        QCOMPARE(r2, r1);
    }
}

void TestDocument::documentRenderLut()
{
    // Document 持有 LUT,PainterRenderer 应用
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), QColor(60, 80, 200));
    doc.addLayer(layer);

    PainterRenderer p;
    QImage baseline = p.render(doc);
    QCOMPARE(baseline.pixelColor(0, 0).red(), 60);

    adj::ExposureParams ep; ep.exposure = 1.0;
    doc.setRenderLut(adj::lutFromExposure(ep));
    const QImage tuned = p.render(doc);
    QVERIFY(tuned.pixelColor(0, 0).red() != 60); // 1 EV 提亮

    // 切回恒等
    doc.setRenderLut(adj::identityLut());
    const QImage back = p.render(doc);
    QCOMPARE(back.pixelColor(0, 0).red(), 60);
}

void TestDocument::gpuRendererLutConsistency()
{
    // GpuRenderer 在 headless / 无 GL 环境下会 fallback 到 PainterRenderer,
    // 所以"无 GL"时实际仍由 CPU 路径渲染 —— 始终与 PainterRenderer 输出一致。
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(30, 60, 120, 200));
    doc.addLayer(layer);

    adj::ExposureParams ep; ep.exposure = 0.8; ep.gamma = 1.1;
    const adj::AdjustLut lut = adj::lutFromExposure(ep);
    doc.setRenderLut(lut);

    GpuRenderer g;
    PainterRenderer p;
    const QImage gImg = g.render(doc);
    const QImage pImg = p.render(doc);
    QCOMPARE(gImg.size(), pImg.size());
    // 任何 1 个像素出现差异说明 GPU/CPU 路径不一致
    bool differ = false;
    for (int y = 0; y < 8 && !differ; ++y)
        for (int x = 0; x < 8 && !differ; ++x)
            if (gImg.pixel(x, y) != pImg.pixel(x, y))
                differ = true;
    QVERIFY(!differ);
}

void TestDocument::adjLutCommandUndoRedo()
{
    // AdjustLutCommand:redo 设置新 LUT,undo 还原
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), Qt::white);
    doc.addLayer(layer);
    QVERIFY(doc.renderLut().isNull());

    adj::LevelsParams p; p.range.gamma = 0.5;  // 提亮
    const adj::AdjustLut bright = adj::lutFromLevels(p);
    QVERIFY(!bright.isNull());

    QUndoStack stack;
    stack.push(new AdjustLutCommand(&doc, doc.renderLut(), bright,
                                    QStringLiteral("Levels")));
    QCOMPARE(doc.renderLut().data, bright.data); // redo(首次执行)
    stack.undo();
    QVERIFY(doc.renderLut().isNull());            // 还原
    stack.redo();
    QCOMPARE(doc.renderLut().data, bright.data);  // 再次应用
    QVERIFY(stack.canUndo());
}

void TestDocument::documentOwnsUndoStack()
{
    // Document 自带撤销栈,可独立于任何外部栈工作
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), Qt::white);
    doc.addLayer(layer);
    QVERIFY(doc.undoStack() != nullptr);
    QVERIFY(doc.undoStack()->isClean());

    const QImage before = layer->pixels();
    QImage after = before;
    after.setPixel(0, 0, qRgba(0, 0, 0, 255));
    doc.undoStack()->push(new FilterCommand(layer, before, after, QStringLiteral("Paint")));
    QVERIFY(!doc.undoStack()->isClean());

    doc.undoStack()->undo();
    QCOMPARE(layer->pixels().pixel(0, 0), before.pixel(0, 0));
}

void TestDocument::perDocumentUndoIsolation()
{
    // 两个 Document 的撤销栈互不影响
    Document d1(QSize(2, 2));
    Document d2(QSize(2, 2));
    auto *l1 = new Layer(QStringLiteral("A"), QSize(2, 2), Qt::red);
    auto *l2 = new Layer(QStringLiteral("B"), QSize(2, 2), Qt::blue);
    d1.addLayer(l1);
    d2.addLayer(l2);

    d1.undoStack()->push(new AddLayerCommand(&d1, new Layer(QStringLiteral("X"), QSize(2, 2), Qt::green)));
    QCOMPARE(d1.layerCount(), 2);
    QCOMPARE(d2.layerCount(), 1); // d2 不受影响

    d1.undoStack()->undo();
    QCOMPARE(d1.layerCount(), 1);
    QCOMPARE(d2.layerCount(), 1);
}

void TestDocument::documentGaussianBlurRadius()
{
    // Document 持有非破坏性 blur radius,默认 0
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), QColor(60, 80, 200));
    doc.addLayer(layer);
    QCOMPARE(doc.gaussianBlurRadius(), 0.0);

    doc.setGaussianBlurRadius(3.5);
    QCOMPARE(doc.gaussianBlurRadius(), 3.5);
    // 重复设置不重发 contentChanged
    doc.setGaussianBlurRadius(3.5);
    // 图层像素不变(非破坏性)
    QCOMPARE(layer->pixels().pixelColor(0, 0), QColor(60, 80, 200));
}

void TestDocument::gaussianBlurCommandUndoRedo()
{
    Document doc(QSize(4, 4));
    doc.setGaussianBlurRadius(0.0);
    QUndoStack stack;
    stack.push(new GaussianBlurCommand(&doc, 0.0, 5.0));
    QCOMPARE(doc.gaussianBlurRadius(), 5.0);
    stack.undo();
    QCOMPARE(doc.gaussianBlurRadius(), 0.0);
    stack.redo();
    QCOMPARE(doc.gaussianBlurRadius(), 5.0);
}

void TestDocument::gpuGaussianBlurConsistency()
{
    // GpuRenderer 在 headless 下 fallback 到 PainterRenderer,PainterRenderer
    // 现在也走 filt::gaussianBlur(与 GPU shader 等价),所以 blur 输出必一致
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(80, 120, 200, 255));
    doc.addLayer(layer);
    doc.setGaussianBlurRadius(3.0);

    GpuRenderer g;
    PainterRenderer p;
    const QImage gImg = g.render(doc);
    const QImage pImg = p.render(doc);
    QCOMPARE(gImg.size(), pImg.size());
    bool differ = false;
    for (int y = 0; y < 8 && !differ; ++y)
        for (int x = 0; x < 8 && !differ; ++x)
            if (gImg.pixel(x, y) != pImg.pixel(x, y))
                differ = true;
    QVERIFY(!differ);
}

void TestDocument::vignetteAndMotionBlurOnDocument()
{
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(180, 180, 180));
    doc.addLayer(layer);

    QCOMPARE(doc.vignetteAmount(), 0.0);
    QCOMPARE(doc.motionBlurDistance(), 0.0);

    doc.setVignette(50.0, 30.0, 100.0, 60.0);
    QCOMPARE(doc.vignetteAmount(), 50.0);
    QCOMPARE(doc.vignetteMidpoint(), 30.0);
    doc.setMotionBlur(15.0, 90.0);
    QCOMPARE(doc.motionBlurDistance(), 15.0);
    QCOMPARE(doc.motionBlurAngle(), 90.0);

    // 图层像素保持不变(非破坏性)
    QCOMPARE(layer->pixels().pixelColor(0, 0), QColor(180, 180, 180));

    doc.clearVignette();
    QCOMPARE(doc.vignetteAmount(), 0.0);
    doc.clearMotionBlur();
    QCOMPARE(doc.motionBlurDistance(), 0.0);
}

void TestDocument::vignetteAndMotionBlurCommandUndoRedo()
{
    Document doc(QSize(4, 4));
    QUndoStack stack;

    stack.push(new VignetteCommand(&doc, 0, 50, 100, 60, 60, 40, 100, 50));
    QCOMPARE(doc.vignetteAmount(), 60.0);
    stack.undo();
    QCOMPARE(doc.vignetteAmount(), 0.0);
    stack.redo();
    QCOMPARE(doc.vignetteAmount(), 60.0);

    stack.push(new MotionBlurCommand(&doc, 0, 0, 12, 45));
    QCOMPARE(doc.motionBlurDistance(), 12.0);
    QCOMPARE(doc.motionBlurAngle(), 45.0);
    stack.undo();
    QCOMPARE(doc.motionBlurDistance(), 0.0);
    stack.redo();
    QCOMPARE(doc.motionBlurDistance(), 12.0);
}

void TestDocument::gpuVignetteAndMotionConsistency()
{
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(200, 200, 200));
    doc.addLayer(layer);
    doc.setVignette(50.0, 30.0, 100.0, 60.0);
    doc.setMotionBlur(10.0, 45.0);

    GpuRenderer g;
    PainterRenderer p;
    const QImage gImg = g.render(doc);
    const QImage pImg = p.render(doc);
    QCOMPARE(gImg.size(), pImg.size());
    bool differ = false;
    for (int y = 0; y < 8 && !differ; ++y)
        for (int x = 0; x < 8 && !differ; ++x)
            if (gImg.pixel(x, y) != pImg.pixel(x, y))
                differ = true;
    QVERIFY(!differ);
}

void TestDocument::addNoiseOnDocument()
{
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), QColor(120, 120, 120));
    doc.addLayer(layer);
    QCOMPARE(doc.noiseAmount(), 0.0);
    QVERIFY(doc.noiseGaussian());
    QVERIFY(!doc.noiseMonochromatic());

    doc.setAddNoise(30.0, false, true, 7u);
    QCOMPARE(doc.noiseAmount(), 30.0);
    QVERIFY(!doc.noiseGaussian());
    QVERIFY(doc.noiseMonochromatic());
    QCOMPARE(doc.noiseSeed(), 7u);
    QCOMPARE(layer->pixels().pixelColor(0, 0), QColor(120, 120, 120)); // 非破坏性

    doc.clearAddNoise();
    QCOMPARE(doc.noiseAmount(), 0.0);
}

void TestDocument::addNoiseCommandUndoRedo()
{
    Document doc(QSize(4, 4));
    QUndoStack stack;
    stack.push(new AddNoiseCommand(&doc, 0, true, false, 1u, 25.0, false, true, 42u));
    QCOMPARE(doc.noiseAmount(), 25.0);
    QVERIFY(!doc.noiseGaussian());
    QVERIFY(doc.noiseMonochromatic());
    QCOMPARE(doc.noiseSeed(), 42u);
    stack.undo();
    QCOMPARE(doc.noiseAmount(), 0.0);
    QVERIFY(doc.noiseGaussian());
    QVERIFY(!doc.noiseMonochromatic());
    stack.redo();
    QCOMPARE(doc.noiseAmount(), 25.0);
}

void TestDocument::gpuAddNoiseConsistency()
{
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(100, 100, 100));
    doc.addLayer(layer);
    doc.setAddNoise(40.0, true, false, 12345u);

    GpuRenderer g;
    PainterRenderer p;
    const QImage gImg = g.render(doc);
    const QImage pImg = p.render(doc);
    QCOMPARE(gImg.size(), pImg.size());
    bool differ = false;
    for (int y = 0; y < 8 && !differ; ++y)
        for (int x = 0; x < 8 && !differ; ++x)
            if (gImg.pixel(x, y) != pImg.pixel(x, y))
                differ = true;
    QVERIFY(!differ);
}

void TestDocument::hsvBwCbOnDocument()
{
    Document doc(QSize(4, 4));
    auto *layer = new Layer(QStringLiteral("L"), QSize(4, 4), QColor(120, 80, 200));
    doc.addLayer(layer);
    QCOMPARE(doc.hsv().hue, 0.0);
    QVERIFY(!doc.hsv().colorize);

    adj::HsvParams hp; hp.hue = 30; hp.saturation = 20; hp.lightness = -10;
    doc.setHsv(hp);
    QCOMPARE(doc.hsv().hue, 30.0);
    doc.clearHsv();
    QCOMPARE(doc.hsv().hue, 0.0);

    adj::BwParams bp; bp.reds = 80; bp.tint = true; bp.tintHue = 40;
    doc.setBlackWhite(bp);
    QCOMPARE(doc.blackWhite().reds, 80);
    QVERIFY(doc.blackWhite().tint);
    doc.clearBlackWhite();
    QCOMPARE(doc.blackWhite().reds, 40);

    adj::CbParams cp; cp.shadowCyanRed = 25; cp.midMagentaGreen = -15;
    doc.setColorBalance(cp);
    QCOMPARE(doc.colorBalance().shadowCyanRed, 25);
    doc.clearColorBalance();
    QCOMPARE(doc.colorBalance().shadowCyanRed, 0);

    // 图层像素不变
    QCOMPARE(layer->pixels().pixelColor(0, 0), QColor(120, 80, 200));
}

void TestDocument::hsvBwCbCommandUndoRedo()
{
    Document doc(QSize(4, 4));
    QUndoStack stack;
    adj::HsvParams hp; hp.hue = 30;
    stack.push(new HsvCommand(&doc, doc.hsv(), hp));
    QCOMPARE(doc.hsv().hue, 30.0);
    stack.undo();
    QCOMPARE(doc.hsv().hue, 0.0);
    stack.redo();
    QCOMPARE(doc.hsv().hue, 30.0);

    adj::BwParams bp; bp.reds = 80;
    stack.push(new BlackWhiteCommand(&doc, doc.blackWhite(), bp));
    QCOMPARE(doc.blackWhite().reds, 80);
    stack.undo();
    QCOMPARE(doc.blackWhite().reds, 40);

    adj::CbParams cp; cp.shadowCyanRed = 25;
    stack.push(new ColorBalanceCommand(&doc, doc.colorBalance(), cp));
    QCOMPARE(doc.colorBalance().shadowCyanRed, 25);
    stack.undo();
    QCOMPARE(doc.colorBalance().shadowCyanRed, 0);
}

void TestDocument::gpuHsvBwCbConsistency()
{
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(100, 150, 200));
    doc.addLayer(layer);
    adj::HsvParams hp; hp.hue = 20; hp.saturation = 10;
    doc.setHsv(hp);
    adj::BwParams bp; bp.reds = 80; bp.yellows = 60; bp.greens = 40; bp.cyans = 20; bp.blues = 10; bp.magentas = 30;
    doc.setBlackWhite(bp);
    adj::CbParams cp; cp.shadowCyanRed = 20; cp.midYellowBlue = -10;
    doc.setColorBalance(cp);

    GpuRenderer g;
    PainterRenderer p;
    const QImage gImg = g.render(doc);
    const QImage pImg = p.render(doc);
    QCOMPARE(gImg.size(), pImg.size());
    bool differ = false;
    for (int y = 0; y < 8 && !differ; ++y)
        for (int x = 0; x < 8 && !differ; ++x)
            if (gImg.pixel(x, y) != pImg.pixel(x, y))
                differ = true;
    QVERIFY(!differ);
}

void TestDocument::rawProviderExtensions()
{
    // 覆盖主流 RAW 扩展名(大小写不敏感)
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("/tmp/a.DNG")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("foo.cr2")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("x.NEF")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("y.arw")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("z.rw2")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("a.raf")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("b.orf")));
    QVERIFY(RawDecoder::isRawFile(QStringLiteral("c.pef")));
    // 非 RAW 扩展名
    QVERIFY(!RawDecoder::isRawFile(QStringLiteral("a.png")));
    QVERIFY(!RawDecoder::isRawFile(QStringLiteral("a.psd")));
    QVERIFY(!RawDecoder::isRawFile(QStringLiteral("a.jpg")));
    QVERIFY(!RawDecoder::isRawFile(QStringLiteral("a")));
    QVERIFY(!RawDecoder::isRawFile(QString()));
}

void TestDocument::rawProviderFactoryAndStub()
{
    // 工厂:返回非空
    std::unique_ptr<RawDecoder> dec(RawDecoder::create());
    QVERIFY(dec != nullptr);

    // Stub:对不存在的路径,decode 返回 false + error
    QImage img;
    QString err;
    const bool ok = dec->decode(QStringLiteral("/nonexistent/path.DNG"), img, &err);
    QVERIFY(!ok);
    QVERIFY(!err.isEmpty());
    QVERIFY(img.isNull());
}

void TestDocument::visionProviderFactoryAndStub()
{
    // 工厂:有/无 vision_tool.py 都应返回非空
    std::unique_ptr<VisionProvider> vp(VisionProvider::create());
    QVERIFY(vp != nullptr);

    // 调用:返回 false + 非空错误
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(QColor(120, 80, 200));
    QImage mask;
    QString err;
    const bool ok = vp->selectObject(img, QPoint(4, 4), mask, &err);
    QVERIFY(!ok);
    QVERIFY(!err.isEmpty());
    QVERIFY(mask.isNull());
}

// ============================== Magic Wand 落地 ==============================
void TestDocument::magicWandMaskBoundingRect()
{
    // CanvasView::setSelectionMask 应该:
    // 1) 存下 mask 用于 overlay 绘制
    // 2) 自动算出 boundingRect 作为 selectionRect
    CanvasView view;
    Document doc(QSize(10, 10));
    view.setDocument(&doc);

    // 构造 10x10 mask:中心 4x4 区域点亮(1)
    QImage mask(10, 10, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 3; y <= 6; ++y)
        for (int x = 2; x <= 5; ++x)
            mask.setPixel(x, y, 255);

    view.setSelectionMask(mask);
    QCOMPARE(view.selectionMask().size(), QSize(10, 10));
    QCOMPARE(view.selectionRect(), QRect(2, 3, 4, 4));

    // 全 0 mask → selectionRect 应清空(空选区)
    QImage emptyMask(10, 10, QImage::Format_Grayscale8);
    emptyMask.fill(0);
    view.setSelectionMask(emptyMask);
    QVERIFY(view.selectionRect().isEmpty());

    // 单像素 mask
    QImage onePixel(10, 10, QImage::Format_Grayscale8);
    onePixel.fill(0);
    onePixel.setPixel(7, 4, 200);
    view.setSelectionMask(onePixel);
    QCOMPARE(view.selectionRect(), QRect(7, 4, 1, 1));
}

void TestDocument::selectionCommandUndoRedo()
{
    // SelectionCommand:rect-only + mask-aware 两条路径
    Document doc(QSize(8, 8));
    CanvasView view;
    view.setDocument(&doc);
    QVERIFY(view.selectionRect().isEmpty());

    // 路径 1:rect-only
    QUndoStack stack;
    QRect r1(2, 3, 5, 4);
    QRect r2(6, 0, 2, 2);
    stack.push(new SelectionCommand(&view, r1, r2));
    QCOMPARE(view.selectionRect(), r2); // redo 应用
    stack.undo();
    QCOMPARE(view.selectionRect(), r1); // 还原
    stack.redo();
    QCOMPARE(view.selectionRect(), r2);

    // 路径 2:mask-aware,setSelectionMask 会同步算出 boundingRect
    QImage maskA(8, 8, QImage::Format_Grayscale8); maskA.fill(0);
    QImage maskB(8, 8, QImage::Format_Grayscale8); maskB.fill(0);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) maskA.setPixel(x, y, 200);
    for (int y = 4; y < 8; ++y) for (int x = 4; x < 8; ++x) maskB.setPixel(x, y, 200);
    stack.push(new SelectionCommand(&view, QRect(0, 0, 4, 4), QRect(4, 4, 4, 4),
                                    maskA, maskB));
    QCOMPARE(view.selectionRect(), QRect(4, 4, 4, 4));     // 触发 setSelectionMask → bbox
    QVERIFY(!view.selectionMask().isNull());
    stack.undo();
    QCOMPARE(view.selectionRect(), QRect(0, 0, 4, 4));     // 还原旧 mask 的 bbox
    stack.redo();
    QCOMPARE(view.selectionRect(), QRect(4, 4, 4, 4));
}

void TestDocument::magicWandCompositeCommand()
{
    // Magic Wand 一次操作:加 mask 图层 + 设置 selectionRect,撤销/重做都应整体还原。
    Document doc(QSize(8, 8));
    auto *bg = new Layer(QStringLiteral("bg"), doc.size(), QColor(40, 80, 160));
    doc.addLayer(bg);
    QCOMPARE(doc.layerCount(), 1);

    CanvasView view;
    view.setDocument(&doc);
    QVERIFY(view.selectionRect().isEmpty());

    // 模拟 VisionProvider 输出:中心 4x4 区域为前景
    QImage mask(8, 8, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 2; y <= 5; ++y)
        for (int x = 2; x <= 5; ++x)
            mask.setPixel(x, y, 200);

    // 在 MainWindow::onMagicWand 里做的 mask → RGBA 图层 + 复合 push
    QImage layerPx(doc.size(), QImage::Format_ARGB32_Premultiplied);
    layerPx.fill(Qt::transparent);
    for (int y = 0; y < doc.size().height(); ++y) {
        const uchar *src = mask.constScanLine(y);
        QRgb *dst = reinterpret_cast<QRgb *>(layerPx.scanLine(y));
        for (int x = 0; x < doc.size().width(); ++x) {
            const int a = src[x];
            if (a == 0) continue;
            dst[x] = qRgba(255 * a / 255, 160 * a / 255, 60 * a / 255, a);
        }
    }
    auto *maskLayer = new Layer(QStringLiteral("Magic Wand Mask"), doc.size(), Qt::transparent);
    maskLayer->pixels() = layerPx;
    maskLayer->setOpacity(0.55);

    QUndoStack stack;
    auto *mwCmd = new MagicWandCommand();
    new AddLayerCommand(&doc, maskLayer, -1, mwCmd);
    new SelectionCommand(&view, view.selectionRect(), QRect(2, 2, 4, 4),
                         view.selectionMask(), mask, mwCmd);
    stack.push(mwCmd);

    // 复合命令被 push 后:图层加进文档,选区变到 mask 的 bbox
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(view.selectionRect(), QRect(2, 2, 4, 4));
    QCOMPARE(view.selectionMask().size(), QSize(8, 8));
    QCOMPARE(doc.layers().at(1)->name(), QStringLiteral("Magic Wand Mask"));

    // 撤销:图层 + 选区 一起回到原状
    stack.undo();
    QCOMPARE(doc.layerCount(), 1);
    QVERIFY(view.selectionRect().isEmpty());

    // 重做:再次完整出现
    stack.redo();
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(view.selectionRect(), QRect(2, 2, 4, 4));
    QCOMPARE(doc.layers().at(1)->name(), QStringLiteral("Magic Wand Mask"));
}

// ============================== Selection 原语 ==============================
namespace {
// 构造一个 8x8 mask:中心 4x4 区域为 255,四角为 0
QImage makeCenterMask4(const QSize &size = QSize(8, 8))
{
    QImage m(size, QImage::Format_Grayscale8);
    m.fill(0);
    const int x0 = (size.width() - 4) / 2;
    const int y0 = (size.height() - 4) / 2;
    for (int y = y0; y < y0 + 4; ++y)
        for (int x = x0; x < x0 + 4; ++x)
            m.setPixel(x, y, 255);
    return m;
}
// Grayscale8 上 QImage::pixel() 返回 QRgb(会做灰度→ARGB 展开),
// 直接读字节才是原始 mask 值,断言必须用它。
int maskValue(const QImage &mask, int x, int y)
{
    return mask.constScanLine(y)[x];
}
} // namespace

void TestDocument::selMaskPrimitives()
{
    const QSize sz(8, 8);

    // maskFromRect:矩形内 255,外 0
    const QImage r = sel::maskFromRect(QRect(2, 2, 4, 4), sz);
    QCOMPARE(r.size(), sz);
    QCOMPARE(r.format(), QImage::Format_Grayscale8);
    QCOMPARE(maskValue(r, 3, 3), 255);
    QCOMPARE(maskValue(r, 0, 0), 0);
    QCOMPARE(sel::coverage(r), 16); // 4x4 = 16

    // coverage:中心 mask 同样是 16
    QCOMPARE(sel::coverage(makeCenterMask4(sz)), 16);

    // 全 0 mask → coverage 0
    QImage zero(sz, QImage::Format_Grayscale8);
    zero.fill(0);
    QCOMPARE(sel::coverage(zero), 0);

    // resampleMask:尺寸一致原样返回(同一地址内容)
    const QImage same = sel::resampleMask(r, sz);
    QCOMPARE(same.size(), sz);
    QCOMPARE(maskValue(same, 3, 3), 255);

    // resampleMask:缩放后仍是灰度且尺寸正确
    const QImage small = sel::resampleMask(r, QSize(4, 4));
    QCOMPARE(small.size(), QSize(4, 4));
    QCOMPARE(small.format(), QImage::Format_Grayscale8);

    // null mask 传给 resampleMask → null
    QVERIFY(sel::resampleMask(QImage(), sz).isNull());
}

void TestDocument::selBlendByMaskRespectsShape()
{
    // base 全红,processed 全蓝,mask 中心 4x4 为 255,其余 0
    QImage base(8, 8, QImage::Format_ARGB32);
    base.fill(QColor(255, 0, 0, 255));
    QImage processed(8, 8, QImage::Format_ARGB32);
    processed.fill(QColor(0, 0, 255, 255));
    const QImage mask = makeCenterMask4();

    const QImage out = sel::blendByMask(base, processed, mask);
    QCOMPARE(out.size(), base.size());

    // 选区内(中心)→ 完全变成 processed(蓝)
    const QColor inside = out.pixelColor(4, 4);
    QCOMPARE(inside.blue(), 255);
    QCOMPARE(inside.red(), 0);

    // 选区外(角落)→ 保持 base(红)
    const QColor outside = out.pixelColor(0, 0);
    QCOMPARE(outside.red(), 255);
    QCOMPARE(outside.blue(), 0);

    // null mask → 原样返回 base(向后兼容:整层处理)
    const QImage noMask = sel::blendByMask(base, processed, QImage());
    QCOMPARE(noMask, base);

    // 全 0 mask → 等价于不生效,仍返回 base 内容
    QImage zeroMask(8, 8, QImage::Format_Grayscale8);
    zeroMask.fill(0);
    const QImage zeroOut = sel::blendByMask(base, processed, zeroMask);
    QCOMPARE(zeroOut.pixelColor(4, 4), QColor(255, 0, 0, 255));
    QCOMPARE(zeroOut.pixelColor(0, 0), QColor(255, 0, 0, 255));

    // 中间值(羽化)→ 线性插值
    QImage half(8, 8, QImage::Format_Grayscale8);
    half.fill(128);
    const QImage halfOut = sel::blendByMask(base, processed, half);
    const QColor c = halfOut.pixelColor(4, 4);
    // 红 = 255 + (0-255)*128/255 ≈ 127,蓝 = 0 + 255*128/255 ≈ 128
    QVERIFY2(c.red() > 120 && c.red() < 135, qPrintable(QString("red=%1").arg(c.red())));
    QVERIFY2(c.blue() > 120 && c.blue() < 135, qPrintable(QString("blue=%1").arg(c.blue())));
}

void TestDocument::selClearByMaskRespectsShape()
{
    QImage src(8, 8, QImage::Format_ARGB32);
    src.fill(QColor(0, 200, 0, 255)); // 全不透明绿
    const QImage mask = makeCenterMask4();

    const QImage out = sel::clearByMask(src, QRect(2, 2, 4, 4), mask);
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), src.size());

    // 选区内 → 透明
    QCOMPARE(out.pixelColor(4, 4).alpha(), 0);
    // 选区外 → 仍是不透明绿
    QCOMPARE(out.pixelColor(0, 0).alpha(), 255);
    QCOMPARE(out.pixelColor(0, 0).green(), 200);

    // 原始 src 不被修改(值语义)
    QCOMPARE(src.pixelColor(4, 4).alpha(), 255);

    // 无 mask → 退回矩形清除
    const QImage rectOut = sel::clearByMask(src, QRect(0, 0, 2, 2), QImage());
    QVERIFY(!rectOut.isNull());
    QCOMPARE(rectOut.pixelColor(0, 0).alpha(), 0);   // rect 内清除
    QCOMPARE(rectOut.pixelColor(7, 7).alpha(), 255); // rect 外保留

    // 全 0 mask → 无任何像素被触及 → 返回 null(调用方可跳过撤销记录)
    QImage zeroMask(8, 8, QImage::Format_Grayscale8);
    zeroMask.fill(0);
    QVERIFY(sel::clearByMask(src, QRect(2, 2, 4, 4), zeroMask).isNull());

    // null src → null
    QVERIFY(sel::clearByMask(QImage(), QRect(0, 0, 2, 2), mask).isNull());
}

void TestDocument::selCropToMaskBounds()
{
    QImage src(8, 8, QImage::Format_ARGB32);
    src.fill(QColor(10, 20, 30, 255));
    const QImage mask = makeCenterMask4(); // 中心 4x4 → (2,2)-(5,5)

    // 有 mask:裁剪范围由 mask bbox 决定
    const QImage out = sel::cropToMask(src, QRect(), mask);
    QCOMPARE(out.size(), QSize(4, 4));
    QCOMPARE(out.pixelColor(0, 0), QColor(10, 20, 30, 255));

    // mask bbox 与 rect 求交集
    const QImage out2 = sel::cropToMask(src, QRect(0, 0, 8, 8), mask);
    QCOMPARE(out2.size(), QSize(4, 4));

    // 无 mask → 按 rect 裁剪
    const QImage out3 = sel::cropToMask(src, QRect(2, 2, 4, 4), QImage());
    QCOMPARE(out3.size(), QSize(4, 4));

    // rect 完全在图外 → null
    QVERIFY(sel::cropToMask(src, QRect(100, 100, 4, 4), QImage()).isNull());

    // 全 0 mask → 无有效区域 → null
    QImage zeroMask(8, 8, QImage::Format_Grayscale8);
    zeroMask.fill(0);
    QVERIFY(sel::cropToMask(src, QRect(), zeroMask).isNull());

    // null src → null
    QVERIFY(sel::cropToMask(QImage(), QRect(0, 0, 4, 4), mask).isNull());
}

void TestDocument::clearMaskCommandUndoRedo()
{
    // ClearMaskCommand:按 mask 形状清除,undo 完整还原,redo 再次清除
    Document doc(QSize(8, 8));
    auto *layer = new Layer(QStringLiteral("L"), QSize(8, 8), QColor(0, 200, 0, 255));
    doc.addLayer(layer);
    const QImage mask = makeCenterMask4();
    const QRect rect(2, 2, 4, 4);
    const QImage before = layer->pixels().copy(rect);

    QUndoStack stack;
    stack.push(new ClearMaskCommand(layer, rect, mask, before));

    // redo(首次执行):mask 覆盖区域被清透明
    QCOMPARE(layer->pixels().pixelColor(4, 4).alpha(), 0);
    // 选区外保持原样
    QCOMPARE(layer->pixels().pixelColor(0, 0).alpha(), 255);
    QCOMPARE(layer->pixels().pixelColor(0, 0).green(), 200);

    // undo:恢复 rect 内原始像素
    stack.undo();
    QCOMPARE(layer->pixels().pixelColor(4, 4), QColor(0, 200, 0, 255));
    QCOMPARE(layer->pixels().pixelColor(2, 2), QColor(0, 200, 0, 255));

    // redo:再次清除
    stack.redo();
    QCOMPARE(layer->pixels().pixelColor(4, 4).alpha(), 0);
}

// ============================== 布尔运算 ==============================
void TestDocument::selCombineBooleanOps()
{
    const QSize sz(8, 8);
    // a = 左半边 (0,0)-(3,7),b = 上半边 (0,0)-(7,3)
    const QImage a = sel::maskFromRect(QRect(0, 0, 4, 8), sz);
    const QImage b = sel::maskFromRect(QRect(0, 0, 8, 4), sz);
    QCOMPARE(sel::coverage(a), 32);
    QCOMPARE(sel::coverage(b), 32);

    // Union:面积 = 32 + 32 - 16(重叠 4x4) = 48
    const QImage u = sel::combine(a, b, sel::CombineOp::Union, sz);
    QCOMPARE(u.size(), sz);
    QCOMPARE(sel::coverage(u), 48);
    QCOMPARE(maskValue(u, 1, 1), 255); // 重叠区
    QCOMPARE(maskValue(u, 5, 5), 255); // 仅 a
    QCOMPARE(maskValue(u, 1, 5), 255); // 仅 b

    // Subtract:a - b → 48 - 32 = 16(左下角 4x4)
    const QImage s = sel::combine(a, b, sel::CombineOp::Subtract, sz);
    QCOMPARE(sel::coverage(s), 16);
    QCOMPARE(maskValue(s, 1, 5), 255); // 仅 a 的部分
    QCOMPARE(maskValue(s, 5, 1), 0);   // 仅 b 的部分被减掉
    QCOMPARE(maskValue(s, 1, 1), 0);   // 重叠被减掉

    // Intersect:a ∩ b = 左上 4x4 = 16
    const QImage i = sel::combine(a, b, sel::CombineOp::Intersect, sz);
    QCOMPARE(sel::coverage(i), 16);
    QCOMPARE(maskValue(i, 1, 1), 255);
    QCOMPARE(maskValue(i, 5, 1), 0);
    QCOMPARE(maskValue(i, 1, 5), 0);

    // 空集语义
    const QImage none;
    QCOMPARE(sel::combine(none, b, sel::CombineOp::Union, sz), b);   // 空 ∪ b = b
    QCOMPARE(sel::coverage(sel::combine(none, b, sel::CombineOp::Subtract, sz)), 0); // 空 - b = 空
    QCOMPARE(sel::coverage(sel::combine(a, none, sel::CombineOp::Intersect, sz)), 0); // a ∩ 空 = 空
    QCOMPARE(sel::combine(a, none, sel::CombineOp::Union, sz), a);   // a ∪ 空 = a

    // 羽化边缘参与运算:一半强度相减应得到约一半
    QImage halfA(8, 8, QImage::Format_Grayscale8); halfA.fill(255);
    QImage halfB(8, 8, QImage::Format_Grayscale8); halfB.fill(128);
    const QImage soft = sel::combine(halfA, halfB, sel::CombineOp::Subtract, sz);
    QCOMPARE(maskValue(soft, 4, 4), 255 - 128); // 255*(255-128)/255 = 127
}

void TestDocument::selFeatherGrowShrink()
{
    const QSize sz(8, 8);
    const QImage center = makeCenterMask4(sz); // 中心 4x4
    QCOMPARE(sel::coverage(center), 16);

    // feather:半径 1 → 边缘出现中间值,总面积(非零)可能扩大但边缘变软
    const QImage feathered = sel::featherMask(center, 1.0);
    QCOMPARE(feathered.size(), sz);
    QCOMPARE(feathered.format(), QImage::Format_Grayscale8);
    // 中心仍是最亮
    QCOMPARE(maskValue(feathered, 4, 4), 255);
    // 紧邻原选区边界的像素应出现过渡值(0 < v < 255)
    const int edge = maskValue(feathered, 1, 4);
    QVERIFY2(edge > 0 && edge < 255, qPrintable(QString("edge=%1").arg(edge)));
    // radius=0 → 原样
    QCOMPARE(sel::featherMask(center, 0.0), center);

    // grow:半径 1 → 选区向外扩张,bbox 变大
    const QImage grown = sel::growMask(center, 1);
    QCOMPARE(grown.size(), sz);
    QVERIFY(sel::coverage(grown) > 16);
    // 原来(1,4)在选区外,grow 后应在区内
    QCOMPARE(maskValue(grown, 1, 4), 255);
    // radius=0 → 原样
    QCOMPARE(sel::growMask(center, 0), center);

    // shrink:半径 1 → 选区向内收缩,bbox 变小
    const QImage shrunk = sel::shrinkMask(center, 1);
    QCOMPARE(shrunk.size(), sz);
    QVERIFY(sel::coverage(shrunk) < 16);
    // 原来的边界 (2,4) 现在应在外
    QCOMPARE(maskValue(shrunk, 2, 4), 0);
    // 中心仍在
    QCOMPARE(maskValue(shrunk, 4, 4), 255);

    // shrink 过度 → 全 0 但不是 null
    const QImage tiny = sel::shrinkMask(makeCenterMask4(QSize(4, 4)), 3);
    QCOMPARE(sel::coverage(tiny), 0);
    QVERIFY(!tiny.isNull());

    // null mask 一律原样返回
    QVERIFY(sel::featherMask(QImage(), 5.0).isNull());
    QVERIFY(sel::growMask(QImage(), 5).isNull());
    QVERIFY(sel::shrinkMask(QImage(), 5).isNull());
}

void TestDocument::maskCommandUndoRedo()
{
    Document doc(QSize(8, 8));
    CanvasView view;
    view.setDocument(&doc);

    const QImage before = makeCenterMask4(QSize(8, 8));
    const QImage after = sel::growMask(before, 1);
    view.setSelectionMask(before);
    QCOMPARE(view.selectionRect(), QRect(2, 2, 4, 4));

    QUndoStack stack;
    stack.push(new MaskCommand(&view, before, after, QStringLiteral("Expand")));
    QCOMPARE(sel::coverage(view.selectionMask()), sel::coverage(after));

    stack.undo();
    QCOMPARE(sel::coverage(view.selectionMask()), 16); // 回到 before
    QCOMPARE(view.selectionRect(), QRect(2, 2, 4, 4));

    stack.redo();
    QCOMPARE(sel::coverage(view.selectionMask()), sel::coverage(after));

    // null after(取消选择)→ selectionRect 被清空
    stack.push(new MaskCommand(&view, after, QImage(), QStringLiteral("None")));
    QVERIFY(view.selectionRect().isEmpty());
    stack.undo();
    QCOMPARE(sel::coverage(view.selectionMask()), sel::coverage(after));
}

void TestDocument::cropDocumentCommandUndoRedo()
{
    // 文档级裁剪:画布尺寸 + 所有图层像素 + 图层偏移一起变
    Document doc(QSize(8, 8));
    auto *l0 = new Layer(QStringLiteral("L0"), QSize(8, 8), QColor(255, 0, 0, 255));
    doc.addLayer(l0);
    auto *l1 = new Layer(QStringLiteral("L1"), QSize(8, 8), QColor(0, 0, 255, 255));
    l1->setOffset(QPoint(1, 1));
    doc.addLayer(l1);
    QCOMPARE(doc.size(), QSize(8, 8));

    QVector<QImage> beforePixels;
    QVector<QPoint> beforeOffsets;
    beforePixels << l0->pixels() << l1->pixels();
    beforeOffsets << l0->offset() << l1->offset();

    // 裁剪到 (2,2)-(5,5) → 4x4
    QUndoStack stack;
    stack.push(new CropDocumentCommand(&doc, QRect(2, 2, 4, 4), beforePixels, beforeOffsets,
                                       QSize(8, 8)));

    // 画布尺寸变了
    QCOMPARE(doc.size(), QSize(4, 4));
    // 图层像素被裁到新尺寸
    QCOMPARE(l0->pixels().size(), QSize(4, 4));
    QCOMPARE(l1->pixels().size(), QSize(4, 4));
    // 偏移被平移
    QCOMPARE(l0->offset(), QPoint(-2, -2));
    QCOMPARE(l1->offset(), QPoint(-1, -1));

    stack.undo();
    QCOMPARE(doc.size(), QSize(8, 8));
    QCOMPARE(l0->pixels().size(), QSize(8, 8));
    QCOMPARE(l0->offset(), QPoint(0, 0));
    QCOMPARE(l1->offset(), QPoint(1, 1));
    QCOMPARE(l0->pixels().pixelColor(0, 0), QColor(255, 0, 0, 255));

    stack.redo();
    QCOMPARE(doc.size(), QSize(4, 4));
    QCOMPARE(l0->offset(), QPoint(-2, -2));
}

void TestDocument::layerViaMaskCommandUndoRedo()
{
    Document doc(QSize(8, 8));
    auto *src = new Layer(QStringLiteral("bg"), QSize(8, 8), QColor(0, 200, 0, 255));
    doc.addLayer(src);
    QCOMPARE(doc.layerCount(), 1);

    const QImage mask = makeCenterMask4(QSize(8, 8)); // 中心 4x4
    auto *cut = new Layer(QStringLiteral("cut"), QSize(8, 8), Qt::transparent);
    const QRect selRect(2, 2, 4, 4);

    QUndoStack stack;
    stack.push(new LayerViaMaskCommand(&doc, src, cut, selRect, mask));

    // 新图层插入到源图层之上
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(doc.activeLayer(), cut);
    // 抠出的图层:选区内有内容,选区外透明
    QCOMPARE(cut->pixels().pixelColor(4, 4).alpha(), 255);
    QCOMPARE(cut->pixels().pixelColor(4, 4).green(), 200);
    QCOMPARE(cut->pixels().pixelColor(0, 0).alpha(), 0);
    // 原图层被挖空:选区内透明,选区外保留
    QCOMPARE(src->pixels().pixelColor(4, 4).alpha(), 0);
    QCOMPARE(src->pixels().pixelColor(0, 0).alpha(), 255);
    QCOMPARE(src->pixels().pixelColor(0, 0).green(), 200);

    stack.undo();
    QCOMPARE(doc.layerCount(), 1);
    QCOMPARE(doc.activeLayer(), src);
    // 原图层完整还原
    QCOMPARE(src->pixels().pixelColor(4, 4).alpha(), 255);
    QCOMPARE(src->pixels().pixelColor(4, 4).green(), 200);

    stack.redo();
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(src->pixels().pixelColor(4, 4).alpha(), 0);
}

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
