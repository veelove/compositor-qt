#include "Filters.h"

#include <QtGlobal>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace filt {

namespace {

inline uint8_t clampByte(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }

// 生成 1D 高斯核(半径为 sigma 的 3σ 窗口)
QVector<double> gaussianKernel(double sigma)
{
    if (sigma <= 0)
        return {1.0};
    const int radius = int(std::ceil(sigma * 3.0));
    QVector<double> k(radius * 2 + 1);
    double sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        const double v = std::exp(-(i * i) / (2 * sigma * sigma));
        k[i + radius] = v;
        sum += v;
    }
    for (double &v : k)
        v /= sum;
    return k;
}

// 水平模糊(单通道平面)
QVector<uint8_t> blur1D(const QVector<uint8_t> &src, int w, int h, const QVector<double> &kernel, bool horizontal)
{
    QVector<uint8_t> out(src.size());
    const int kRadius = kernel.size() / 2;
    const int len = horizontal ? w : h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double acc = 0;
            for (int k = -kRadius; k <= kRadius; ++k) {
                const int pos = horizontal ? (x + k) : (y + k);
                // clamp 边缘
                const int clamped = std::max(0, std::min(len - 1, pos));
                const int idx = horizontal ? (y * w + clamped) : (clamped * w + x);
                acc += src[idx] * kernel[k + kRadius];
            }
            out[y * w + x] = uint8_t(acc + 0.5);
        }
    }
    return out;
}

// 简单确定性随机(线性同余),便于固定 seed 复现
struct Lcg
{
    quint32 state;
    explicit Lcg(quint32 seed) : state(seed ? seed : 0x12345678u) {}
    quint32 next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    // [-1, 1]
    double unit()
    {
        return (next() / 4294967295.0) * 2.0 - 1.0;
    }
};

} // namespace

// ============================== Gaussian Blur ==============================
QImage gaussianBlur(const QImage &src, double radius)
{
    if (src.isNull() || radius <= 0)
        return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    const QVector<double> kernel = gaussianKernel(radius);

    // 分离到 R/G/B/A 四平面(便于 1D 卷积)
    QVector<uint8_t> R(w * h), G(w * h), B(w * h), A(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const QRgb px = img.pixel(x, y);
            const int i = y * w + x;
            R[i] = qRed(px); G[i] = qGreen(px); B[i] = qBlue(px); A[i] = qAlpha(px);
        }

    R = blur1D(R, w, h, kernel, true);  R = blur1D(R, w, h, kernel, false);
    G = blur1D(G, w, h, kernel, true);  G = blur1D(G, w, h, kernel, false);
    B = blur1D(B, w, h, kernel, true);  B = blur1D(B, w, h, kernel, false);
    A = blur1D(A, w, h, kernel, true);  A = blur1D(A, w, h, kernel, false);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            img.setPixel(x, y, qRgba(R[i], G[i], B[i], A[i]));
        }
    return img;
}

// ============================== Motion Blur ==============================
QImage motionBlur(const QImage &src, double distance, double angleDeg)
{
    if (src.isNull() || distance <= 0)
        return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    // 采样方向(上游 CIMotionBlur:angle 逆时针,Core Image y 向上 → 我们屏幕 y 向下需反转)
    // Photoshop 的角度是顺时针,Core Image 逆时针,我们按"屏幕角度"(顺时针)解释输入,
    // 这样 UI 上滑块角度 = 视觉拖尾角度
    const double rad = qDegreesToRadians(angleDeg);
    const double dx = std::cos(rad);
    const double dy = std::sin(rad);
    const int samples = int(std::ceil(distance)) + 1;

    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            for (int s = -samples / 2; s <= samples / 2; ++s) {
                const double t = (s / double(samples)) * distance;
                const int sx = x + int(std::round(t * dx));
                const int sy = y + int(std::round(t * dy));
                if (sx < 0 || sx >= w || sy < 0 || sy >= h)
                    continue;
                const QRgb px = img.pixel(sx, sy);
                r += qRed(px); g += qGreen(px); b += qBlue(px); a += qAlpha(px);
                ++count;
            }
            if (count == 0) {
                out.setPixel(x, y, img.pixel(x, y));
            } else {
                out.setPixel(x, y, qRgba(clampByte(int(r / count)), clampByte(int(g / count)),
                                         clampByte(int(b / count)), clampByte(int(a / count))));
            }
        }
    }
    return out;
}

