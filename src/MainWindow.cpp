#include "MainWindow.h"

#include "CanvasView.h"
#include "Document.h"
#include "DocumentTab.h"
#include "Adjustments.h"
#include "AdjustmentsUi.h"
#include "Filters.h"
#include "FiltersUi.h"
#include "Layer.h"
#include "LayersPanel.h"
#include "PainterRenderer.h"
#include "PsdReader.h"
#include "PsdWriter.h"
#include "RawProvider.h"
#include "Selection.h"
#include "UndoCommands.h"
#include "VisionProvider.h"

#include <memory>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>

#include <optional>

namespace {

// 把 CanvasView 的画布坐标系 mask 转到指定图层的像素坐标系(按 targetSize 缩放)。
// 返回 null 表示当前没有可用 mask,调用方应退回"整层处理"。
QImage maskForLayerPixels(const CanvasView &canvas, const Layer &layer, const QSize &targetSize)
{
    const QImage canvasMask = canvas.selectionMask();
    if (canvasMask.isNull())
        return {};
    const QSize layerSize = layer.pixels().size();
    if (layerSize.isEmpty())
        return {};
    QImage out(layerSize, QImage::Format_Grayscale8);
    out.fill(0);
    {
        QPainter p(&out);
        p.drawImage(QPoint(-layer.offset().x(), -layer.offset().y()), canvasMask);
    }
    if (out.size() != targetSize)
        return sel::resampleMask(out, targetSize);
    return out;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Compositor Qt"));
    resize(1280, 800);

    // 中心:QTabWidget
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    createActions();
    createMenus();
    createStatusBar();
    createToolBar();
    createDockPanels();

    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    // 启动时打开一个空文档
    auto *doc = new Document(QSize(1200, 800), this);
    doc->addLayer(new Layer(tr("Background"), doc->size(), Qt::white));
    doc->setActiveLayer(doc->layerAt(0));
    createTab(doc);

    m_tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    maybeCloseAllTabs();
    event->accept();
}

// ---------------------------------------------------------------------------
// Tab 操作
// ---------------------------------------------------------------------------
DocumentTab *MainWindow::createTab(Document *document, const QString &path)
{
    auto *tab = new DocumentTab(document, m_tabs);
    if (!path.isEmpty())
        tab->setPath(path);
    const int index = m_tabs->addTab(tab, tab->property("title").toString().isEmpty()
                                           ? QFileInfo(path).fileName()
                                           : tr("Untitled"));
    connect(tab, &DocumentTab::titleChanged, this, [this, tab](const QString &t) {
        const int i = m_tabs->indexOf(tab);
        if (i >= 0)
            m_tabs->setTabText(i, t);
        if (m_tabs->currentWidget() == tab)
            updateTitleFromTab(tab);
    });
    m_tabs->setCurrentIndex(index);
    return tab;
}

DocumentTab *MainWindow::currentTab() const
{
    return qobject_cast<DocumentTab *>(m_tabs ? m_tabs->currentWidget() : nullptr);
}

Document *MainWindow::currentDocument() const
{
    auto *t = currentTab();
    return t ? t->document() : nullptr;
}

CanvasView *MainWindow::currentCanvas() const
{
    auto *t = currentTab();
    return t ? t->canvas() : nullptr;
}

void MainWindow::setCurrentTab(DocumentTab *tab)
{
    const int i = m_tabs->indexOf(tab);
    if (i >= 0)
        m_tabs->setCurrentIndex(i);
}

void MainWindow::updateTitleFromTab(DocumentTab *tab)
{
    const QString base = tab->path().isEmpty()
                             ? tr("Compositor Qt - Untitled")
                             : tr("Compositor Qt - %1").arg(QFileInfo(tab->path()).fileName());
    setWindowTitle(tab->isModified() ? base + tr(" *") : base);
}

void MainWindow::onTabChanged(int index)
{
    auto *tab = index >= 0 ? qobject_cast<DocumentTab *>(m_tabs->widget(index)) : nullptr;
    Document *doc = tab ? tab->document() : nullptr;

    // 1) 同步面板 / 画布
    m_layersPanel->setDocument(doc);
    if (tab)
        m_layersPanel->setUndoStack(doc ? doc->undoStack() : nullptr);

    // 2) Undo / Redo action 跟随当前栈
    disconnect(m_undoAction, nullptr, nullptr, nullptr);
    disconnect(m_redoAction, nullptr, nullptr, nullptr);
    if (doc) {
        connect(m_undoAction, &QAction::triggered, doc->undoStack(), &QUndoStack::undo);
        connect(m_redoAction, &QAction::triggered, doc->undoStack(), &QUndoStack::redo);
        connect(doc->undoStack(), &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);
        connect(doc->undoStack(), &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);
        m_undoAction->setEnabled(doc->undoStack()->canUndo());
        m_redoAction->setEnabled(doc->undoStack()->canRedo());
    } else {
        m_undoAction->setEnabled(false);
        m_redoAction->setEnabled(false);
    }

    // 3) 切换当前 Canvas 信号 -> 重新连接
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *t = qobject_cast<DocumentTab *>(m_tabs->widget(i));
        if (t && t->canvas())
            disconnect(t->canvas(), nullptr, this, nullptr);
    }
    CanvasView *canvas = tab ? tab->canvas() : nullptr;
    if (canvas) {
        connect(canvas, &CanvasView::zoomChanged, this, [this](double z) {
            m_zoomLabel->setText(tr("%1%").arg(qRound(z * 100)));
        });
        connect(canvas, &CanvasView::cursorInfoChanged, this,
                [this](const QString &t) { m_cursorLabel->setText(t); });
        connect(canvas, &CanvasView::colorPicked, this, &MainWindow::onColorPicked);
        connect(canvas, &CanvasView::magicWandTriggered, this, &MainWindow::onMagicWand);
        m_zoomLabel->setText(tr("%1%").arg(qRound(canvas->zoom() * 100)));
        m_cursorLabel->clear();
    } else {
        m_zoomLabel->setText(tr("100%"));
        m_cursorLabel->clear();
    }

