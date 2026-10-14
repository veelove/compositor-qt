#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QWidget>

#include <memory>

class Document;
class Layer;
class QUndoStack;
class Renderer;

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

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToActual();

signals:
    void zoomChanged(double zoom);
    void cursorInfoChanged(const QString &text);

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
    void paintBrush(const QPoint &canvasPos);
    void endBrush();

    Document *m_document = nullptr;
    QUndoStack *m_undoStack = nullptr;
    std::unique_ptr<Renderer> m_renderer;

    double m_zoom = 1.0;
    QPointF m_pan; // 视图偏移(设备像素)
    bool m_panning = false;
    bool m_spacePressed = false;
    QPoint m_lastPanPos;

    // 原型画笔
    bool m_brushing = false;
    QPoint m_lastBrushPos;
    QRect m_dirtyRect;
    QImage m_beforeFull; // 拖拽期间的活动层整层快照(用于生成撤销快照)
    QColor m_brushColor = QColor(30, 144, 255);
    int m_brushSize = 12;
};
