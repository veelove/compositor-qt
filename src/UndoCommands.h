#pragma once

#include <QImage>
#include <QRect>
#include <QUndoCommand>

class Document;
class Layer;

// 添加图层:redo 加入文档,undo 移除(图层所有权在命令与文档间转移)。
class AddLayerCommand : public QUndoCommand
{
public:
    AddLayerCommand(Document *document, Layer *layer, int index = -1, QUndoCommand *parent = nullptr);
    ~AddLayerCommand() override;
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    Layer *m_layer;
    int m_index;
};

// 移除图层:与 AddLayerCommand 互逆。
class RemoveLayerCommand : public QUndoCommand
{
public:
    RemoveLayerCommand(Document *document, int index, QUndoCommand *parent = nullptr);
    ~RemoveLayerCommand() override;
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    Layer *m_layer;
    int m_index;
};

// 画笔:记录脏矩形内像素的 before/after 快照。
class PaintCommand : public QUndoCommand
{
public:
    PaintCommand(Document *document, Layer *layer, const QRect &dirtyRect,
                 const QImage &before, const QImage &after, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    Layer *m_layer;
    QRect m_rect;
    QImage m_before;
    QImage m_after;
};
