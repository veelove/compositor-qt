#include <QtTest>

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

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
