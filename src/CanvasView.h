#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QWidget>

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
    void magicWandTriggered(const QPoint &canvasPos); // 智能选择(单点)

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
    void beginSelect(const QPoint &canvasPos);
    void dragSelect(const QPoint &canvasPos);
    void endSelect();
    void clearSelection();
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
};
