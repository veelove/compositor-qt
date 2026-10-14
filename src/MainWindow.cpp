#include "MainWindow.h"

#include "CanvasView.h"
#include "Document.h"
#include "Adjustments.h"
#include "AdjustmentsUi.h"
#include "Filters.h"
#include "FiltersUi.h"
#include "Layer.h"
#include "LayersPanel.h"
#include "PainterRenderer.h"
#include "PsdReader.h"
#include "PsdWriter.h"
#include "UndoCommands.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Compositor Qt"));
    resize(1280, 800);

    m_canvas = new CanvasView(this);
    setCentralWidget(m_canvas);
    m_canvas->setUndoStack(&m_undoStack);

    createActions();
    createMenus();
    createStatusBar();
    createDockPanels();
    createToolBar();

    connect(m_canvas, &CanvasView::zoomChanged, this, [this](double zoom) {
        m_zoomLabel->setText(tr("%1%").arg(qRound(zoom * 100)));
    });
    connect(m_canvas, &CanvasView::cursorInfoChanged, this, [this](const QString &text) {
        m_cursorLabel->setText(text);
    });
    connect(m_canvas, &CanvasView::toolChanged, this, [this](Tool tool) {
        syncToolActions(int(tool));
    });
    connect(m_canvas, &CanvasView::colorPicked, this, &MainWindow::onColorPicked);

    m_canvas->setFocus();
}

MainWindow::~MainWindow()
{
    clearDocument();
}