    // 4) Title
    if (tab)
        updateTitleFromTab(tab);
    else
        setWindowTitle(tr("Compositor Qt"));

    // 5) Status
    statusBar()->clearMessage();
}

void MainWindow::onTabCloseRequested(int index)
{
    auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(index));
    if (tab)
        closeTab(tab);
}

void MainWindow::closeTab(DocumentTab *tab)
{
    if (!tab)
        return;
    if (tab->isModified()) {
        const QMessageBox::StandardButton ans = QMessageBox::question(
            this, tr("未保存的修改"),
            tr("文档 %1 有未保存的修改,确定关闭?").arg(QFileInfo(tab->path()).fileName()),
            QMessageBox::Discard | QMessageBox::Cancel);
        if (ans != QMessageBox::Discard)
            return;
    }
    m_tabs->removeTab(m_tabs->indexOf(tab));
    delete tab;
}

void MainWindow::maybeCloseAllTabs()
{
    // 简化:全部接受关闭(已修改的会先在 closeTab 询问)
    while (m_tabs->count() > 0) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(0));
        if (!tab)
            break;
        if (tab->isModified()) {
            const QMessageBox::StandardButton ans = QMessageBox::question(
                this, tr("未保存的修改"),
                tr("文档 %1 有未保存的修改,确定关闭?").arg(QFileInfo(tab->path()).fileName()),
                QMessageBox::Discard | QMessageBox::Cancel);
            if (ans != QMessageBox::Cancel)
                return;
        }
        m_tabs->removeTab(0);
        delete tab;
    }
}

// ---------------------------------------------------------------------------
// File 操作
// ---------------------------------------------------------------------------
void MainWindow::fileNew()
{
    auto *doc = new Document(QSize(1200, 800), this);
    doc->addLayer(new Layer(tr("Background"), doc->size(), Qt::white));
    doc->setActiveLayer(doc->layerAt(0));
    createTab(doc);
    statusBar()->showMessage(tr("新建文档"), 3000);
}

void MainWindow::fileOpen()
{
    // 文件过滤器包含主流格式 + 相机 RAW
    const QString filter = tr(
        "All Images (*.psd *.png *.jpg *.jpeg *.bmp "
        "*.dng *.cr2 *.cr3 *.nef *.arw *.rw2 *.raf *.orf);;"
        "PSD (*.psd);;PNG (*.png);;JPEG (*.jpg *.jpeg);;"
        "Camera RAW (*.dng *.cr2 *.cr3 *.nef *.arw *.rw2 *.raf *.orf)");
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Image"), QString(), filter);
    if (path.isEmpty())
        return;

    QImage image;
    bool ok = false;
    QString error;

    if (RawDecoder::isRawFile(path)) {
        // 相机 RAW → 通过 RawDecoder 抽象(内部用 dcraw_emu/LibRaw)
        std::unique_ptr<RawDecoder> dec(RawDecoder::create());
        if (dec->decode(path, image, &error)) {
            ok = true;
        }
    } else if (path.endsWith(".psd", Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Open Failed"), tr("无法打开文件: %1").arg(file.errorString()));
            return;
        }
        const QByteArray data = file.readAll();
        const auto r = psd::Reader::read(data);
        if (r.ok) {
            image = r.composite;
            ok = true;
        } else {
            error = r.error;
        }
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Open Failed"), tr("无法打开文件: %1").arg(file.errorString()));
            return;
        }
        const QByteArray data = file.readAll();
        if (image.loadFromData(data))
            ok = true;
        else
            error = tr("无法解码图片文件");
    }

    if (!ok) {
        QMessageBox::warning(this, tr("Open Failed"), tr("打开失败: %1").arg(error));
        return;
    }
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Open Failed"), tr("图像为空"));
        return;
    }
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    auto *doc = new Document(image.size(), this);
    auto *layer = new Layer(tr("Background"), image.size());
    layer->pixels() = image;
    doc->addLayer(layer);
    doc->setActiveLayer(layer);
    createTab(doc, path);
    statusBar()->showMessage(tr("已打开 %1").arg(path), 3000);
}

