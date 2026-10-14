#include "UndoCommands.h"

#include "Document.h"
#include "Layer.h"

#include <QPainter>

// ---------------------------------------------------------------------------
// AddLayerCommand
// ---------------------------------------------------------------------------
AddLayerCommand::AddLayerCommand(Document *document, Layer *layer, int index, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Add Layer"), parent)
    , m_document(document)
    , m_layer(layer)
    , m_index(index)
{
    // 命令先持有图层;redo 后交给文档
    m_layer->setParent(this);
}

AddLayerCommand::~AddLayerCommand()
{
    // 仅当图层不在文档中(处于 undo 状态)时由命令负责销毁
    if (m_layer && m_layer->parent() == this)
        delete m_layer;
}

void AddLayerCommand::undo()
{
    m_document->removeLayer(m_layer);
    m_layer->setParent(this);
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
    if (m_layer && m_layer->parent() == this)
        delete m_layer;
}

void RemoveLayerCommand::undo()
{
    m_document->addLayer(m_layer, m_index);
    m_document->setActiveLayer(m_layer);
}

void RemoveLayerCommand::redo()
{
    m_document->removeLayer(m_layer);
    m_layer->setParent(this);
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

static void blitRegion(Layer *layer, const QRect &rect, const QImage &image)
{
    QPainter p(&layer->pixels());
    p.setCompositionMode(QPainter::CompositionMode_Source); // 覆盖含 alpha
    p.drawImage(rect.topLeft(), image);
    p.end();
    emit layer->changed();
}

void PaintCommand::undo()
{
    blitRegion(m_layer, m_rect, m_before);
}

void PaintCommand::redo()
{
    blitRegion(m_layer, m_rect, m_after);
}
