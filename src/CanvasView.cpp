#include "CanvasView.h"

#include "Document.h"
#include "Layer.h"
#include "PainterRenderer.h"
#include "Renderer.h"
#include "UndoCommands.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>

CanvasView::CanvasView(QWidget *parent)
    : QWidget(parent)
    , m_renderer(std::make_unique<PainterRenderer>())
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

CanvasView::~CanvasView() = default;

void CanvasView::setDocument(Document *document)
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document)
        connect(m_document, &Document::contentChanged, this, [this] { update(); });
    zoomToFit();
    update();
}

QSize CanvasView::documentSize() const
{
    return m_document ? m_document->size() : QSize(1200, 800);
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

void CanvasView::applyZoom(double factor, const QPointF &anchor)
{
    const QPointF anchorCanvas = viewToCanvas(anchor);
    setZoom(m_zoom * factor);
    m_pan = anchor - anchorCanvas * m_zoom; // 保持锚点下的画布点不动
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

    if (!m_document) {
        p.setPen(palette().placeholderText().color());
        p.drawText(rect(), Qt::AlignCenter, tr("File › New 新建文档"));
        return;
    }

    const QSize doc = documentSize();
    const QRectF docRect(canvasToView(QPointF(0, 0)), QSizeF(doc) * m_zoom);

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

    // 渲染文档(经 Renderer 抽象,后续可换 QRhi)
    const QImage scene = m_renderer->render(*m_document);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
    p.drawImage(docRect, scene);

    // 文档边框
    p.setPen(palette().shadow().color());
    p.drawRect(docRect.adjusted(0, 0, -1, -1));

    if (m_zoom < 0.4) {
        p.setPen(palette().placeholderText().color());
        p.drawText(docRect, Qt::AlignCenter, tr("Compositor Qt · 原型画布"));
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

    if (event->button() == Qt::LeftButton && m_document && m_document->activeLayer()) {
        m_brushing = true;
        m_lastBrushPos = viewToCanvas(event->pos()).toPoint();
        m_dirtyRect = QRect();
        // 整层快照,释放时裁出脏矩形生成撤销命令
        m_beforeFull = QImage(m_document->activeLayer()->pixels());
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }

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

    if (m_brushing) {
        paintBrush(viewToCanvas(event->pos()).toPoint());
        event->accept();
        return;
    }

    const QPointF c = viewToCanvas(event->pos());
    emit cursorInfoChanged(QStringLiteral("X %1, Y %2").arg(qRound(c.x())).arg(qRound(c.y())));
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_brushing) {
        endBrush();
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
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

void CanvasView::paintBrush(const QPoint &pos)
{
    Layer *layer = m_document->activeLayer();
    if (!layer)
        return;
    QPainter p(&layer->pixels());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(m_brushColor, m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(m_lastBrushPos, pos);
    p.end();

    m_dirtyRect |= QRect(m_lastBrushPos, pos).normalized()
                       .adjusted(-m_brushSize, -m_brushSize, m_brushSize, m_brushSize);
    m_dirtyRect &= layer->pixels().rect();
    m_lastBrushPos = pos;
    emit layer->changed(); // -> document contentChanged -> update()
}

void CanvasView::endBrush()
{
    Layer *layer = m_document ? m_document->activeLayer() : nullptr;
    if (layer && !m_dirtyRect.isEmpty() && m_undoStack) {
        const QImage before = m_beforeFull.copy(m_dirtyRect);
        const QImage after = layer->pixels().copy(m_dirtyRect);
        m_undoStack->push(new PaintCommand(m_document, layer, m_dirtyRect, before, after));
    }
    m_beforeFull = QImage();
}