void MainWindow::fileSave()
{
    auto *tab = currentTab();
    if (!tab) {
        statusBar()->showMessage(tr("没有可保存的文档"), 3000);
        return;
    }
    Document *doc = tab->document();
    QString path = tab->path();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Image"), QString(),
            tr("PSD (*.psd);;PNG (*.png);;JPEG (*.jpg *.jpeg)"));
        if (path.isEmpty())
            return;
        tab->setPath(path);
    }

    bool ok = false;
    QString error;
    if (path.endsWith(".psd", Qt::CaseInsensitive)) {
        const QByteArray bytes = psd::writePsd(*doc, {}, &error);
        if (!bytes.isEmpty()) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                ok = (f.write(bytes) == bytes.size());
                if (!ok) error = f.errorString();
            } else {
                error = f.errorString();
            }
        }
    } else {
        PainterRenderer renderer;
        const QImage composite = renderer.render(*doc);
        ok = composite.save(path);
        if (!ok) error = tr("无法保存图片");
    }

    if (ok) {
        doc->undoStack()->setClean();
        updateTitleFromTab(tab);
        const int i = m_tabs->indexOf(tab);
        m_tabs->setTabText(i, QFileInfo(path).fileName());
        statusBar()->showMessage(tr("已保存 %1").arg(path), 3000);
    } else {
        QMessageBox::warning(this, tr("Save Failed"), tr("保存失败: %1").arg(error));
    }
}

void MainWindow::fileClose()
{
    closeTab(currentTab());
}

// ---------------------------------------------------------------------------
// 滤镜(都在当前 tab 的活动图层上操作)
// ---------------------------------------------------------------------------
void MainWindow::applyFilterToActiveLayer(const QString &name,
                                          const std::function<QImage(const QImage &)> &fn)
{
    Document *doc = currentDocument();
    if (!doc || !doc->activeLayer()) {
        statusBar()->showMessage(tr("没有活动图层"), 3000);
        return;
    }
    CanvasView *canvas = currentCanvas();
    Layer *layer = doc->activeLayer();
    const QImage before = layer->pixels();
    if (before.isNull())
        return;
    const QImage processed = fn(before);
    if (processed.isNull())
        return;

    // 有 mask(Magic Wand)→ 滤镜只作用于选区内,选区外保持原样(羽化边缘自然过渡)
    QImage after = processed;
    if (canvas && !canvas->selectionMask().isNull()) {
        const QImage mask = maskForLayerPixels(*canvas, *layer, before.size());
        if (!mask.isNull())
            after = sel::blendByMask(before, processed, mask);
    }
    if (after == before)
        return;
    doc->undoStack()->push(new FilterCommand(layer, before, after, name));
    statusBar()->showMessage(tr("已应用 %1").arg(name), 3000);
}

void MainWindow::filterGaussianBlur()
{
    const auto p = filtui::promptGaussianBlur(this);
    if (!p) return;
    applyFilterToActiveLayer(tr("Gaussian Blur"), [p](const QImage &src) {
        return filt::gaussianBlur(src, p->radius);
    });
}

void MainWindow::filterMotionBlur()
{
    const auto p = filtui::promptMotionBlur(this);
    if (!p) return;
    applyFilterToActiveLayer(tr("Motion Blur"), [p](const QImage &src) {
        return filt::motionBlur(src, p->distance, p->angle);
    });
}

void MainWindow::filterAddNoise()
{
    const auto p = filtui::promptAddNoise(this);
    if (!p) return;
    applyFilterToActiveLayer(tr("Add Noise"), [p](const QImage &src) {
        return filt::addNoise(src, p->amount, p->gaussian, p->monochromatic, p->seed);
    });
}

void MainWindow::filterVignette()
{
    const auto p = filtui::promptVignette(this);
    if (!p) return;
    applyFilterToActiveLayer(tr("Vignette"), [p](const QImage &src) {
        return filt::vignette(src, p->amount, p->color, p->midpoint, p->roundness, p->feather);
    });
}

// ---------------------------------------------------------------------------
// 调色(作用于当前 tab 的 Document.renderLut,非破坏性)
// ---------------------------------------------------------------------------
void MainWindow::applyLutAdjustment(
    const QString &name, const std::function<std::optional<adjui::LutApplyResult>()> &prompt)
{
    Document *doc = currentDocument();
    if (!doc) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    const auto res = prompt();
    if (!res)
        return;
    doc->undoStack()->push(new AdjustLutCommand(doc, doc->renderLut(), res->lut, name));
    statusBar()->showMessage(tr("已应用 %1").arg(name), 3000);
}

void MainWindow::adjLevels()
{
    applyLutAdjustment(tr("Levels"), [this] { return adjui::promptAndBuildLevels(this); });
}
void MainWindow::adjBrightnessContrast()
{
    applyLutAdjustment(tr("Brightness/Contrast"),
                       [this] { return adjui::promptAndBuildBrightnessContrast(this); });
}
void MainWindow::adjExposure()
{
    applyLutAdjustment(tr("Exposure"), [this] { return adjui::promptAndBuildExposure(this); });
}
void MainWindow::adjCurves()
{
    applyLutAdjustment(tr("Curves"), [this] { return adjui::promptAndBuildCurves(this); });
}

