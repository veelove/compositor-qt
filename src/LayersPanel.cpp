#include "LayersPanel.h"

#include "Document.h"
#include "Layer.h"
#include "UndoCommands.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QUndoStack>
#include <QVBoxLayout>

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::NoDragDrop);
    // 允许双击改图层名
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    root->addWidget(m_list, /*stretch*/ 1);

    auto *btns = new QHBoxLayout;
    m_addBtn  = new QPushButton(tr("＋"), this);
    m_delBtn  = new QPushButton(tr("－"), this);
    m_upBtn   = new QPushButton(tr("↑"), this);
    m_downBtn = new QPushButton(tr("↓"), this);
    m_visBtn  = new QPushButton(tr("◉"), this);
    for (auto *b : {m_addBtn, m_delBtn, m_upBtn, m_downBtn, m_visBtn}) {
        b->setFixedWidth(28);
        b->setEnabled(false);
        btns->addWidget(b);
    }
    root->addLayout(btns);

    connect(m_list, &QListWidget::itemChanged, this, &LayersPanel::onItemChanged);
    connect(m_list, &QListWidget::currentItemChanged, this, &LayersPanel::onCurrentItemChanged);

    connect(m_addBtn, &QPushButton::clicked, this, &LayersPanel::addLayer);
    connect(m_delBtn, &QPushButton::clicked, this, &LayersPanel::deleteLayer);
    connect(m_upBtn,  &QPushButton::clicked, this, &LayersPanel::moveUp);
    connect(m_downBtn,&QPushButton::clicked, this, &LayersPanel::moveDown);
    connect(m_visBtn, &QPushButton::clicked, this, &LayersPanel::toggleVisibility);
}

void LayersPanel::setDocument(Document *document)
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document) {
        connect(m_document, &Document::structureChanged, this, &LayersPanel::refresh);
        connect(m_document, &Document::activeLayerChanged, this, &LayersPanel::refresh);
        connect(m_document, &Document::contentChanged, this, &LayersPanel::refresh);
    }
    refresh();
}

void LayersPanel::setUndoStack(QUndoStack *stack)
{
    m_undoStack = stack;
}

void LayersPanel::refresh()
{
    m_updating = true;
    m_list->clear();
    if (!m_document) {
        m_updating = false;
        for (auto *b : {m_addBtn, m_delBtn, m_upBtn, m_downBtn, m_visBtn})
            b->setEnabled(false);
        return;
    }
    // 从底到顶显示(列表顶部 = 画布最上层)
    const auto &layers = m_document->layers();
    for (int i = layers.size() - 1; i >= 0; --i) {
        Layer *layer = layers.at(i);
        auto *item = new QListWidgetItem(
            (layer->isVisible() ? QStringLiteral("👁 ") : QStringLiteral("⛔ ")) + layer->name(),
            m_list);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(layer)));
        if (layer == m_document->activeLayer())
            m_list->setCurrentItem(item);
    }
    m_addBtn->setEnabled(true);
    m_delBtn->setEnabled(true);
    m_upBtn->setEnabled(true);
    m_downBtn->setEnabled(true);
    m_visBtn->setEnabled(true);
    m_updating = false;
}

void LayersPanel::onItemChanged(QListWidgetItem *item)
{
    if (m_updating || !m_document || !item)
        return;
    Layer *layer = reinterpret_cast<Layer *>(item->data(Qt::UserRole).value<quintptr>());
    if (!layer || !m_document->layers().contains(layer))
        return;
    const QString text = item->text();
    // 去掉眼睛前缀再取名字
    QString name = text;
    if (name.startsWith(QStringLiteral("👁 "))) name = name.mid(2);
    else if (name.startsWith(QStringLiteral("⛔ "))) name = name.mid(2);
    layer->setName(name.isEmpty() ? tr("Layer") : name);
    // 重新显示(避免前缀丢失)
    refresh();
}

void LayersPanel::onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *)
{
    if (m_updating || !m_document || !current)
        return;
    Layer *layer = reinterpret_cast<Layer *>(current->data(Qt::UserRole).value<quintptr>());
    if (layer && m_document->layers().contains(layer)) {
        m_document->setActiveLayer(layer);
        emit layerSelected(layer);
    }
}

void LayersPanel::addLayer()
{
    if (!m_document || !m_undoStack)
        return;
    auto *layer = new Layer(tr("Layer %1").arg(m_document->layerCount() + 1), m_document->size(), Qt::transparent);
    m_undoStack->push(new AddLayerCommand(m_document, layer));
}

void LayersPanel::deleteLayer()
{
    if (!m_document || !m_undoStack)
        return;
    const int idx = m_document->indexOf(m_document->activeLayer());
    if (idx < 0)
        return;
    if (m_document->layerCount() <= 1)
        return; // 保留至少一层
    m_undoStack->push(new RemoveLayerCommand(m_document, idx));
}

void LayersPanel::moveUp()
{
    if (!m_document || !m_undoStack)
        return;
    Layer *layer = m_document->activeLayer();
    const int idx = m_document->indexOf(layer);
    if (idx < 0 || idx >= m_document->layerCount() - 1)
        return;
    // 用 Remove + Add 命令实现(可撤销)
    auto *cmd = new QUndoCommand(tr("Reorder Layer"));
    new RemoveLayerCommand(m_document, idx, cmd);
    new AddLayerCommand(m_document, layer, idx + 1, cmd);
    m_undoStack->push(cmd);
}

void LayersPanel::moveDown()
{
    if (!m_document || !m_undoStack)
        return;
    Layer *layer = m_document->activeLayer();
    const int idx = m_document->indexOf(layer);
    if (idx <= 0)
        return;
    auto *cmd = new QUndoCommand(tr("Reorder Layer"));
    new RemoveLayerCommand(m_document, idx, cmd);
    new AddLayerCommand(m_document, layer, idx - 1, cmd);
    m_undoStack->push(cmd);
}

void LayersPanel::toggleVisibility()
{
    if (!m_document)
        return;
    Layer *layer = m_document->activeLayer();
    if (!layer)
        return;
    layer->setVisible(!layer->isVisible());
    refresh();
}
