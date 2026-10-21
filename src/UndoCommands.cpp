#include "UndoCommands.h"

#include "CanvasView.h"
#include "Document.h"
#include "Layer.h"
#include "Selection.h"

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
// ClearMaskCommand
// ---------------------------------------------------------------------------
ClearMaskCommand::ClearMaskCommand(Layer *layer, const QRect &rect, const QImage &mask,
                                   const QImage &before, QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Clear Selection (Mask)"), parent)
    , m_layer(layer)
    , m_rect(rect)
    , m_mask(mask)
    , m_before(before)
{
}

void ClearMaskCommand::undo()
{
    // m_before 是 m_rect 区域的快照,直接覆盖回去即可完整还原
    blitRegion(m_layer, m_rect, m_before);
}

void ClearMaskCommand::redo()
{
    QImage px = m_layer->pixels();
    const QImage cleared = sel::clearByMask(px, m_rect, m_mask);
    if (cleared.isNull()) {
        emit m_layer->changed();
        return;
    }
    blitRegion(m_layer, m_rect, cleared.copy(m_rect));
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
// MaskCommand
// ---------------------------------------------------------------------------
MaskCommand::MaskCommand(CanvasView *canvas, const QImage &before, const QImage &after,
                         const QString &name, QUndoCommand *parent)
    : QUndoCommand(name, parent)
    , m_canvas(canvas)
    , m_before(before)
    , m_after(after)
{
}

void MaskCommand::undo()
{
    if (m_canvas)
        m_canvas->setSelectionMask(m_before);
}

void MaskCommand::redo()
{
    if (m_canvas)
        m_canvas->setSelectionMask(m_after);
}

// ---------------------------------------------------------------------------
// CropDocumentCommand
// ---------------------------------------------------------------------------
CropDocumentCommand::CropDocumentCommand(Document *document, const QRect &newCanvasRect,
                                         const QVector<QImage> &beforePixels,
                                         const QVector<QPoint> &beforeOffsets,
                                         const QSize &beforeCanvasSize,
                                         QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Crop Document"), parent)
    , m_document(document)
    , m_beforeRect(QRect(QPoint(0, 0), beforeCanvasSize))
    , m_beforePixels(beforePixels)
    , m_beforeOffsets(beforeOffsets)
    , m_beforeCanvasSize(beforeCanvasSize)
    , m_afterRect(newCanvasRect)
    , m_afterCanvasSize(newCanvasRect.size())
{
    // after 快照由 redo() 首次执行时填充(此时才知道裁剪后的实际像素)
}

void CropDocumentCommand::applyCrop(const QRect &newCanvasRect, const QVector<QImage> &pixels,
                                   const QVector<QPoint> &offsets, const QSize &canvasSize)
{
    if (!m_document)
        return;
    // 画布尺寸先到位,再逐层覆盖像素(避免中间态触发不一致的 contentChanged)
    m_document->setSize(canvasSize);
    const int n = qMin(pixels.size(), m_document->layerCount());
    for (int i = 0; i < n; ++i) {
        Layer *layer = m_document->layerAt(i);
        if (!layer)
            continue;
        if (i < offsets.size())
            layer->setOffset(offsets.at(i));
        layer->pixels() = pixels.at(i);
        emit layer->changed();
    }
    Q_UNUSED(newCanvasRect);
}

void CropDocumentCommand::redo()
{
    // 首次 redo:执行裁剪并记录 after 快照(供后续 undo/redo 复用)
    if (m_afterPixels.isEmpty()) {
        QVector<QImage> afterPixels;
        QVector<QPoint> afterOffsets;
        afterPixels.reserve(m_document->layerCount());
        for (int i = 0; i < m_document->layerCount(); ++i) {
            Layer *layer = m_document->layerAt(i);
            if (!layer)
                continue;
            // 把图层像素从画布坐标系裁到新画布大小
            QImage cropped(m_afterCanvasSize, QImage::Format_ARGB32);
            cropped.fill(Qt::transparent);
            {
                QPainter p(&cropped);
                p.drawImage(-m_afterRect.topLeft(), layer->pixels());
            }
            afterPixels.append(cropped);
            afterOffsets.append(layer->offset() - m_afterRect.topLeft());
        }
        m_afterPixels = afterPixels;
        m_afterOffsets = afterOffsets;
    }
    applyCrop(m_afterRect, m_afterPixels, m_afterOffsets, m_afterCanvasSize);
}

void CropDocumentCommand::undo()
{
    applyCrop(m_beforeRect, m_beforePixels, m_beforeOffsets, m_beforeCanvasSize);
}

// ---------------------------------------------------------------------------
// LayerViaMaskCommand
// ---------------------------------------------------------------------------
LayerViaMaskCommand::LayerViaMaskCommand(Document *document, Layer *sourceLayer, Layer *cutLayer,
                                         const QRect &selRect, const QImage &mask,
                                         QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Layer via Mask"), parent)
    , m_document(document)
    , m_sourceLayer(sourceLayer)
    , m_cutLayer(cutLayer)
    , m_selRect(selRect)
    , m_mask(mask)
{
}

LayerViaMaskCommand::~LayerViaMaskCommand()
{
    // 命令析构时 cutLayer 可能既不在文档里也没被别处持有 → 释放它
    if (m_cutLayer && m_document && m_document->indexOf(m_cutLayer) < 0)
        delete m_cutLayer;
}

void LayerViaMaskCommand::redo()
{
    if (!m_document || !m_sourceLayer || !m_cutLayer)
        return;
    if (m_sourceBefore.isNull())
        m_sourceBefore = m_sourceLayer->pixels();

    // 新图层 = 原图层按 mask 抠出的内容(选区外透明)
    QImage cut = m_sourceBefore;
    if (!m_mask.isNull()) {
        // 用 alpha 乘 mask 得到"抠出"效果:base=原图,processed=全透明
        QImage transparent(m_sourceBefore.size(), QImage::Format_ARGB32);
        transparent.fill(Qt::transparent);
        cut = sel::blendByMask(transparent, m_sourceBefore, m_mask);
    } else if (!m_selRect.isEmpty()) {
        QImage cropped(m_sourceBefore.size(), QImage::Format_ARGB32);
        cropped.fill(Qt::transparent);
        {
            QPainter p(&cropped);
            p.drawImage(QPoint(-m_selRect.left(), -m_selRect.top()), m_sourceBefore);
        }
        cut = cropped;
    }
    m_cutLayer->pixels() = cut;

    // 原图层挖空
    m_sourceLayer->pixels() = m_mask.isNull()
                                  ? sel::clearByMask(m_sourceBefore, m_selRect, QImage())
                                  : sel::clearByMask(m_sourceBefore, m_selRect, m_mask);

    // 插入到源图层之上
    const int srcIndex = m_document->indexOf(m_sourceLayer);
    m_document->addLayer(m_cutLayer, srcIndex >= 0 ? srcIndex + 1 : -1);
    m_document->setActiveLayer(m_cutLayer);
}

void LayerViaMaskCommand::undo()
{
    if (!m_document || !m_sourceLayer)
        return;
    // 原图层还原
    m_sourceLayer->pixels() = m_sourceBefore;
    emit m_sourceLayer->changed();
    // 移除新图层(若还在文档里)
    if (m_cutLayer && m_document->indexOf(m_cutLayer) >= 0) {
        m_document->removeLayer(m_cutLayer);
        m_cutLayer->setParent(nullptr);
        m_document->setActiveLayer(m_sourceLayer);
    }
}

// ---------------------------------------------------------------------------
// MagicWandCommand
// ---------------------------------------------------------------------------
MagicWandCommand::MagicWandCommand(QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Magic Wand"), parent)
{
}