// 注:旧的 adjHueSaturation() 已被 adjHueSaturationLive()(非破坏性)替代,这里保留函数体空
//      以便未来若要恢复破坏性版本不破坏签名。
void MainWindow::adjHueSaturation()
{
    // no-op
}

void MainWindow::adjHueSaturationLive()
{
    Document *doc = currentDocument();
    if (!doc) { statusBar()->showMessage(tr("没有活动文档"), 3000); return; }
    const auto p = adjui::promptHueSaturation(this);
    if (!p) return;
    doc->undoStack()->push(new HsvCommand(doc, doc->hsv(), *p));
    statusBar()->showMessage(tr("已应用 Hue/Saturation(GPU)"), 3000);
}

void MainWindow::adjBlackWhiteLive()
{
    Document *doc = currentDocument();
    if (!doc) { statusBar()->showMessage(tr("没有活动文档"), 3000); return; }
    bool ok = false;
    adj::BwParams p = doc->blackWhite();
    p.reds     = QInputDialog::getInt(this, tr("黑白(GPU) · 红"),   tr("红(0-100)"),   p.reds,     0, 200, 1, &ok); if (!ok) return;
    p.yellows  = QInputDialog::getInt(this, tr("黑白(GPU) · 黄"),   tr("黄(0-100)"),   p.yellows,  0, 200, 1, &ok); if (!ok) return;
    p.greens   = QInputDialog::getInt(this, tr("黑白(GPU) · 绿"),   tr("绿(0-100)"),   p.greens,   0, 200, 1, &ok); if (!ok) return;
    p.cyans    = QInputDialog::getInt(this, tr("黑白(GPU) · 青"),   tr("青(0-100)"),   p.cyans,    0, 200, 1, &ok); if (!ok) return;
    p.blues    = QInputDialog::getInt(this, tr("黑白(GPU) · 蓝"),   tr("蓝(0-100)"),   p.blues,    0, 200, 1, &ok); if (!ok) return;
    p.magentas = QInputDialog::getInt(this, tr("黑白(GPU) · 品红"), tr("品红(0-100)"), p.magentas, 0, 200, 1, &ok); if (!ok) return;
    const QMessageBox::StandardButton t = QMessageBox::question(
        this, tr("黑白 · 上色"), tr("应用 tint 上色?"),
        QMessageBox::Yes | QMessageBox::No, p.tint ? QMessageBox::Yes : QMessageBox::No);
    if (t == QMessageBox::Cancel) return; // 不会发生
    p.tint = (t == QMessageBox::Yes);
    if (p.tint) {
        p.tintHue = QInputDialog::getInt(this, tr("黑白 · tint 色相"), tr("色相(0-360)"), p.tintHue, 0, 360, 1, &ok); if (!ok) return;
        p.tintSaturation = QInputDialog::getInt(this, tr("黑白 · tint 饱和度"), tr("饱和度(0-100)"), p.tintSaturation, 0, 100, 1, &ok); if (!ok) return;
    }
    doc->undoStack()->push(new BlackWhiteCommand(doc, doc->blackWhite(), p));
    statusBar()->showMessage(tr("已应用黑白(GPU)"), 3000);
}

void MainWindow::adjColorBalanceLive()
{
    Document *doc = currentDocument();
    if (!doc) { statusBar()->showMessage(tr("没有活动文档"), 3000); return; }
    bool ok = false;
    adj::CbParams p = doc->colorBalance();
    // 阴影
    p.shadowCyanRed      = QInputDialog::getInt(this, tr("色彩平衡 · 阴影 · 青/红"),   tr("青/红(-100~100)"),  p.shadowCyanRed,      -100, 100, 1, &ok); if (!ok) return;
    p.shadowMagentaGreen = QInputDialog::getInt(this, tr("色彩平衡 · 阴影 · 品红/绿"), tr("品红/绿(-100~100)"), p.shadowMagentaGreen, -100, 100, 1, &ok); if (!ok) return;
    p.shadowYellowBlue   = QInputDialog::getInt(this, tr("色彩平衡 · 阴影 · 黄/蓝"),   tr("黄/蓝(-100~100)"),   p.shadowYellowBlue,   -100, 100, 1, &ok); if (!ok) return;
    // 中间调
    p.midCyanRed         = QInputDialog::getInt(this, tr("色彩平衡 · 中调 · 青/红"),   tr("青/红(-100~100)"),  p.midCyanRed,         -100, 100, 1, &ok); if (!ok) return;
    p.midMagentaGreen    = QInputDialog::getInt(this, tr("色彩平衡 · 中调 · 品红/绿"), tr("品红/绿(-100~100)"), p.midMagentaGreen,    -100, 100, 1, &ok); if (!ok) return;
    p.midYellowBlue      = QInputDialog::getInt(this, tr("色彩平衡 · 中调 · 黄/蓝"),   tr("黄/蓝(-100~100)"),   p.midYellowBlue,      -100, 100, 1, &ok); if (!ok) return;
    // 高光
    p.highlightCyanRed      = QInputDialog::getInt(this, tr("色彩平衡 · 高光 · 青/红"),   tr("青/红(-100~100)"),  p.highlightCyanRed,      -100, 100, 1, &ok); if (!ok) return;
    p.highlightMagentaGreen = QInputDialog::getInt(this, tr("色彩平衡 · 高光 · 品红/绿"), tr("品红/绿(-100~100)"), p.highlightMagentaGreen, -100, 100, 1, &ok); if (!ok) return;
    p.highlightYellowBlue   = QInputDialog::getInt(this, tr("色彩平衡 · 高光 · 黄/蓝"),   tr("黄/蓝(-100~100)"),   p.highlightYellowBlue,   -100, 100, 1, &ok); if (!ok) return;
    doc->undoStack()->push(new ColorBalanceCommand(doc, doc->colorBalance(), p));
    statusBar()->showMessage(tr("已应用色彩平衡(GPU)"), 3000);
}

