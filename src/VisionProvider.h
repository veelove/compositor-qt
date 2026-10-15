#pragma once

#include <QImage>
#include <QPoint>
#include <QPolygonF>

// 视觉智能 — 对象选择(用户点选 → 主体分割 → alpha 蒙版)
//
// 设计:上游 Compositor 用 Apple Vision 的 VNInstanceSegmentationRequest
//      macOS 独占。
// 跨平台替代方案(详见评估报告):
//   方案 A:ONNX Runtime + U²-Net / SAM(模型大,CPU 推理慢,GPU 需 OpenCL/CUDA)
//   方案 B:OpenCV DNN + 中等量级模型
//   方案 C:OpenCV GrabCut + 用户给矩形框(经典 Graph Cut,无需 ML,效果近似)
//   方案 D:reBG / SAM2(2024 学术前沿) — 当前不成熟
// MVP 选型:方案 A 的进程级封装 —— Python 桥脚本(tools/vision_tool.py,内含 onnxruntime + U²-Net),
//          stdin=PNG 字节流 + 命令行 x y,stdout 输出 PNG 灰度 mask。
//          优点:零编译依赖(Qt 不需链 onnxruntime),跨平台一致;
//          缺点:每张图启动进程,模型权重需用户预下载(env COMPOSITOR_U2NET_MODEL)。
// 脚本内嵌三层降级,确保 Qt 端总能拿到 mask:
//   1) onnxruntime + U^2-Net(u2netp 轻量) 显著性检测 + 动态阈值
//   2) OpenCV GrabCut(矩形 init)
//   3) 点击点颜色洪水漫灌(Pillow/自实现 PNG 解码)
// 若 vision_tool.py 完全找不到 → StubVisionProvider 给出清晰安装指引。
class VisionProvider
{
public:
    virtual ~VisionProvider() = default;
    // 探测系统 vision_tool.py(返回空 = 不可用)
    static QString scriptPath();
    // 工厂:有 vision_tool.py → ProcessVisionProvider,否则 → StubVisionProvider
    static VisionProvider *create();
    // 在 image 上用 point 选前景,返回 8-bit alpha 蒙版(0=背景,255=前景)
    virtual bool selectObject(const QImage &image, const QPoint &point,
                              QImage &mask, QString *error) = 0;
};

// 进程级实现:调 vision_tool.py 输入图像+点 → stdout PNG mask
class ProcessVisionProvider : public VisionProvider
{
public:
    bool selectObject(const QImage &image, const QPoint &point,
                      QImage &mask, QString *error) override;
};

// 占位 stub:所有实现返回清晰"未实现"错误
class StubVisionProvider : public VisionProvider
{
public:
    bool selectObject(const QImage &, const QPoint &, QImage &, QString *error) override;
};
