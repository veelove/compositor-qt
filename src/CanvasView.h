#pragma once

#include <QPointF>
#include <QSize>
#include <QWidget>

class CanvasView : public QWidget
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget *parent = nullptr);

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
    // 骨架阶段用一个固定占位文档尺寸,后续接入真正的文档模型
    QSize documentSize() const { return QSize(1200, 800); }

    QPointF canvasToView(const QPointF &p) const { return p * m_zoom + m_pan; }
    QPointF viewToCanvas(const QPointF &p) const { return (p - m_pan) / m_zoom; }
    void applyZoom(double factor, const QPointF &anchorViewPos);
    void setZoom(double zoom);

    double m_zoom = 1.0;
    QPointF m_pan;              // 视图偏移(设备像素)
    bool m_panning = false;
    bool m_spacePressed = false;
    QPoint m_lastPanPos;
};