void MainWindow::adjInvert()
{
    Document *doc = currentDocument();
    if (!doc || !doc->activeLayer()) {
        statusBar()->showMessage(tr("没有活动图层"), 3000);
        return;
    }
    Layer *layer = doc->activeLayer();
    const QImage before = layer->pixels();
    QImage after = before;
    adj::applyInvert(after);
    if (after == before) return;
    doc->undoStack()->push(new FilterCommand(layer, before, after, tr("Invert")));
    statusBar()->showMessage(tr("已应用 Invert"), 3000);
}

void MainWindow::adjResetLut()
{
    Document *doc = currentDocument();
    if (!doc) return;
    if (doc->renderLut().isNull()) return;
    doc->undoStack()->push(new AdjustLutCommand(doc, doc->renderLut(),
                                               adj::identityLut(), tr("Reset Adjustments")));
    statusBar()->showMessage(tr("已重置调色"), 3000);
}

void MainWindow::adjGaussianBlurLive()
{
    // 非破坏性高斯模糊:弹半径对话框 → 走 GaussianBlurCommand(GpuRenderer/PainterRenderer 都用 radius 字段)
    Document *doc = currentDocument();
    if (!doc) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    const int current = int(std::ceil(doc->gaussianBlurRadius()));
    bool ok = false;
    const int radius = QInputDialog::getInt(this, tr("高斯模糊(GPU 预览)"),
                                           tr("半径(像素)"), current, 0, 64, 1, &ok);
    if (!ok)
        return;
    const double r = double(radius);
    doc->undoStack()->push(new GaussianBlurCommand(doc, doc->gaussianBlurRadius(), r));
    statusBar()->showMessage(tr("已应用高斯模糊(GPU 预览)"), 3000);
}

void MainWindow::adjVignetteLive()
{
    Document *doc = currentDocument();
    if (!doc) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    bool ok = false;
    const int amount = QInputDialog::getInt(this, tr("暗角(GPU 预览)"),
                                            tr("数量(0-100)"), int(doc->vignetteAmount()), 0, 100, 1, &ok);
    if (!ok) return;
    const int mid = QInputDialog::getInt(this, tr("暗角 · 中点"), tr("中点(0-100)"),
                                         int(doc->vignetteMidpoint()), 0, 100, 1, &ok);
    if (!ok) return;
    const int round = QInputDialog::getInt(this, tr("暗角 · 圆度"), tr("圆度(-100~100)"),
                                            int(doc->vignetteRoundness()), -100, 100, 1, &ok);
    if (!ok) return;
    const int feather = QInputDialog::getInt(this, tr("暗角 · 羽化"), tr("羽化(0-100)"),
                                             int(doc->vignetteFeather()), 0, 100, 1, &ok);
    if (!ok) return;
    doc->undoStack()->push(new VignetteCommand(doc,
                                               doc->vignetteAmount(), doc->vignetteMidpoint(),
                                               doc->vignetteRoundness(), doc->vignetteFeather(),
                                               double(amount), double(mid), double(round), double(feather)));
    statusBar()->showMessage(tr("已应用暗角(GPU 预览)"), 3000);
}

void MainWindow::adjMotionBlurLive()
{
    Document *doc = currentDocument();
    if (!doc) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    bool ok = false;
    const int dist = QInputDialog::getInt(this, tr("动感模糊(GPU 预览)"),
                                          tr("距离(像素)"), int(doc->motionBlurDistance()), 0, 100, 1, &ok);
    if (!ok) return;
    const int angle = QInputDialog::getInt(this, tr("动感模糊 · 角度"), tr("角度(度)"),
                                           int(doc->motionBlurAngle()), 0, 360, 1, &ok);
    if (!ok) return;
    doc->undoStack()->push(new MotionBlurCommand(doc,
                                                 doc->motionBlurDistance(), doc->motionBlurAngle(),
                                                 double(dist), double(angle)));
    statusBar()->showMessage(tr("已应用动感模糊(GPU 预览)"), 3000);
}

