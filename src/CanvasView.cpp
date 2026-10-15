#include "CanvasView.h"

#include "Document.h"
#include "Layer.h"
#include "PainterRenderer.h"
#include "Renderer.h"
#include "UndoCommands.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSizeF>
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

void CanvasView::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    // 切换工具时结束进行中的操作
    if (m_brushing)
        endBrush();
    if (m_moving)
        endMove();
    m_tool = tool;
    updateCursor();
    emit toolChanged(m_tool);
    update();
}

void CanvasView::updateCursor()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    switch (m_tool) {
    case Tool::Move:
        setCursor(Qt::SizeAllCursor);
        break;
    case Tool::Eyedropper:
    case Tool::Brush:
    case Tool::RectSelect:
    case Tool::MagicWand:
        setCursor(Qt::CrossCursor);
        break;
    }
}

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

    drawSelectionOverlay(p, docRect);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    applyZoom(factor, event->position());
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    // 平移(空格+左键 / 中键)优先级最高,任何工具下都可用
    if ((event->button() == Qt::LeftButton && m_spacePressed)
        || event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || !m_document || !m_document->activeLayer()) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint canvasPos = viewToCanvas(event->pos()).toPoint();
    switch (m_tool) {
    case Tool::Brush:
        m_brushing = true;
        m_lastBrushPos = canvasPos;
        m_dirtyRect = QRect();
        // 整层快照,释放时裁出脏矩形生成撤销命令
        m_beforeFull = QImage(m_document->activeLayer()->pixels());
        break;
    case Tool::Move:
        beginMove(canvasPos);
        break;
    case Tool::Eyedropper:
        pickColor(canvasPos);
        break;
    case Tool::RectSelect:
        beginSelect(canvasPos);
        break;
    case Tool::MagicWand:
        emit magicWandTriggered(canvasPos);
        break;
    }
    event->accept();
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

    const QPoint canvasPos = viewToCanvas(event->pos()).toPoint();
    switch (m_tool) {
    case Tool::Brush:
        if (m_brushing) {
            paintBrush(canvasPos);
            event->accept();
            return;
        }
        break;
    case Tool::Move:
        if (m_moving) {
            dragMove(canvasPos);
            event->accept();
            return;
        }
        break;
    case Tool::RectSelect:
        if (m_selecting) {
            dragSelect(canvasPos);
            event->accept();
            return;
        }
        break;
    case Tool::Eyedropper:
        break;
    }

    const QPointF c = viewToCanvas(event->pos());
    emit cursorInfoChanged(QStringLiteral("X %1, Y %2").arg(qRound(c.x())).arg(qRound(c.y())));
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_brushing) {
            endBrush();
            updateCursor();
            event->accept();
            return;
        }
        if (m_moving) {
            endMove();
            updateCursor();
            event->accept();
            return;
        }
        if (m_selecting) {
            endSelect();
            updateCursor();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        updateCursor();
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
    if (event->key() == Qt::Key_Delete && !m_selection.isEmpty() && m_document) {
        clearSelection();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (!m_selection.isEmpty()) {
            m_selection = QRect();
            update();
        }
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

// ------------------------------- Move 工具 -------------------------------
void CanvasView::beginMove(const QPoint &canvasPos)
{
    Layer *layer = m_document->activeLayer();
    if (!layer)
        return;
    m_moving = true;
    m_moveStartCanvas = canvasPos;
    m_moveStartOffset = layer->offset();
}

void CanvasView::dragMove(const QPoint &canvasPos)
{
    Layer *layer = m_document->activeLayer();
    if (!layer)
        return;
    layer->setOffset(m_moveStartOffset + (canvasPos - m_moveStartCanvas));
}

void CanvasView::endMove()
{
    m_moving = false;
    Layer *layer = m_document ? m_document->activeLayer() : nullptr;
    if (!layer)
        return;
    const QPoint offset = layer->offset();
    if (offset != m_moveStartOffset && m_undoStack)
        m_undoStack->push(new MoveLayerCommand(layer, m_moveStartOffset, offset));
}

// ---------------------------- Eyedropper 工具 ----------------------------
void CanvasView::pickColor(const QPoint &canvasPos)
{
    if (!m_document)
        return;
    const QRect docRect(QPoint(0, 0), m_document->size());
    if (!docRect.contains(canvasPos))
        return;
    const QImage composite = m_renderer->render(*m_document);
    const QColor color = composite.pixelColor(canvasPos);
    emit colorPicked(color);
    emit cursorInfoChanged(tr("取色 %1").arg(color.name()));
}

// ---------------------------- RectSelect 工具 ----------------------------
void CanvasView::beginSelect(const QPoint &canvasPos)
{
    m_selecting = true;
    m_selectStart = canvasPos;
    m_selection = QRect(canvasPos, canvasPos);
    update();
}

void CanvasView::dragSelect(const QPoint &canvasPos)
{
    QRect sel(m_selectStart, canvasPos);
    sel = sel.normalized().intersected(QRect(QPoint(0, 0), m_document->size()));
    m_selection = sel;
    update();
}

void CanvasView::endSelect()
{
    m_selecting = false;
    update();
}

void CanvasView::clearSelection()
{
    Layer *layer = m_document ? m_document->activeLayer() : nullptr;
    if (!layer || m_selection.isEmpty())
        return;
    // 画布选区 → 图层像素坐标(减去图层偏移),并裁剪到图层范围
    QRect pixelRect = m_selection.translated(-layer->offset());
    pixelRect = pixelRect.intersected(layer->pixels().rect());
    if (pixelRect.isEmpty()) {
        m_selection = QRect();
        update();
        return;
    }
    const QImage before = layer->pixels().copy(pixelRect);
    if (m_undoStack)
        m_undoStack->push(new ClearRectCommand(layer, pixelRect, before));
    m_selection = QRect();
    update();
}

void CanvasView::drawSelectionOverlay(QPainter &p, const QRectF &docRect)
{
    Q_UNUSED(docRect);
    if (m_selection.isEmpty())
        return;
    const QRectF viewRect(canvasToView(m_selection.topLeft()),
                          QSizeF(m_selection.size()) * m_zoom);
    p.setPen(QPen(Qt::white, 1, Qt::DashLine));
    p.drawRect(viewRect.adjusted(0, 0, -1, -1));
}
