#pragma once

#include <QWidget>

class Document;
class Layer;
class QListWidget;
class QListWidgetItem;
class QUndoStack;
class QPushButton;

// 图层树面板:显示文档图层列表 + 增/删/上移/下移/显隐切换
class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(Document *document);
    void setUndoStack(QUndoStack *stack);

signals:
    void layerSelected(Layer *layer);

private slots:
    void refresh();
    void onItemChanged(QListWidgetItem *item);
    void onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void addLayer();
    void deleteLayer();
    void moveUp();
    void moveDown();
    void toggleVisibility();

private:
    Document *m_document = nullptr;
    QUndoStack *m_undoStack = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
    QPushButton *m_visBtn = nullptr;
    bool m_updating = false; // 防重入
};