void MainWindow::adjAddNoiseLive()
{
    Document *doc = currentDocument();
    if (!doc) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    bool ok = false;
    const int amount = QInputDialog::getInt(this, tr("杂色(GPU 预览)"),
                                            tr("数量(0-100)"), int(doc->noiseAmount()), 0, 100, 1, &ok);
    if (!ok) return;
    // gaussian/mono 用 QMessageBox 简易选择(避免新对话框)
    const QMessageBox::StandardButton g = QMessageBox::question(
        this, tr("杂色 · 分布"), tr("高斯分布?"), QMessageBox::Yes | QMessageBox::No,
        doc->noiseGaussian() ? QMessageBox::Yes : QMessageBox::No);
    if (g == QMessageBox::Cancel) return; // 不会发生(只有 Yes/No),但避免 IDE 警告
    const QMessageBox::StandardButton m = QMessageBox::question(
        this, tr("杂色 · 单色"), tr("单色(灰度)?") , QMessageBox::Yes | QMessageBox::No,
        doc->noiseMonochromatic() ? QMessageBox::Yes : QMessageBox::No);
    const bool mono = (m == QMessageBox::Yes);
    const quint32 seed = quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFu);
    doc->undoStack()->push(new AddNoiseCommand(doc,
                                               doc->noiseAmount(), doc->noiseGaussian(),
                                               doc->noiseMonochromatic(), doc->noiseSeed(),
                                               double(amount), g == QMessageBox::Yes,
                                               mono, seed));
    statusBar()->showMessage(tr("已应用杂色(GPU 预览)"), 3000);
}

// ---------------------------------------------------------------------------
// UI 构造
// ---------------------------------------------------------------------------
void MainWindow::createActions()
{
    m_newAction = new QAction(tr("&New"), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::fileNew);

    m_openAction = new QAction(tr("&Open..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::fileOpen);

    m_saveAction = new QAction(tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::fileSave);

    m_closeAction = new QAction(tr("&Close Tab"), this);
    m_closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::fileClose);

    m_quitAction = new QAction(tr("E&xit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = new QAction(tr("&Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);

    m_redoAction = new QAction(tr("&Redo"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);

    m_zoomInAction = new QAction(tr("Zoom &In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, [this] {
        if (auto *c = currentCanvas()) c->zoomIn();
    });

    m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, [this] {
        if (auto *c = currentCanvas()) c->zoomOut();
    });

    m_zoomFitAction = new QAction(tr("Zoom to &Fit"), this);
    m_zoomFitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_zoomFitAction, &QAction::triggered, this, [this] {
        if (auto *c = currentCanvas()) c->zoomToFit();
    });

    m_zoomActualAction = new QAction(tr("Actual Si&ze"), this);
    m_zoomActualAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0));
    connect(m_zoomActualAction, &QAction::triggered, this, [this] {
        if (auto *c = currentCanvas()) c->zoomToActual();
    });

    m_filterGaussianAction = new QAction(tr("Gaussian &Blur..."), this);
    connect(m_filterGaussianAction, &QAction::triggered, this, &MainWindow::filterGaussianBlur);
    m_filterMotionAction = new QAction(tr("&Motion Blur..."), this);
    connect(m_filterMotionAction, &QAction::triggered, this, &MainWindow::filterMotionBlur);
    m_filterNoiseAction = new QAction(tr("Add &Noise..."), this);
    connect(m_filterNoiseAction, &QAction::triggered, this, &MainWindow::filterAddNoise);
    m_filterVignetteAction = new QAction(tr("&Vignette..."), this);
    connect(m_filterVignetteAction, &QAction::triggered, this, &MainWindow::filterVignette);

    m_adjLevelsAction = new QAction(tr("&Levels..."), this);
    connect(m_adjLevelsAction, &QAction::triggered, this, &MainWindow::adjLevels);
    m_adjBcAction = new QAction(tr("Brightness/&Contrast..."), this);
    connect(m_adjBcAction, &QAction::triggered, this, &MainWindow::adjBrightnessContrast);
    m_adjExposureAction = new QAction(tr("E&xposure..."), this);
    connect(m_adjExposureAction, &QAction::triggered, this, &MainWindow::adjExposure);
    m_adjCurvesAction = new QAction(tr("&Curves..."), this);
    connect(m_adjCurvesAction, &QAction::triggered, this, &MainWindow::adjCurves);
    m_adjHsvAction = new QAction(tr("&Hue/Saturation (GPU)..."), this);
    connect(m_adjHsvAction, &QAction::triggered, this, &MainWindow::adjHueSaturationLive);
    m_adjBwAction = new QAction(tr("&Black & White (GPU)..."), this);
    connect(m_adjBwAction, &QAction::triggered, this, &MainWindow::adjBlackWhiteLive);
    m_adjCbAction = new QAction(tr("Color &Balance (GPU)..."), this);
    connect(m_adjCbAction, &QAction::triggered, this, &MainWindow::adjColorBalanceLive);
    m_adjInvertAction = new QAction(tr("&Invert"), this);
    connect(m_adjInvertAction, &QAction::triggered, this, &MainWindow::adjInvert);
    m_adjResetAction = new QAction(tr("&Reset Adjustments"), this);
    connect(m_adjResetAction, &QAction::triggered, this, &MainWindow::adjResetLut);
    m_cropToSelectionAction = new QAction(tr("Crop to &Selection"), this);
    m_cropToSelectionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Shift | Qt::Key_C));
    connect(m_cropToSelectionAction, &QAction::triggered, this, &MainWindow::cropToSelection);

    m_adjGaussianLiveAction = new QAction(tr("Gaussian Blur (GPU)..."), this);
    connect(m_adjGaussianLiveAction, &QAction::triggered, this, &MainWindow::adjGaussianBlurLive);

    m_adjVignetteLiveAction = new QAction(tr("Vignette (GPU)..."), this);
    connect(m_adjVignetteLiveAction, &QAction::triggered, this, &MainWindow::adjVignetteLive);
    m_adjMotionBlurLiveAction = new QAction(tr("Motion Blur (GPU)..."), this);
    connect(m_adjMotionBlurLiveAction, &QAction::triggered, this, &MainWindow::adjMotionBlurLive);

    m_adjNoiseLiveAction = new QAction(tr("Add Noise (GPU)..."), this);
    connect(m_adjNoiseLiveAction, &QAction::triggered, this, &MainWindow::adjAddNoiseLive);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomFitAction);
    viewMenu->addAction(m_zoomActualAction);

    QMenu *filterMenu = menuBar()->addMenu(tr("&Filter"));
    filterMenu->addAction(m_filterGaussianAction);
    filterMenu->addAction(m_filterMotionAction);
    filterMenu->addSeparator();
    filterMenu->addAction(m_filterNoiseAction);
    filterMenu->addAction(m_filterVignetteAction);

    QMenu *imageMenu = menuBar()->addMenu(tr("&Image"));
    imageMenu->addAction(m_adjLevelsAction);
    imageMenu->addAction(m_adjCurvesAction);
    imageMenu->addAction(m_adjExposureAction);
    imageMenu->addAction(m_adjBcAction);
    imageMenu->addAction(m_adjHsvAction);
    imageMenu->addAction(m_adjBwAction);
    imageMenu->addAction(m_adjCbAction);
    imageMenu->addSeparator();
    imageMenu->addAction(m_adjInvertAction);
    imageMenu->addSeparator();
    imageMenu->addAction(m_cropToSelectionAction);
    imageMenu->addSeparator();
    imageMenu->addAction(m_adjGaussianLiveAction);
    imageMenu->addAction(m_adjVignetteLiveAction);
    imageMenu->addAction(m_adjMotionBlurLiveAction);
    imageMenu->addAction(m_adjNoiseLiveAction);
    imageMenu->addAction(m_adjResetAction);
}

