#pragma once

#include "AdjustLut.h"

#include <QObject>
#include <QSize>
#include <QVector>

class Layer;

// 文档:一个画布尺寸 + 一组有序图层(索引越大越靠上)。
class Document : public QObject
{
    Q_OBJECT
public:
    explicit Document(const QSize &size, QObject *parent = nullptr);

    QSize size() const { return m_size; }
    void setSize(const QSize &size);

    int layerCount() const { return m_layers.size(); }
    const QVector<Layer *> &layers() const { return m_layers; }
    Layer *layerAt(int index) const;
    int indexOf(Layer *layer) const;

    // 加入后文档接管图层所有权;parent 通常传 document(命令撤销时会转移所有权)
    void addLayer(Layer *layer, int index = -1);
    // 从文档移除(不删除对象,所有权归还调用方)
    Layer *removeLayer(int index);
    void removeLayer(Layer *layer);

    Layer *activeLayer() const { return m_activeLayer; }
    void setActiveLayer(Layer *layer);

    // 渲染期全局调色 LUT(非破坏性,图层像素不变;空 = 无调色)
    const adj::AdjustLut &renderLut() const { return m_renderLut; }
    void setRenderLut(const adj::AdjustLut &lut);

signals:
    void contentChanged();    // 像素/显示变化,画布需重绘
    void structureChanged();  // 图层增删
    void activeLayerChanged();

private:
    QSize m_size;
    QVector<Layer *> m_layers;
    Layer *m_activeLayer = nullptr;
    adj::AdjustLut m_renderLut;
};
