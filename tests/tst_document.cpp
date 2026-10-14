#include <QtTest>

#include "Adjustments.h"
#include "Document.h"
#include "Geometry.h"
#include "Layer.h"
#include "PainterRenderer.h"
#include "PsdReader.h"
#include "UndoCommands.h"

#include <QPainter>
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

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
