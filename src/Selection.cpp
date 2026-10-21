#include "Selection.h"

#include <QPainter>

namespace sel {

QImage maskFromRect(const QRect &rect, const QSize &imageSize)
{
    QImage mask(imageSize, QImage::Format_Grayscale8);
    mask.fill(0);
    const QRect r = rect.intersected(QRect(QPoint(0, 0), imageSize));
    if (r.isEmpty())
        return mask;
    // 直接填像素而不是用 QPainter,避免 Grayscale8 上 QPainter 的 alpha 混合开销
    for (int y = r.top(); y <= r.bottom(); ++y) {
        uchar *row = mask.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x)
            row[x] = 255;
    }
    return mask;
}

int coverage(const QImage &mask)
{
    if (mask.isNull())
        return 0;
    // 用 QImage 的 bits 累加;Grayscale8 每像素 1 字节,stride 可能 > width(行对齐)
    long long total = 0;
    const int h = mask.height();
    for (int y = 0; y < h; ++y) {
        const uchar *row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
            total += (row[x] > 0) ? 1 : 0;
    }
    return int(total);
}

QImage resampleMask(const QImage &mask, const QSize &targetSize)
{
    if (mask.isNull() || targetSize.isEmpty())
        return {};
    if (mask.size() == targetSize)
        return mask;
    return mask.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

QImage blendByMask(const QImage &base, const QImage &processed, const QImage &mask)
{
    if (mask.isNull() || base.isNull())
        return base;
    if (base.size() != processed.size())
        return processed; // 尺寸不一致无法按像素混合,退化为直接用结果

    const QImage m = resampleMask(mask, base.size());
    if (m.isNull())
        return base;

    QImage out = base.convertToFormat(QImage::Format_ARGB32);
    const QImage p = processed.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        const QRgb *src = reinterpret_cast<const QRgb *>(p.constScanLine(y));
        const uchar *a = m.constScanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            const int alpha = a[x];
            if (alpha == 0)
                continue;
            if (alpha == 255) {
                dst[x] = src[x];
                continue;
            }
            // 线性插值(逐通道);dst/src 都是 ARGB32(非预乘),alpha 通道同样插值
            const QRgb d = dst[x];
            const QRgb s = src[x];
            const int da = qAlpha(d), sa = qAlpha(s);
            const int r = qRed(d)   + ((qRed(s)   - qRed(d))   * alpha) / 255;
            const int g = qGreen(d) + ((qGreen(s) - qGreen(d)) * alpha) / 255;
            const int b = qBlue(d)  + ((qBlue(s)  - qBlue(d))  * alpha) / 255;
            const int aa = da + ((sa - da) * alpha) / 255;
            dst[x] = qRgba(r, g, b, aa);
        }
    }
    return out;
}

QImage clearByMask(const QImage &src, const QRect &rect, const QImage &mask)
{
    if (src.isNull())
        return {};

    QImage out = src.convertToFormat(QImage::Format_ARGB32);
    if (mask.isNull()) {
        // 无 mask:退化为矩形清除(向后兼容 RectSelect)
        const QRect r = rect.intersected(out.rect());
        if (r.isEmpty())
            return {};
        for (int y = r.top(); y <= r.bottom(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = r.left(); x <= r.right(); ++x)
                row[x] = qRgba(0, 0, 0, 0);
        }
        return out;
    }

    const QImage m = resampleMask(mask, out.size());
    if (m.isNull())
        return {};

    bool touched = false;
    for (int y = 0; y < out.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        const uchar *a = m.constScanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            if (a[x] == 0)
                continue;
            row[x] = qRgba(0, 0, 0, 0);
            touched = true;
        }
    }
    return touched ? out : QImage();
}

QImage cropToMask(const QImage &src, const QRect &rect, const QImage &mask)
{
    if (src.isNull())
        return {};
    QRect r = rect;
    if (mask.isNull()) {
        r = rect.intersected(src.rect());
        if (r.isEmpty())
            return {};
        return src.copy(r);
    }
    // 有 mask:以 mask 的非零区域为准(mask bbox 优先,其次 rect)
    const QImage m = resampleMask(mask, src.size());
    if (m.isNull())
        return {};

    int minX = src.width(), minY = src.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < m.height(); ++y) {
        const uchar *row = m.constScanLine(y);
        for (int x = 0; x < m.width(); ++x) {
            if (row[x] > 0) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0)
        return {};
    const QRect maskRect(QPoint(minX, minY), QPoint(maxX, maxY));
    r = maskRect.isEmpty() ? QRect() : maskRect;
    if (rect.isValid() && !rect.isEmpty())
        r = r.intersected(rect);
    r = r.intersected(src.rect());
    if (r.isEmpty())
        return {};
    return src.copy(r);
}

} // namespace sel