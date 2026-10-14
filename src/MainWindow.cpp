#include "MainWindow.h"

#include "CanvasView.h"
#include "Document.h"
#include "Layer.h"

#include <QAction>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

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

    connect(m_canvas, &CanvasView::zoomChanged, this, [this](double zoom) {
        m_zoomLabel->setText(tr("%1%").arg(qRound(zoom * 100)));
    });
    connect(m_canvas, &CanvasView::cursorInfoChanged, this, [this](const QString &text) {
        m_cursorLabel->setText(text);
    });

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
    connect(m_openAction, &QAction::triggered, this, [] { /* TODO: 打开文件 */ });

    m_saveAction = new QAction(tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(false);

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

void MainWindow::newDocument()
{
    clearDocument();
    m_document = new Document(QSize(1200, 800), this);
    // 默认白色背景层(不进入撤销栈,属于"新建"本身)
    m_document->addLayer(new Layer(tr("Background"), m_document->size(), Qt::white));
    m_document->setActiveLayer(m_document->layerAt(0));
    m_canvas->setDocument(m_document);
    setWindowTitle(tr("Compositor Qt - Untitled"));
    statusBar()->showMessage(tr("新建文档"), 3000);
}

void MainWindow::clearDocument()
{
    m_undoStack.clear();
    m_canvas->setDocument(nullptr);
    delete m_document;
    m_document = nullptr;
    setWindowTitle(tr("Compositor Qt"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    clearDocument();
    event->accept();
}
