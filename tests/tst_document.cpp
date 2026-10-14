#include <QtTest>

#include "Adjustments.h"
#include "Document.h"
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

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
