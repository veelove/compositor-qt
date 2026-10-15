#pragma once

#include "AdjustLut.h"

#include <QObject>
#include <QSize>
#include <QUndoStack>
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

    // 渲染期高斯模糊(非破坏性,图层像素不变;radius<=0 不应用)
    double gaussianBlurRadius() const { return m_gaussianBlurRadius; }
    void setGaussianBlurRadius(double radius);

    // 渲染期暗角(非破坏性;amount<=0 不应用)
    double vignetteAmount() const { return m_vignette.amount; }
    double vignetteMidpoint() const { return m_vignette.midpoint; }
    double vignetteRoundness() const { return m_vignette.roundness; }
    double vignetteFeather() const { return m_vignette.feather; }
    void setVignette(double amount, double midpoint, double roundness, double feather);
    void clearVignette();

    // 渲染期动感模糊(非破坏性;distance<=0 不应用)
    double motionBlurDistance() const { return m_motionBlur.distance; }
    double motionBlurAngle() const { return m_motionBlur.angle; }
    void setMotionBlur(double distance, double angle);
    void clearMotionBlur();

    // 渲染期添加杂色(非破坏性;amount<=0 不应用)
    double noiseAmount() const { return m_noise.amount; }
    bool noiseGaussian() const { return m_noise.gaussian; }
    bool noiseMonochromatic() const { return m_noise.monochromatic; }
    quint32 noiseSeed() const { return m_noise.seed; }
    void setAddNoise(double amount, bool gaussian, bool monochromatic, quint32 seed);
    void clearAddNoise();

    // 每个 Document 独立的撤销栈
    QUndoStack *undoStack() { return &m_undoStack; }

    // 渲染期 HSV(非破坏性;amount=色相+饱和度+明度;identity 时 skip)
    const adj::HsvParams &hsv() const { return m_hsv; }
    void setHsv(const adj::HsvParams &p);
    void clearHsv();

    // 渲染期黑白(非破坏性;6 权重 + tint)
    const adj::BwParams &blackWhite() const { return m_bw; }
    void setBlackWhite(const adj::BwParams &p);
    void clearBlackWhite();

    // 渲染期色彩平衡(非破坏性;9 偏移 + preserveLuminosity)
    const adj::CbParams &colorBalance() const { return m_cb; }
    void setColorBalance(const adj::CbParams &p);
    void clearColorBalance();

signals:
    void contentChanged();    // 像素/显示变化,画布需重绘
    void structureChanged();  // 图层增删
    void activeLayerChanged();

private:
    QSize m_size;
    QVector<Layer *> m_layers;
    Layer *m_activeLayer = nullptr;
    adj::AdjustLut m_renderLut;
    double m_gaussianBlurRadius = 0.0;
    struct VignetteParams { double amount = 0, midpoint = 50, roundness = 100, feather = 60; } m_vignette;
    struct MotionBlurParams { double distance = 0, angle = 0; } m_motionBlur;
    struct NoiseParams { double amount = 0; bool gaussian = true; bool monochromatic = false; quint32 seed = 0; } m_noise;
    adj::HsvParams m_hsv;
    adj::BwParams m_bw;
    adj::CbParams m_cb;
    QUndoStack m_undoStack;
};
