#include "UndoCommands.h"

#include "CanvasView.h"
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
// AdjustLutCommand
// ---------------------------------------------------------------------------
AdjustLutCommand::AdjustLutCommand(Document *document, const adj::AdjustLut &before,
                                   const adj::AdjustLut &after, const QString &name,
                                   QUndoCommand *parent)
    : QUndoCommand(name, parent)
    , m_document(document)
    , m_before(before)
    , m_after(after)
{
}

void AdjustLutCommand::undo()
{
    m_document->setRenderLut(m_before);
}

void AdjustLutCommand::redo()
{
    m_document->setRenderLut(m_after);
}

// ---------------------------------------------------------------------------
// GaussianBlurCommand
// ---------------------------------------------------------------------------
GaussianBlurCommand::GaussianBlurCommand(Document *document, double before, double after,
                                         QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Gaussian Blur"), parent)
    , m_document(document)
    , m_before(before)
    , m_after(after)
{
}

void GaussianBlurCommand::undo()
{
    m_document->setGaussianBlurRadius(m_before);
}

void GaussianBlurCommand::redo()
{
    m_document->setGaussianBlurRadius(m_after);
}

// ---------------------------------------------------------------------------
// VignetteCommand
// ---------------------------------------------------------------------------
VignetteCommand::VignetteCommand(Document *document,
                                 double bAmount, double bMidpoint, double bRoundness, double bFeather,
                                 double aAmount, double aMidpoint, double aRoundness, double aFeather,
                                 QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Vignette"), parent)
    , m_document(document)
    , m_bAmount(bAmount), m_bMid(bMidpoint), m_bRound(bRoundness), m_bFeather(bFeather)
    , m_aAmount(aAmount), m_aMid(aMidpoint), m_aRound(aRoundness), m_aFeather(aFeather)
{
}

void VignetteCommand::undo()
{
    m_document->setVignette(m_bAmount, m_bMid, m_bRound, m_bFeather);
}

void VignetteCommand::redo()
{
    m_document->setVignette(m_aAmount, m_aMid, m_aRound, m_aFeather);
}

// ---------------------------------------------------------------------------
// MotionBlurCommand
// ---------------------------------------------------------------------------
MotionBlurCommand::MotionBlurCommand(Document *document,
                                      double bDistance, double bAngle,
                                      double aDistance, double aAngle,
                                      QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Motion Blur"), parent)
    , m_document(document)
    , m_bDistance(bDistance), m_bAngle(bAngle)
    , m_aDistance(aDistance), m_aAngle(aAngle)
{
}

void MotionBlurCommand::undo()
{
    m_document->setMotionBlur(m_bDistance, m_bAngle);
}

void MotionBlurCommand::redo()
{
    m_document->setMotionBlur(m_aDistance, m_aAngle);
}

// ---------------------------------------------------------------------------
// AddNoiseCommand
// ---------------------------------------------------------------------------
AddNoiseCommand::AddNoiseCommand(Document *document,
                                 double bAmount, bool bGaussian, bool bMono, quint32 bSeed,
                                 double aAmount, bool aGaussian, bool aMono, quint32 aSeed,
                                 QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Add Noise"), parent)
    , m_document(document)
    , m_bAmount(bAmount), m_aAmount(aAmount)
    , m_bGauss(bGaussian), m_aGauss(aGaussian)
    , m_bMono(bMono), m_aMono(aMono)
    , m_bSeed(bSeed), m_aSeed(aSeed)
{
}

void AddNoiseCommand::undo()
{
    m_document->setAddNoise(m_bAmount, m_bGauss, m_bMono, m_bSeed);
}

void AddNoiseCommand::redo()
{
    m_document->setAddNoise(m_aAmount, m_aGauss, m_aMono, m_aSeed);
}

// ---------------------------------------------------------------------------
// HsvCommand
// ---------------------------------------------------------------------------
HsvCommand::HsvCommand(Document *document, const adj::HsvParams &before, const adj::HsvParams &after,
                       QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Hue/Saturation"), parent)
    , m_document(document)
    , m_before(before)
    , m_after(after)
{
}

void HsvCommand::undo() { m_document->setHsv(m_before); }
void HsvCommand::redo() { m_document->setHsv(m_after); }

// ---------------------------------------------------------------------------
// BlackWhiteCommand
// ---------------------------------------------------------------------------
BlackWhiteCommand::BlackWhiteCommand(Document *document, const adj::BwParams &before,
                                      const adj::BwParams &after, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Black & White"), parent)
    , m_document(document)
    , m_before(before)
    , m_after(after)
{
}

void BlackWhiteCommand::undo() { m_document->setBlackWhite(m_before); }
void BlackWhiteCommand::redo() { m_document->setBlackWhite(m_after); }

// ---------------------------------------------------------------------------
// ColorBalanceCommand
// ---------------------------------------------------------------------------
ColorBalanceCommand::ColorBalanceCommand(Document *document, const adj::CbParams &before,
                                           const adj::CbParams &after, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Color Balance"), parent)
    , m_document(document)
    , m_before(before)
    , m_after(after)
{
}

void ColorBalanceCommand::undo() { m_document->setColorBalance(m_before); }
void ColorBalanceCommand::redo() { m_document->setColorBalance(m_after); }

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

// ---------------------------------------------------------------------------
// SelectionCommand
// ---------------------------------------------------------------------------
SelectionCommand::SelectionCommand(CanvasView *canvas, const QRect &before, const QRect &after,
                                   const QImage &beforeMask, const QImage &afterMask,
                                   QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Change Selection"), parent)
    , m_canvas(canvas)
    , m_before(before)
    , m_after(after)
    , m_beforeMask(beforeMask)
    , m_afterMask(afterMask)
{
}

void SelectionCommand::undo()
{
    if (!m_canvas)
        return;
    if (!m_beforeMask.isNull())
        m_canvas->setSelectionMask(m_beforeMask);
    else
        m_canvas->setSelectionRect(m_before);
}

void SelectionCommand::redo()
{
    if (!m_canvas)
        return;
    if (!m_afterMask.isNull())
        m_canvas->setSelectionMask(m_afterMask);
    else
        m_canvas->setSelectionRect(m_after);
}

// ---------------------------------------------------------------------------
// MagicWandCommand
// ---------------------------------------------------------------------------
MagicWandCommand::MagicWandCommand(QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Magic Wand"), parent)
{
}
