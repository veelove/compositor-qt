#pragma once

#include <QImage>
#include <QPoint>
#include <QPolygonF>

// 视觉智能 — 对象选择(用户点选 → 主体分割 → 蒙版多边形)
//
// 设计:上游 Compositor 用 Apple Vision 的 VNInstanceSegmentationRequest
//      (生成"实例级"低分辨率掩码,按点击位置取对应实例,边缘处理得到 Path)。
//      macOS 独占。
// 跨平台替代方案:
//   方案 A:ONNX Runtime + U²-Net / SAM
//          模型效果对齐需要大量调参,体积大(50-300MB),CPU 推理慢
//   方案 B:OpenCV DNN + 中等量级抠图模型
//          可用,但效果仍弱于 Vision 的实例分割
//   方案 C:OpenCV GrabCut + 用户给矩形框
//          经典 Graph Cut,可作"矩形选取"近似方案,无需 ML
//   方案 D:reBG / SAM2(2024 学术前沿) — 当前不成熟
// MVP 选型(待 spike 后定):方案 A 跑 ONNX Runtime,预下载小模型(20MB 内),
//      GPU 加速走 Qt Rhi/OpenCL。
//
// 现状(MVP):仅 stub 返回清晰错误;
// 实际接入需:依赖 ONNX Runtime / OpenCV,集成模型文件,管理 GPU 内存
class VisionProvider
{
public:
    virtual ~VisionProvider() = default;
    // 在 image 上用 point 选前景,返回 8-bit alpha 蒙版(0=背景,255=前景)
    virtual bool selectObject(const QImage &image, const QPoint &point,
                              QImage &mask, QString *error) = 0;
};

// 占位 stub:所有实现返回清晰"未实现"错误
// 后续接入 ONNX Runtime 时,替换为真实实现并切换工厂
class StubVisionProvider : public VisionProvider
{
public:
    bool selectObject(const QImage &, const QPoint &, QImage &, QString *error) override
    {
        if (error)
            *error = QStringLiteral(
                "对象选择未实现(StubVisionProvider)。\n"
                "计划方案:ONNX Runtime + U²-Net(详见 VisionProvider.h)");
        return false;
    }
};