// ============================== Add Noise ==============================
QImage addNoise(const QImage &src, double amount, bool gaussian, bool monochromatic, quint32 seed)
{
    if (src.isNull() || amount <= 0)
        return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    Lcg rng(seed);
    const double strength = amount / 100.0 * 255.0 / 2.0; // 幅度归一化

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const QRgb px = img.pixel(x, y);
            int r = qRed(px), g = qGreen(px), b = qBlue(px);
            // 高斯:两个均匀叠加近似(Box-Muller 简化)
            double n = gaussian ? (rng.unit() + rng.unit()) / 2.0 : rng.unit();
            int delta = int(std::round(n * strength));
            if (monochromatic) {
                r = clampByte(r + delta); g = clampByte(g + delta); b = clampByte(b + delta);
            } else {
                const double n2 = gaussian ? (rng.unit() + rng.unit()) / 2.0 : rng.unit();
                const double n3 = gaussian ? (rng.unit() + rng.unit()) / 2.0 : rng.unit();
                r = clampByte(r + int(std::round(n * strength)));
                g = clampByte(g + int(std::round(n2 * strength)));
                b = clampByte(b + int(std::round(n3 * strength)));
            }
            img.setPixel(x, y, qRgba(r, g, b, qAlpha(px)));
        }
    }
    return img;
}

// ============================== Vignette ==============================
QImage vignette(const QImage &src, double amount, const QColor &color,
                double midpoint, double roundness, double feather, double highlights)
{
    Q_UNUSED(highlights); // 保留参数;当前实现未用高光保护
    if (src.isNull() || amount <= 0)
        return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    const double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
    const double maxDist = std::sqrt(cx * cx + cy * cy);
    const double amountN = std::clamp(amount, 0.0, 100.0) / 100.0;
    const double mid = std::clamp(midpoint, 0.0, 100.0) / 100.0;        // 0..1
    const double round = std::clamp(roundness, -100.0, 100.0) / 100.0; // -1..1
    const double feat = std::clamp(feather, 0.0, 100.0) / 100.0;        // 0..1

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double ndx = (x - cx) / cx;   // -1..1
            const double ndy = (y - cy) / cy;
            // 圆度:round>0 更圆(横向权重),round<0 更椭(纵向权重)
            const double rx = ndx * (1.0 + round * 0.3);
            const double ry = ndy * (1.0 - round * 0.3);
            double dist = std::sqrt(rx * rx + ry * ry) / std::sqrt(2.0); // 0..1(角到角)
            // 衰减曲线:先到 mid,再过 feather 平滑
            // 简单版本:dist 超过 mid 后线性加强
            double falloff = 0;
            if (dist > mid) {
                const double t = (dist - mid) / std::max(0.01, (1.0 - mid));
                falloff = t * t; // 二次缓入
                // feather 平滑:软化 falloff 起始
                if (feat > 0)
                    falloff = std::pow(falloff, 0.5 + feat * 0.5);
            }
            const double mix = falloff * amountN;

            const QRgb px = img.pixel(x, y);
            const int r = qRed(px), g = qGreen(px), b = qBlue(px), a = qAlpha(px);
            // 向 vignette 颜色混合(alpha 也混)
            const int nr = clampByte(int(r * (1 - mix) + color.red()   * mix));
            const int ng = clampByte(int(g * (1 - mix) + color.green() * mix));
            const int nb = clampByte(int(b * (1 - mix) + color.blue()  * mix));
            img.setPixel(x, y, qRgba(nr, ng, nb, a));
        }
    }
    return img;
}

} // namespace filt