void MainWindow::createStatusBar()
{
    m_zoomLabel = new QLabel(this);
    m_cursorLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->showMessage(tr("Ready"), 3000);
    m_zoomLabel->setText(tr("100%"));
}

void MainWindow::createToolBar()
{
    auto *toolBar = addToolBar(tr("Tools"));
    toolBar->setObjectName(QStringLiteral("ToolsToolBar"));
    toolBar->setMovable(false);

    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    auto addTool = [&](const QString &text, const QKeySequence &key, Tool tool) {
        QAction *a = toolBar->addAction(text);
        a->setCheckable(true);
        a->setShortcut(key);
        a->setToolTip(text + QStringLiteral(" (") + key.toString() + QStringLiteral(")"));
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, tool] {
            if (auto *c = currentCanvas()) c->setTool(tool);
        });
        return a;
    };
    m_toolBrushAction     = addTool(tr("画笔"), Qt::Key_B, Tool::Brush);
    m_toolMoveAction      = addTool(tr("移动"), Qt::Key_V, Tool::Move);
    m_toolEyedropperAction= addTool(tr("吸管"), Qt::Key_I, Tool::Eyedropper);
    m_toolSelectAction    = addTool(tr("选区"), Qt::Key_M, Tool::RectSelect);
    m_toolMagicWandAction = addTool(tr("智能选择"), Qt::Key_W, Tool::MagicWand);
    m_toolBrushAction->setChecked(true);

    // 工具切换时同步菜单/动作显示
    auto syncTools = [this](Tool tool) {
        m_toolBrushAction->setChecked(tool == Tool::Brush);
        m_toolMoveAction->setChecked(tool == Tool::Move);
        m_toolEyedropperAction->setChecked(tool == Tool::Eyedropper);
        m_toolSelectAction->setChecked(tool == Tool::RectSelect);
    };
    Q_UNUSED(syncTools); // 当前通过 CanvasView 自身 cursor 管理即可
}

