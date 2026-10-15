#pragma once

#include "AdjustLut.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QUndoCommand>

class Document;
class Layer;

// 滤镜:对整个图层像素应用变换(整层 before/after 快照),支持 undo/redo。
class FilterCommand : public QUndoCommand
{
public:
    FilterCommand(Layer *layer, const QImage &before, const QImage &after,
                  const QString &name, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Layer *m_layer;
    QImage m_before;
    QImage m_after;
};

// 移动图层:记录位置偏移 before/after。
class MoveLayerCommand : public QUndoCommand
{
public:
    MoveLayerCommand(Layer *layer, const QPoint &before, const QPoint &after, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Layer *m_layer;
    QPoint m_before;
    QPoint m_after;
};

// 清除选区:把矩形区域像素置为透明,undo 恢复 before 快照。
class ClearRectCommand : public QUndoCommand
{
public:
    ClearRectCommand(Layer *layer, const QRect &rect, const QImage &before, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Layer *m_layer;
    QRect m_rect;
    QImage m_before;
};

// 调色 LUT:对 Document.renderLut 做 before/after 记录(非破坏性),
// undo/redo 切换 LUT,从而触发重绘。
class AdjustLutCommand : public QUndoCommand
{
public:
    AdjustLutCommand(Document *document, const adj::AdjustLut &before,
                     const adj::AdjustLut &after, const QString &name, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    adj::AdjustLut m_before;
    adj::AdjustLut m_after;
};

// 高斯模糊(非破坏性,记录 radius before/after)
class GaussianBlurCommand : public QUndoCommand
{
public:
    GaussianBlurCommand(Document *document, double before, double after, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    double m_before;
    double m_after;
};

// 暗角(非破坏性,4 维参数 before/after)
class VignetteCommand : public QUndoCommand
{
public:
    VignetteCommand(Document *document,
                    double bAmount, double bMidpoint, double bRoundness, double bFeather,
                    double aAmount, double aMidpoint, double aRoundness, double aFeather,
                    QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    double m_bAmount, m_bMid, m_bRound, m_bFeather;
    double m_aAmount, m_aMid, m_aRound, m_aFeather;
};

// 动感模糊(非破坏性,distance/angle before/after)
class MotionBlurCommand : public QUndoCommand
{
public:
    MotionBlurCommand(Document *document,
                      double bDistance, double bAngle,
                      double aDistance, double aAngle,
                      QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document *m_document;
    double m_bDistance, m_bAngle, m_aDistance, m_aAngle;
};

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