void MainWindow::createActions()
{
    m_newAction = new QAction(tr("&New"), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newDocument);

    m_openAction = new QAction(tr("&Open..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openDocument);

    m_saveAction = new QAction(tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(true); // 有文档即可保存,openDocument 后自动有

    m_quitAction = new QAction(tr("E&xit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = new QAction(tr("&Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, &m_undoStack, &QUndoStack::undo);
    connect(&m_undoStack, &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);
    m_undoAction->setEnabled(false);

    m_redoAction = new QAction(tr("&Redo"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, &m_undoStack, &QUndoStack::redo);
    connect(&m_undoStack, &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);
    m_redoAction->setEnabled(false);

    m_zoomInAction = new QAction(tr("Zoom &In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, m_canvas, &CanvasView::zoomIn);

    m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, m_canvas, &CanvasView::zoomOut);

    m_zoomFitAction = new QAction(tr("Zoom to &Fit"), this);
    m_zoomFitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_zoomFitAction, &QAction::triggered, m_canvas, &CanvasView::zoomToFit);

    m_zoomActualAction = new QAction(tr("Actual Si&ze"), this);
    m_zoomActualAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0));
    connect(m_zoomActualAction, &QAction::triggered, m_canvas, &CanvasView::zoomToActual);

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
    m_adjHsvAction = new QAction(tr("&Hue/Saturation..."), this);
    connect(m_adjHsvAction, &QAction::triggered, this, &MainWindow::adjHueSaturation);
    m_adjInvertAction = new QAction(tr("&Invert"), this);
    connect(m_adjInvertAction, &QAction::triggered, this, &MainWindow::adjInvert);
    m_adjResetAction = new QAction(tr("&Reset Adjustments"), this);
    connect(m_adjResetAction, &QAction::triggered, this, &MainWindow::adjResetLut);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
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
    imageMenu->addSeparator();
    imageMenu->addAction(m_adjInvertAction);
    imageMenu->addSeparator();
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

void MainWindow::createDockPanels()
{
    m_layersPanel = new LayersPanel(this);
    m_layersPanel->setUndoStack(&m_undoStack);
    auto *dock = new QDockWidget(tr("Layers"), this);
    dock->setObjectName(QStringLiteral("LayersDock"));
    dock->setWidget(m_layersPanel);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::createToolBar()
{
    auto *toolBar = addToolBar(tr("Tools"));
    toolBar->setObjectName(QStringLiteral("ToolsToolBar"));
    toolBar->setMovable(false);

    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    m_toolBrushAction = toolBar->addAction(tr("画笔"));
    m_toolBrushAction->setCheckable(true);
    m_toolBrushAction->setChecked(true);
    m_toolBrushAction->setShortcut(QKeySequence(Qt::Key_B));
    m_toolBrushAction->setToolTip(tr("画笔 (B)"));

    m_toolMoveAction = toolBar->addAction(tr("移动"));
    m_toolMoveAction->setCheckable(true);
    m_toolMoveAction->setShortcut(QKeySequence(Qt::Key_V));
    m_toolMoveAction->setToolTip(tr("移动图层 (V)"));

    m_toolEyedropperAction = toolBar->addAction(tr("吸管"));
    m_toolEyedropperAction->setCheckable(true);
    m_toolEyedropperAction->setShortcut(QKeySequence(Qt::Key_I));
    m_toolEyedropperAction->setToolTip(tr("吸管取色 (I)"));

    m_toolSelectAction = toolBar->addAction(tr("选区"));
    m_toolSelectAction->setCheckable(true);
    m_toolSelectAction->setShortcut(QKeySequence(Qt::Key_M));
    m_toolSelectAction->setToolTip(tr("矩形选区 (M) · Delete 清除"));

    group->addAction(m_toolBrushAction);
    group->addAction(m_toolMoveAction);
    group->addAction(m_toolEyedropperAction);
    group->addAction(m_toolSelectAction);

    connect(m_toolBrushAction, &QAction::triggered, this, [this] { m_canvas->setTool(Tool::Brush); });
    connect(m_toolMoveAction, &QAction::triggered, this, [this] { m_canvas->setTool(Tool::Move); });
    connect(m_toolEyedropperAction, &QAction::triggered, this, [this] { m_canvas->setTool(Tool::Eyedropper); });
    connect(m_toolSelectAction, &QAction::triggered, this, [this] { m_canvas->setTool(Tool::RectSelect); });
}

void MainWindow::syncToolActions(int toolIndex)
{
    const QList<QAction *> actions{ m_toolBrushAction, m_toolMoveAction,
                                    m_toolEyedropperAction, m_toolSelectAction };
    for (int i = 0; i < actions.size(); ++i)
        actions[i]->setChecked(i == toolIndex);
}

void MainWindow::onColorPicked(const QColor &color)
{
    m_canvas->setBrushColor(color);
    statusBar()->showMessage(tr("画笔颜色: %1").arg(color.name()), 3000);
}

void MainWindow::newDocument()
{
    clearDocument();
    m_document = new Document(QSize(1200, 800), this);
    // 默认白色背景层(不进入撤销栈,属于"新建"本身)
    m_document->addLayer(new Layer(tr("Background"), m_document->size(), Qt::white));
    m_document->setActiveLayer(m_document->layerAt(0));
    m_canvas->setDocument(m_document);
    m_layersPanel->setDocument(m_document);
    setWindowTitle(tr("Compositor Qt - Untitled"));
    statusBar()->showMessage(tr("新建文档"), 3000);
}

void MainWindow::openDocument()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Image"), QString(),
        tr("Images (*.psd *.png *.jpg *.jpeg *.bmp);;PSD (*.psd);;PNG (*.png);;JPEG (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    // 读文件字节
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open Failed"), tr("无法打开文件: %1").arg(file.errorString()));
        return;
    }
    const QByteArray data = file.readAll();

    QImage image;
    bool ok = false;
    QString error;
    if (path.endsWith(".psd", Qt::CaseInsensitive)) {
        // PSD → 用 psd::Reader
        const auto r = psd::Reader::read(data);
        if (r.ok) {
            image = r.composite;
            ok = true;
        } else {
            error = r.error;
        }
    } else {
        // 其它格式 → QImage
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

    // 建立文档
    clearDocument();
    m_document = new Document(image.size(), this);
    auto *layer = new Layer(tr("Background"), image.size());
    layer->pixels() = image;
    m_document->addLayer(layer);
    m_document->setActiveLayer(layer);
    m_canvas->setDocument(m_document);
    m_layersPanel->setDocument(m_document);
    m_currentPath = path;
    setWindowTitle(tr("Compositor Qt - %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(tr("已打开 %1").arg(path), 3000);
}

void MainWindow::saveDocument()
{
    if (!m_document) {
        statusBar()->showMessage(tr("没有可保存的文档"), 3000);
        return;
    }
    QString path = m_currentPath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Image"), QString(),
            tr("PSD (*.psd);;PNG (*.png);;JPEG (*.jpg *.jpeg)"));
        if (path.isEmpty())
            return;
    }

    bool ok = false;
    QString error;
    if (path.endsWith(".psd", Qt::CaseInsensitive)) {
        const QByteArray bytes = psd::writePsd(*m_document, {}, &error);
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
        // PNG/JPEG/BMP 用 QImage 渲染合成图后保存
        PainterRenderer renderer;
        const QImage composite = renderer.render(*m_document);
        ok = composite.save(path);
        if (!ok) error = tr("无法保存图片");
    }

    if (ok) {
        m_currentPath = path;
        setWindowTitle(tr("Compositor Qt - %1").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(tr("已保存 %1").arg(path), 3000);
    } else {
        QMessageBox::warning(this, tr("Save Failed"), tr("保存失败: %1").arg(error));
    }
}

void MainWindow::clearDocument()
{
    m_undoStack.clear();
    m_canvas->setDocument(nullptr);
    m_layersPanel->setDocument(nullptr);
    delete m_document;
    m_document = nullptr;
    setWindowTitle(tr("Compositor Qt"));
}

void MainWindow::applyFilterToActiveLayer(const QString &name,
                                          const std::function<QImage(const QImage &)> &fn)
{
    if (!m_document || !m_document->activeLayer()) {
        statusBar()->showMessage(tr("没有活动图层"), 3000);
        return;
    }
    Layer *layer = m_document->activeLayer();
    const QImage before = layer->pixels();
    if (before.isNull())
        return;
    const QImage after = fn(before);
    if (after.isNull() || after == before)
        return;
    m_undoStack.push(new FilterCommand(layer, before, after, name));
    statusBar()->showMessage(tr("已应用 %1").arg(name), 3000);
}

void MainWindow::filterGaussianBlur()
{
    const auto p = filtui::promptGaussianBlur(this);
    if (!p)
        return;
    applyFilterToActiveLayer(tr("Gaussian Blur"), [p](const QImage &src) {
        return filt::gaussianBlur(src, p->radius);
    });
}

void MainWindow::filterMotionBlur()
{
    const auto p = filtui::promptMotionBlur(this);
    if (!p)
        return;
    applyFilterToActiveLayer(tr("Motion Blur"), [p](const QImage &src) {
        return filt::motionBlur(src, p->distance, p->angle);
    });
}

void MainWindow::filterAddNoise()
{
    const auto p = filtui::promptAddNoise(this);
    if (!p)
        return;
    applyFilterToActiveLayer(tr("Add Noise"), [p](const QImage &src) {
        return filt::addNoise(src, p->amount, p->gaussian, p->monochromatic, p->seed);
    });
}

void MainWindow::filterVignette()
{
    const auto p = filtui::promptVignette(this);
    if (!p)
        return;
    applyFilterToActiveLayer(tr("Vignette"), [p](const QImage &src) {
        return filt::vignette(src, p->amount, p->color, p->midpoint, p->roundness, p->feather);
    });
}

// ---------------------------------------------------------------------------
// 调色:弹对话框 → 应用 LUT → 撤销命令
// ---------------------------------------------------------------------------
void MainWindow::applyLutAdjustment(
    const QString &name, const std::function<std::optional<adjui::LutApplyResult>()> &prompt)
{
    if (!m_document) {
        statusBar()->showMessage(tr("没有活动文档"), 3000);
        return;
    }
    const auto res = prompt();
    if (!res)
        return; // 用户取消或恒等
    m_undoStack.push(new AdjustLutCommand(m_document, m_document->renderLut(),
                                          res->lut, name));
    statusBar()->showMessage(tr("已应用 %1").arg(name), 3000);
}

void MainWindow::adjLevels()
{
    applyLutAdjustment(tr("Levels"),
                       [this] { return adjui::promptAndBuildLevels(this); });
}

void MainWindow::adjBrightnessContrast()
{
    applyLutAdjustment(tr("Brightness/Contrast"),
                       [this] { return adjui::promptAndBuildBrightnessContrast(this); });
}

void MainWindow::adjExposure()
{
    applyLutAdjustment(tr("Exposure"),
                       [this] { return adjui::promptAndBuildExposure(this); });
}

void MainWindow::adjCurves()
{
    applyLutAdjustment(tr("Curves"),
                       [this] { return adjui::promptAndBuildCurves(this); });
}

void MainWindow::adjHueSaturation()
{
    // HSV 非 LUT 类,直接在文档所有图层像素上应用并压入"未命名"LUT 还原
    // MVP:把 HSV 应用为渲染 LUT(在最终合成图上),仍走 AdjustLutCommand。
    // 简单做法:把 HSV 转成 LUT 不现实(需要逐像素不同),所以用单次"图像替换"语义:
    // 把当前 renderLut 渲染结果写回图层,然后清空 LUT;但更简单的方案是
    // 弹窗 → 通过 PainterRenderer 拿一张已 LUT 化的图 → 对它应用 HSV → 写回活动图层。
    if (!m_document || !m_document->activeLayer()) {
        statusBar()->showMessage(tr("没有活动图层"), 3000);
        return;
    }
    const auto p = adjui::promptHueSaturation(this);
    if (!p)
        return;
    Layer *layer = m_document->activeLayer();
    const QImage before = layer->pixels();
    QImage after = before;
    adj::applyHsv(after, *p);
    if (after == before)
        return;
    m_undoStack.push(new FilterCommand(layer, before, after, tr("Hue/Saturation")));
    statusBar()->showMessage(tr("已应用 Hue/Saturation"), 3000);
}

void MainWindow::adjInvert()
{
    if (!m_document || !m_document->activeLayer()) {
        statusBar()->showMessage(tr("没有活动图层"), 3000);
        return;
    }
    Layer *layer = m_document->activeLayer();
    const QImage before = layer->pixels();
    QImage after = before;
    adj::applyInvert(after);
    if (after == before)
        return;
    m_undoStack.push(new FilterCommand(layer, before, after, tr("Invert")));
    statusBar()->showMessage(tr("已应用 Invert"), 3000);
}

void MainWindow::adjResetLut()
{
    if (!m_document)
        return;
    if (m_document->renderLut().isNull())
        return; // 没有调色,无需操作
    m_undoStack.push(new AdjustLutCommand(m_document, m_document->renderLut(),
                                          adj::identityLut(), tr("Reset Adjustments")));
    statusBar()->showMessage(tr("已重置调色"), 3000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    clearDocument();
    event->accept();
}
