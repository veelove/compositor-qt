#pragma once

#include <QMainWindow>

class CanvasView;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createStatusBar();

    CanvasView *m_canvas = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;

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
};
