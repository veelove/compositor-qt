#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QWidget>

#include <Qt>

#include <memory>

class Document;
class Layer;
class QPainter;
class QRectF;
class QUndoStack;
class Renderer;

// 画布工具:与 Photoshop 基础工具对应(MVP)
enum class Tool
{
    Brush,      // 画笔
    Move,       // 移动图层
    Eyedropper, // 吸管取色
    RectSelect, // 矩形选区(Delete 清除)
    MagicWand   // 智能选择(基于视觉智能,产生 alpha 蒙版)
};

class CanvasView : public QWidget
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);
    ~CanvasView() override;

    void setDocument(Document *document);
    Document *document() const { return m_document; }
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }

    double zoom() const { return m_zoom; }
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    void setBrushColor(const QColor &color) { m_brushColor = color; }

    // 选区:可由 RectSelect 或 MagicWand(MaskLayer) 设置。
    // - selectionRect: 用于键盘 Delete 清除 + 屏幕 dashed 边框(基于 mask 的 boundingRect)。
    // - selectionMask: 全图 mask (Format_Grayscale8),用于按 mask 应用变换。
    // - setSelectionMask 同时会算出 boundingRect 写回 selectionRect。
    void setSelectionMask(const QImage &mask);
    QImage selectionMask() const { return m_selectionMask; }
    void setSelectionRect(const QRect &rect);
    QRect selectionRect() const { return m_selection; }
    void clearSelection();

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToActual();

signals:
    void zoomChanged(double zoom);
    void cursorInfoChanged(const QString &text);
    void toolChanged(Tool tool);
    void colorPicked(const QColor &color); // 吸管取色
    // 智能选择(单点)。modifiers 用 Qt::KeyboardModifiers 传递 Shift/Alt,
    // 由 MainWindow 结合"已有选区"决定 Union / Subtract / Intersect。
    void magicWandTriggered(const QPoint &canvasPos, Qt::KeyboardModifiers modifiers);
    // 矩形选区完成(鼠标松开)。同样携带修饰键用于布尔运算。
    void rectSelectFinished(const QRect &canvasRect, Qt::KeyboardModifiers modifiers);
    void selectionChanged(const QRect &rect); // 选区变化(magicWand 或 RectSelect 都触发)

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QSize documentSize() const;
    // 把画布坐标系的 selectionMask 转到指定图层的像素坐标系。
    // 返回 null 表示当前没有 mask(纯矩形选区),调用方应退回矩形行为。
    QImage selectionMaskForLayer(const Layer &layer, const QRect &pixelRect) const;
    QPointF canvasToView(const QPointF &p) const { return p * m_zoom + m_pan; }
    QPointF viewToCanvas(const QPointF &p) const { return (p - m_pan) / m_zoom; }
    void applyZoom(double factor, const QPointF &anchor);
    void setZoom(double zoom);
    void updateCursor();
    // 工具事件
    void paintBrush(const QPoint &canvasPos);
    void endBrush();
    void beginMove(const QPoint &canvasPos);
    void dragMove(const QPoint &canvasPos);
    void endMove();
    void pickColor(const QPoint &canvasPos);
    void beginSelect(const QPoint &canvasPos, Qt::KeyboardModifiers mods);
    void dragSelect(const QPoint &canvasPos);
    void endSelect();
    void drawSelectionOverlay(QPainter &p, const QRectF &docRect);

    Document *m_document = nullptr;
    QUndoStack *m_undoStack = nullptr;
    std::unique_ptr<Renderer> m_renderer;

    double m_zoom = 1.0;
    QPointF m_pan; // 视图偏移(设备像素)
    bool m_panning = false;
    bool m_spacePressed = false;
    QPoint m_lastPanPos;

    // 当前工具
    Tool m_tool = Tool::Brush;

    // 画笔
    bool m_brushing = false;
    QPoint m_lastBrushPos;
    QRect m_dirtyRect;
    QImage m_beforeFull; // 拖拽期间的活动层整层快照(用于生成撤销快照)
    QColor m_brushColor = QColor(30, 144, 255);
    int m_brushSize = 12;

    // 移动工具
    bool m_moving = false;
    QPoint m_moveStartCanvas;   // 按下时的画布坐标
    QPoint m_moveStartOffset;   // 按下时图层偏移

    // 矩形选区
    bool m_selecting = false;
    QPoint m_selectStart;       // 选区起点(画布坐标)
    QRect m_selection;          // 定稿后的选区(画布坐标)
    QImage m_selectionMask;     // magicWand 全图 mask(可能大于 boundingRect)
    Qt::KeyboardModifiers m_selectionModifiers = Qt::NoModifier; // 拖拽时的 Shift/Alt
};
