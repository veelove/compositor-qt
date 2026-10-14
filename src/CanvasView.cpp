#include "CanvasView.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

CanvasView::CanvasView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void CanvasView::setZoom(double zoom)
{
    zoom = std::clamp(zoom, 0.02, 32.0);
    if (qFuzzyCompare(m_zoom, zoom))
        return;
    m_zoom = zoom;
    emit zoomChanged(m_zoom);
    update();
}

void CanvasView::zoomIn()  { applyZoom(1.25, QPointF(rect().center())); }
void CanvasView::zoomOut() { applyZoom(0.8, QPointF(rect().center())); }

void CanvasView::applyZoom(double factor, const QPointF &anchorViewPos)
{
    const QPointF anchorCanvas = viewToCanvas(anchorViewPos);
    setZoom(m_zoom * factor);
    // 保持锚点下的画布坐标不动(以锚点为基准缩放)
    m_pan = anchorViewPos - anchorCanvas * m_zoom;
    update();
}

void CanvasView::zoomToActual()
{
    const QPointF anchorCanvas = viewToCanvas(QPointF(rect().center()));
    setZoom(1.0);
    m_pan = QPointF(rect().center()) - anchorCanvas * m_zoom;
    update();
}

void CanvasView::zoomToFit()
{
    const QSize doc = documentSize();
    const double fit = std::min(width() / double(doc.width()), height() / double(doc.height()));
    m_pan = QPointF((width() - doc.width() * fit) / 2.0, (height() - doc.height() * fit) / 2.0);
    setZoom(fit);
    update();
}

void CanvasView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());

    const QRectF docRect(canvasToView(QPointF(0, 0)), QSizeF(documentSize()) * m_zoom);

    // 棋盘格表示透明背景
    p.save();
    p.setClipRect(docRect);
    const int cell = std::max(4, int(8 * m_zoom));
    const QColor light(0xF2, 0xF2, 0xF2);
    const QColor dark(0xE0, 0xE0, 0xE0);
    const int x0 = int(docRect.left());
    const int y0 = int(docRect.top());
    for (int y = y0; y < int(docRect.bottom()); y += cell) {
        for (int x = x0; x < int(docRect.right()); x += cell) {
            const bool even = (((x - x0) / cell) + ((y - y0) / cell)) % 2 == 0;
            p.fillRect(x, y, cell, cell, even ? light : dark);
        }
    }
    p.restore();

    // 文档边框
    p.setPen(palette().shadow().color());
    p.drawRect(docRect.adjusted(0, 0, -1, -1));

    // 缩放过小时显示占位提示
    if (m_zoom < 0.4) {
        p.setPen(palette().placeholderText().color());
        p.drawText(docRect, Qt::AlignCenter, tr("Compositor Qt 骨架 · 空画布"));
    }
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    applyZoom(factor, event->position());
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton && m_spacePressed)
        || event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    // TODO: 后续在这里接工具(画笔/选框/移动等)
    QWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_pan += QPointF(event->pos() - m_lastPanPos);
        m_lastPanPos = event->pos();
        update();
        event->accept();
        return;
    }
    const QPointF c = viewToCanvas(event->pos());
    emit cursorInfoChanged(QStringLiteral("X %1, Y %2").arg(qRound(c.x())).arg(qRound(c.y())));
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_panning)
            unsetCursor();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
