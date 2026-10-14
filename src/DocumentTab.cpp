#include "DocumentTab.h"

#include "CanvasView.h"
#include "Document.h"

#include <QUrl>
#include <QVBoxLayout>

DocumentTab::DocumentTab(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    m_canvas = new CanvasView(this);
    m_canvas->setDocument(m_document);
    // 画布与文档同源:CanvasView 的 undoStack 指向 Document 的栈
    m_canvas->setUndoStack(m_document->undoStack());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_canvas);

    connect(m_document, &Document::contentChanged, this, [this] { emit titleChanged(displayTitle()); });
    connect(m_document->undoStack(), &QUndoStack::cleanChanged, this, [this] { emit titleChanged(displayTitle()); });
    connect(m_document->undoStack(), &QUndoStack::indexChanged, this, [this] { emit titleChanged(displayTitle()); });
}

DocumentTab::~DocumentTab()
{
    // 解绑,避免 setDocument(nullptr) 在 dtor 中触发额外信号
    m_canvas->setDocument(nullptr);
    delete m_document;
}

bool DocumentTab::isModified() const
{
    return m_document && !m_document->undoStack()->isClean();
}

QString DocumentTab::displayTitle() const
{
    const QString base = m_path.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QUrl::fromLocalFile(m_path).fileName();
    return isModified() ? base + QStringLiteral(" *") : base;
}

void DocumentTab::onDocChanged() { emit titleChanged(displayTitle()); }
