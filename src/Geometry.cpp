#include "Geometry.h"

#include <QPainter>
#include <cmath>

namespace geom {

QImage crop(const QImage &src, const QRect &rect)
{
    if (src.isNull() || rect.isEmpty())
        return {};
    // 与图像取交集,避免越界
    const QRect clipped = rect.intersected(QRect(0, 0, src.width(), src.height()));
    if (clipped.isEmpty())
        return {};
    return src.copy(clipped);
}

QImage resize(const QImage &src, int newWidth, int newHeight, Qt::TransformationMode mode)
{
    if (src.isNull() || newWidth <= 0 || newHeight <= 0)
        return {};
    return src.scaled(newWidth, newHeight, Qt::IgnoreAspectRatio, mode);
}

QImage rotate90(const QImage &src, int quarters)
{
    if (src.isNull())
        return {};
    // 归一化 quarters 到 0..3
    quarters = ((quarters % 4) + 4) % 4;
    switch (quarters) {
    case 0: return src;                           // 0°
    case 1: return src.transformed(QTransform().rotate(90));   // 90° CW
    case 2: return src.transformed(QTransform().rotate(180));  // 180°
    case 3: return src.transformed(QTransform().rotate(270));  // 270° CW = 90° CCW
    }
    return src;
}

QImage rotateArbitrary(const QImage &src, double angleDeg, const QColor &backgroundColor)
{
    if (src.isNull())
        return {};
    // 角度归一化到 [0, 360)
    angleDeg = std::fmod(angleDeg, 360.0);
    if (angleDeg < 0) angleDeg += 360.0;
    // 90° 整数倍走快速路径
    if (qFuzzyCompare(angleDeg, 0.0))   return src;
    if (qFuzzyCompare(angleDeg, 90.0))  return rotate90(src, 1);
    if (qFuzzyCompare(angleDeg, 180.0)) return rotate90(src, 2);
    if (qFuzzyCompare(angleDeg, 270.0)) return rotate90(src, 3);

    // 一般角度:用 QPainter rotate 围绕中心旋转,扩展画布以容纳整个旋转后图像
    const double rad = qDegreesToRadians(angleDeg);
    const double sn = std::abs(std::sin(rad));
    const double cs = std::abs(std::cos(rad));
    const int newW = int(std::ceil(src.width()  * cs + src.height() * sn));
    const int newH = int(std::ceil(src.width()  * sn + src.height() * cs));
    if (newW <= 0 || newH <= 0)
        return {};

    QImage out(newW, newH, src.format());
    out.fill(backgroundColor);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.translate(newW / 2.0, newH / 2.0);
    p.rotate(angleDeg);
    p.drawImage(QPointF(-src.width() / 2.0, -src.height() / 2.0), src);
    p.end();
    return out;
}

} // namespace geom
