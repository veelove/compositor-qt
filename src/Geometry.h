#pragma once

#include <QImage>
#include <QRect>
#include <QtMath>

namespace geom {

// ============================== Crop ==============================
// 从图像切出指定矩形;QRect 自动裁剪到图像范围内。
// 若矩形在图像外,返回 null QImage。
QImage crop(const QImage &src, const QRect &rect);

// ============================== Resize ==============================
// 重采样到目标尺寸(保持原格式)。
// algorithm:Qt::SmoothTransformation(默认)用于缩小/放大,
//         Qt::FastTransformation 用于整数倍大
QImage resize(const QImage &src, int newWidth, int newHeight,
              Qt::TransformationMode mode = Qt::SmoothTransformation);

// ============================== Rotate 90° ==============================
// 顺时针旋转 90 度(quarters = 1/2/3) 或 180 度(quarters = 2)
// quarters 取 0 等同于 4,负数取模 4
QImage rotate90(const QImage &src, int quarters);

// ============================== Rotate 任意角度 ==============================
// 顺时针旋转 angleDeg 度,空白处填 backgroundColor
// 实现:用 QPainter::rotate,围绕图像中心旋转
QImage rotateArbitrary(const QImage &src, double angleDeg, const QColor &backgroundColor = Qt::transparent);

} // namespace geom
