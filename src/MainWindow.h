#pragma once

#include <QMainWindow>
#include <QUndoStack>

#include <functional>

class CanvasView;
class Document;
class LayersPanel;
class QColor;
class QLabel;
class QImage;

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
    void newDocument();
    void openDocument();
    void saveDocument();
    void clearDocument();
    void createDockPanels();
    void createToolBar();
    void syncToolActions(int toolIndex);
    void onColorPicked(const QColor &color);

    // 滤镜:在活动图层上执行变换并压入撤销栈(名称用于 Undo 菜单提示)
    void applyFilterToActiveLayer(const QString &name,
                                  const std::function<QImage(const QImage &)> &fn);
    void filterGaussianBlur();
    void filterMotionBlur();
    void filterAddNoise();
    void filterVignette();

    // 调色(非破坏性 LUT 调色,走 AdjustLutCommand)
    void applyLutAdjustment(const QString &name, const std::function<std::optional<adjui::LutApplyResult>()> &prompt);
    void adjLevels();
    void adjBrightnessContrast();
    void adjExposure();
    void adjCurves();
    void adjHueSaturation();
    void adjInvert();
    void adjResetLut();

    CanvasView *m_canvas = nullptr;
    LayersPanel *m_layersPanel = nullptr;
    Document *m_document = nullptr;
    QUndoStack m_undoStack;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QString m_currentPath; // 已打开/保存的文件路径

    // File
    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
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
