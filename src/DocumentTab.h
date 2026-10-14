#pragma once

#include <QString>
#include <QWidget>

class CanvasView;
class Document;
class QVBoxLayout;

// 一个"文档 tab"：CanvasView + Document + 文件路径的容器
// 拥有 Document 所有权（delete on destroy）
class DocumentTab : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentTab(Document *document, QWidget *parent = nullptr);
    ~DocumentTab() override;

    Document *document() const { return m_document; }
    CanvasView *canvas() const { return m_canvas; }
    QString path() const { return m_path; }
    void setPath(const QString &path) { m_path = path; }
    bool isModified() const; // 任意撤销栈非空 → 视为已修改

signals:
    void titleChanged(const QString &title);

private slots:
    void onDocChanged();

private:
    QString displayTitle() const;

    Document *m_document = nullptr; // owned
    CanvasView *m_canvas = nullptr;
    QString m_path;
};
