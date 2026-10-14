#pragma once

#include <QColor>
#include <QImage>

namespace filt {

// 滤镜集合(MVP):全部纯 CPU,就地或返回新 QImage
// 算法与参数语义对齐上游 Compositor 的 Core Image / C 实现

// ============================== Gaussian Blur ==============================
// 参数:radius(像素,sigma = radius)
// 实现:可分离 1D 高斯卷积(横向+纵向),O(width*height*radius)
// 边界:边缘 clamp(超出取最近像素)
QImage gaussianBlur(const QImage &src, double radius);

// ============================== Motion Blur ==============================
// 参数:distance(像素,拖尾长度), angle(度,0=水平,逆时针,与上游 CIMotionBlur 一致)
// 实现:沿角度方向对像素采样并平均(Box 近似),方向取反得到"拖尾"
QImage motionBlur(const QImage &src, double distance, double angleDeg);

// ============================== Add Noise ==============================
// 参数:amount(0-100,强度), gaussian(高斯 vs 均匀), monochromatic(单通道 vs RGB 各自)
// 实现:独立随机种子(固定 seed 可复现)
QImage addNoise(const QImage &src, double amount, bool gaussian = true,
                bool monochromatic = false, quint32 seed = 0);

// ============================== Vignette ==============================
// 参数:amount(0-100,暗角强度), color(边缘颜色), midpoint(0-100),
//       roundness(-100..100), feather(0-100), highlights(0-100)
// 实现:按到中心的归一化距离做径向衰减,边缘趋向 color
QImage vignette(const QImage &src, double amount, const QColor &color = Qt::black,
                double midpoint = 50, double roundness = 100,
                double feather = 60, double highlights = 25);

} // namespace filt
