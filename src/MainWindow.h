#pragma once

#include <QMainWindow>

#include <functional>

class CanvasView;
class Document;
class DocumentTab;
class LayersPanel;
class QColor;
class QLabel;
class QImage;
class QTabWidget;

namespace adjui { struct LutApplyResult; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createStatusBar();
    void createToolBar();

    // Tab 操作
    DocumentTab *createTab(Document *document, const QString &path = {});
    DocumentTab *currentTab() const;
    Document *currentDocument() const;
    CanvasView *currentCanvas() const;
    void setCurrentTab(DocumentTab *tab);
    void updateTitleFromTab(DocumentTab *tab);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void closeTab(DocumentTab *tab);
    void maybeCloseAllTabs(); // closeEvent 用

    void fileNew();
    void fileOpen();
    void fileSave();
    void fileClose();

    // 滤镜:在活动图层上执行变换并压入撤销栈
    void applyFilterToActiveLayer(const QString &name,
                                  const std::function<QImage(const QImage &)> &fn);
    void filterGaussianBlur();
    void filterMotionBlur();
    void filterAddNoise();
    void filterVignette();

    // 调色(非破坏性 LUT 调色)
    void applyLutAdjustment(const QString &name,
                            const std::function<std::optional<adjui::LutApplyResult>()> &prompt);
    void adjLevels();
    void adjBrightnessContrast();
    void adjExposure();
    void adjCurves();
    void adjHueSaturation();
    void adjInvert();
    void adjResetLut();

    void onColorPicked(const QColor &color);
    void onUndoStackChanged();

    QTabWidget *m_tabs = nullptr;
    LayersPanel *m_layersPanel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;

    // File
    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_quitAction = nullptr;
    // Edit
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    // View
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_zoomFitAction = nullptr;
    QAction *m_zoomActualAction = nullptr;
    // Filter
    QAction *m_filterGaussianAction = nullptr;
    QAction *m_filterMotionAction = nullptr;
    QAction *m_filterNoiseAction = nullptr;
    QAction *m_filterVignetteAction = nullptr;
    // Toolbar
    QAction *m_toolBrushAction = nullptr;
    QAction *m_toolMoveAction = nullptr;
    QAction *m_toolEyedropperAction = nullptr;
    QAction *m_toolSelectAction = nullptr;
    // Adjustments(Image 菜单)
    QAction *m_adjLevelsAction = nullptr;
    QAction *m_adjBcAction = nullptr;
    QAction *m_adjExposureAction = nullptr;
    QAction *m_adjCurvesAction = nullptr;
    QAction *m_adjHsvAction = nullptr;
    QAction *m_adjInvertAction = nullptr;
    QAction *m_adjResetAction = nullptr;
};
