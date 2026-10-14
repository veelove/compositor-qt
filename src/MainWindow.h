#pragma once

#include <QMainWindow>
#include <QUndoStack>

class CanvasView;
class Document;
class QLabel;

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

    CanvasView *m_canvas = nullptr;
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
};
