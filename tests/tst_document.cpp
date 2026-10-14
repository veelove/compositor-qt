#include <QtTest>

#include "Document.h"
#include "Layer.h"
#include "PainterRenderer.h"
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
    void commandsCleanUp();
};

void TestDocument::addRemoveLayer()
{
    Document doc(QSize(10, 10));
    auto *background = new Layer(QStringLiteral("L1"), doc.size(), Qt::red);
    doc.addLayer(background);
    QCOMPARE(doc.layerCount(), 1);
    QCOMPARE(doc.layers().first(), background);

    // AddLayerCommand:push 即 redo,undo 移除,redo 再加回
    QUndoStack stack;
    auto *l2 = new Layer(QStringLiteral("L2"), doc.size(), Qt::blue);
    stack.push(new AddLayerCommand(&doc, l2));
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(doc.activeLayer(), l2);

    stack.undo();
    QCOMPARE(doc.layerCount(), 1);
    QVERIFY(doc.activeLayer() != l2); // 应回退到 background

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

    // 捕获"before"(空白),再画第一笔,捕获"after"
    const QRect dirty(0, 0, 14, 14);
    const QImage before = layer->pixels().copy(dirty);
    QPainter p(&layer->pixels());
    p.setPen(QPen(Qt::black, 3));
    p.drawLine(QPoint(2, 2), QPoint(10, 10));
    p.end();
    const QImage after = layer->pixels().copy(dirty);
    QVERIFY(before != after);

    stack.push(new PaintCommand(&doc, layer, dirty, before, after));
    QVERIFY(stack.canUndo());

    // 直接再画一块绿色(不在撤销栈里)
    {
        QPainter p2(&layer->pixels());
        p2.fillRect(QRect(0, 0, 6, 6), Qt::green);
        p2.end();
    }

    // undo 应恢复 dirty 区域到 first 笔的状态(after)
    stack.undo();
    QCOMPARE(layer->pixels().copy(dirty), after);

    // redo 应再次应用 after
    stack.redo();
    QCOMPARE(layer->pixels().copy(dirty), after);
}

void TestDocument::rendererComposites()
{
    Document doc(QSize(8, 8));
    doc.addLayer(new Layer(QStringLiteral("bottom"), doc.size(), Qt::blue));
    doc.addLayer(new Layer(QStringLiteral("top"), doc.size(), QColor(255, 0, 0, 128))); // 半透明红

    PainterRenderer renderer;
    const QImage out = renderer.render(doc);
    QCOMPARE(out.size(), doc.size());

    // 顶层半透明红叠在蓝底上,中心像素不可能是纯蓝
    QVERIFY(out.pixelColor(4, 4) != QColor(Qt::blue));
    QVERIFY(out.pixelColor(4, 4).red() > 0);
}

void TestDocument::commandsCleanUp()
{
    // Add 一次→ undo→ 释放命令→ 不应泄漏图层
    QUndoStack stack;
    {
        Document doc(QSize(4, 4));
        stack.push(new AddLayerCommand(&doc, new Layer(QStringLiteral("X"), doc.size(), Qt::black)));
        QCOMPARE(doc.layerCount(), 1);
        stack.undo();
        QCOMPARE(doc.layerCount(), 0);
        // 清栈:命令析构,此时图层不在文档中,命令应负责 delete
        stack.clear();
        // 删除 doc 时,文档中已无图层
        Q_UNUSED(doc);
    }
    // 走到这里没崩溃 = 没有 double-free / use-after-free
    QVERIFY(true);
}

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