void MainWindow::createDockPanels() // 由构造函数以成员顺序调用,在 m_tabs 之后
{
    m_layersPanel = new LayersPanel(this);
    auto *dock = new QDockWidget(tr("Layers"), this);
    dock->setObjectName(QStringLiteral("LayersDock"));
    dock->setWidget(m_layersPanel);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::onColorPicked(const QColor &color)
{
    if (auto *c = currentCanvas()) c->setBrushColor(color);
    statusBar()->showMessage(tr("画笔颜色: %1").arg(color.name()), 3000);
}

void MainWindow::onMagicWand(const QPoint &canvasPos)
{
    Document *doc = currentDocument();
    if (!doc) { statusBar()->showMessage(tr("没有活动文档"), 3000); return; }
    CanvasView *canvas = currentCanvas();
    if (!canvas) return;

    QImage composite;
    {
        PainterRenderer r;
        composite = r.render(*doc);
    }
    if (composite.isNull()) { statusBar()->showMessage(tr("合成图为空"), 3000); return; }

    std::unique_ptr<VisionProvider> vp(VisionProvider::create());
    QImage mask;
    QString err;
    if (!vp->selectObject(composite, canvasPos, mask, &err)) {
        QMessageBox::warning(this, tr("智能选择失败"), err);
        return;
    }
    if (mask.isNull()) {
        statusBar()->showMessage(tr("智能选择未识别到主体"), 5000);
        return;
    }

    // mask 必须与文档尺寸一致(VisionProvider 已经会按 image.size() 缩放)
    if (mask.size() != doc->size()) {
        mask = mask.scaled(doc->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        mask = mask.convertToFormat(QImage::Format_Grayscale8);
    }

    // 计算 mask 的 bounding rect
    QRect maskRect;
    {
        int minX = mask.width(), minY = mask.height(), maxX = -1, maxY = -1;
        for (int y = 0; y < mask.height(); ++y) {
            const uchar *row = mask.constScanLine(y);
            for (int x = 0; x < mask.width(); ++x) {
                if (row[x] > 0) {
                    if (x < minX) minX = x;
                    if (y < minY) minY = y;
                    if (x > maxX) maxX = x;
                    if (y > maxY) maxY = y;
                }
            }
        }
        if (maxX >= 0)
            maskRect = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    }
    if (maskRect.isEmpty()) {
        statusBar()->showMessage(tr("智能选择结果为空"), 5000);
        return;
    }

    // 1) 把 mask 转成 RGBA 图层像素:RGB=橙红,Alpha=mask。
    //    作为"Alpha 蒙版图层"插入到图层栈顶部,默认可见;用户可隐藏。
    QImage layerPx(doc->size(), QImage::Format_ARGB32_Premultiplied);
    layerPx.fill(Qt::transparent);
    const QColor tint(255, 160, 60);
    for (int y = 0; y < layerPx.height(); ++y) {
        const uchar *src = mask.constScanLine(y);
        QRgb *dst = reinterpret_cast<QRgb *>(layerPx.scanLine(y));
        for (int x = 0; x < layerPx.width(); ++x) {
            const int a = src[x];
            if (a == 0) continue;
            // 预乘模式下 color *= alpha/255
            const int aa = (tint.alpha() * a) / 255;
            const int cr = (tint.red()   * aa) / 255;
            const int cg = (tint.green() * aa) / 255;
            const int cb = (tint.blue()  * aa) / 255;
            dst[x] = qRgba(cr, cg, cb, aa);
        }
    }
    auto *maskLayer = new Layer(tr("Magic Wand Mask"), doc->size(), Qt::transparent);
    maskLayer->pixels() = layerPx;
    maskLayer->setOpacity(0.55); // 提示色,不太刺眼

    // 2) 复合撤销:在一次 push 里完成"加图层 + 改选区",用户一次 Undo 即可还原
    const QRect oldSel = canvas->selectionRect();
    const QImage oldMask = canvas->selectionMask();
    auto *mwCmd = new MagicWandCommand();
    new AddLayerCommand(doc, maskLayer, -1, mwCmd);
    new SelectionCommand(canvas, oldSel, maskRect, oldMask, mask, mwCmd);
    doc->undoStack()->push(mwCmd);

    // 3) 状态栏
    statusBar()->showMessage(
        tr("智能选择完成:蒙版 %1x%2,选区 (%3,%4)-(%5,%6),命中 %7 像素")
            .arg(mask.width()).arg(mask.height())
            .arg(maskRect.left()).arg(maskRect.top())
            .arg(maskRect.right()).arg(maskRect.bottom())
            .arg(int(maskRect.width() * maskRect.height())),
        5000);
}

void MainWindow::onUndoStackChanged()
{
    if (auto *tab = currentTab())
        updateTitleFromTab(tab);
}

void MainWindow::cropToSelection()
{
    Document *doc = currentDocument();
    CanvasView *canvas = currentCanvas();
    if (!doc || !canvas) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    if (!doc->activeLayer()) {
        statusBar()->showMessage(tr("没有活动图层"), 3000);
        return;
    }
    const QRect selRect = canvas->selectionRect();
    if (selRect.isEmpty()) {
        statusBar()->showMessage(tr("没有选区,请先用选区或智能选择工具"), 3000);
        return;
    }

    Layer *layer = doc->activeLayer();
    const QImage before = layer->pixels();
    if (before.isNull())
        return;

    // mask 感知:有 mask 用 mask 轮廓(Magic Wand),否则用矩形(RectSelect)
    const QImage mask = maskForLayerPixels(*canvas, *layer, before.size());
    const QImage after = sel::cropToMask(before, selRect.translated(-layer->offset()), mask);
    if (after.isNull() || after == before) {
        statusBar()->showMessage(tr("选区裁剪无变化"), 3000);
        return;
    }

    // 裁剪改变画布尺寸 → 这会同时影响所有图层,这里只裁当前图层并在状态栏说明。
    // 文档级裁剪(所有图层 + 画布尺寸)留待后续实现。
    doc->undoStack()->push(new FilterCommand(layer, before, after, tr("Crop to Selection")));
    statusBar()->showMessage(tr("已按选区裁剪图层 → %1x%2").arg(after.width()).arg(after.height()), 4000);
}
