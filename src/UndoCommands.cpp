#include "UndoCommands.h"

#include "Document.h"
#include "Layer.h"

#include <QPainter>

namespace {

// 命令在两种状态下都要安全地"释放"自己持有的图层:
//   1) 图层已被文档接管(redo 后)→ 文档通过父对象负责析构
//   2) 图层被命令临时持有(undo 后)→ 命令的析构函数 delete 掉
// 用一个辅助函数统一处理,避免 QObject 父子关系(命令是 QUndoCommand,不是 QObject)
template <typename LayerT>
void releaseOrphaned(LayerT *layer, Document *doc)
{
    if (layer && doc->indexOf(layer) < 0)
        delete layer;
}

// 把 image 以覆盖模式(含 alpha)画到 layer 的 rect 区域,并通知重绘
void blitRegion(Layer *layer, const QRect &rect, const QImage &image)
{
    QPainter p(&layer->pixels());
    p.setCompositionMode(QPainter::CompositionMode_Source); // 覆盖含 alpha
    p.drawImage(rect.topLeft(), image);
    p.end();
    emit layer->changed();
}

} // namespace

// ---------------------------------------------------------------------------
// FilterCommand
// ---------------------------------------------------------------------------
FilterCommand::FilterCommand(Layer *layer, const QImage &before, const QImage &after,
                             const QString &name, QUndoCommand *parent)
    : QUndoCommand(name, parent)
    , m_layer(layer)
    , m_before(before)
    , m_after(after)
{
}

void FilterCommand::undo()
{
    m_layer->pixels() = m_before;
    emit m_layer->changed();
}

void FilterCommand::redo()
{
    m_layer->pixels() = m_after;
    emit m_layer->changed();
}

// ---------------------------------------------------------------------------
// MoveLayerCommand
// ---------------------------------------------------------------------------
MoveLayerCommand::MoveLayerCommand(Layer *layer, const QPoint &before, const QPoint &after,
                                   QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Move Layer"), parent)
    , m_layer(layer)
    , m_before(before)
    , m_after(after)
{
}

void MoveLayerCommand::undo()
{
    m_layer->setOffset(m_before);
}

void MoveLayerCommand::redo()
{
    m_layer->setOffset(m_after);
}

// ---------------------------------------------------------------------------
// ClearRectCommand
// ---------------------------------------------------------------------------
ClearRectCommand::ClearRectCommand(Layer *layer, const QRect &rect, const QImage &before,
                                   QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Clear Selection"), parent)
    , m_layer(layer)
    , m_rect(rect)
    , m_before(before)
{
}

void ClearRectCommand::undo()
{
    blitRegion(m_layer, m_rect, m_before);
}

void ClearRectCommand::redo()
{
    QPainter p(&m_layer->pixels());
    p.setCompositionMode(QPainter::CompositionMode_Clear); // 区域置透明
    p.fillRect(m_rect, Qt::transparent);
    p.end();
    emit m_layer->changed();
}

// ---------------------------------------------------------------------------
// AddLayerCommand
// ---------------------------------------------------------------------------
AddLayerCommand::AddLayerCommand(Document *document, Layer *layer, int index, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Add Layer"), parent)
    , m_document(document)
    , m_layer(layer)
    , m_index(index)
{
}

AddLayerCommand::~AddLayerCommand()
{
    if (m_layer)
        releaseOrphaned(m_layer, m_document);
}

void AddLayerCommand::undo()
{
    // 让文档"放过"图层所有权:取出后 detach 父子
    m_document->removeLayer(m_layer);
    m_layer->setParent(nullptr);
}

void AddLayerCommand::redo()
{
    m_document->addLayer(m_layer, m_index);
    m_document->setActiveLayer(m_layer);
}

// ---------------------------------------------------------------------------
// RemoveLayerCommand
// ---------------------------------------------------------------------------
RemoveLayerCommand::RemoveLayerCommand(Document *document, int index, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Remove Layer"), parent)
    , m_document(document)
    , m_index(index)
{
    m_layer = m_document->layerAt(index); // 创建时图层仍在文档中
}

RemoveLayerCommand::~RemoveLayerCommand()
{
    if (m_layer)
        releaseOrphaned(m_layer, m_document);
}

void RemoveLayerCommand::undo()
{
    m_document->addLayer(m_layer, m_index);
    m_document->setActiveLayer(m_layer);
}

void RemoveLayerCommand::redo()
{
    m_document->removeLayer(m_layer);
    m_layer->setParent(nullptr);
}

// ---------------------------------------------------------------------------
// PaintCommand
// ---------------------------------------------------------------------------
PaintCommand::PaintCommand(Document *document, Layer *layer, const QRect &dirtyRect,
                           const QImage &before, const QImage &after, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Paint"), parent)
    , m_document(document)
    , m_layer(layer)
    , m_rect(dirtyRect)
    , m_before(before)
    , m_after(after)
{
}

void PaintCommand::undo()
{
    blitRegion(m_layer, m_rect, m_before);
}

void PaintCommand::redo()
{
    blitRegion(m_layer, m_rect, m_after);
}
