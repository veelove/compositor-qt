#include "Adjustments.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace adj {

namespace {

inline double clampD(double v, double lo, double hi)
{
    return std::min(hi, std::max(lo, v));
}

inline uint8_t clampByte(int v)
{
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

// 一次 HSL/RGB 转换(标准 CSS L 公式,Photoshop Hue/Saturation 与之等价)
inline void rgbToHsl(uint8_t r, uint8_t g, uint8_t b, double &h, double &s, double &l)
{
    const double rd = r / 255.0;
    const double gd = g / 255.0;
    const double bd = b / 255.0;
    const double mx = std::max({rd, gd, bd});
    const double mn = std::min({rd, gd, bd});
    const double d  = mx - mn;
    l = (mx + mn) / 2;
    if (d == 0) { h = 0; s = 0; return; }
    s = (l > 0.5) ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == rd)      h = ((gd - bd) / d) + (gd < bd ? 6 : 0);
    else if (mx == gd) h = ((bd - rd) / d) + 2;
    else               h = ((rd - gd) / d) + 4;
    h /= 6; // 0..1
}

inline void hslToRgb(double h, double s, double l, uint8_t &r, uint8_t &g, uint8_t &b)
{
    if (s == 0) {
        const uint8_t v = clampByte(static_cast<int>(l * 255));
        r = g = b = v;
        return;
    }
    auto hue2rgb = [](double p, double q, double t) -> double {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0 / 6) return p + (q - p) * 6 * t;
        if (t < 0.5)     return q;
        if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
        return p;
    };
    const double q = (l < 0.5) ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    r = clampByte(static_cast<int>(hue2rgb(p, q, h + 1.0 / 3) * 255));
    g = clampByte(static_cast<int>(hue2rgb(p, q, h)         * 255));
    b = clampByte(static_cast<int>(hue2rgb(p, q, h - 1.0 / 3) * 255));
}

inline uint8_t applyLightness(uint8_t v, double lightPct)
{
    // lightPct ∈ [-100, +100]
    // 正:朝白色推(out = l + (1-l)*pct/100)
    // 负:朝黑色推(out = l + l*pct/100 = l*(1 + pct/100))
    const double l = v / 255.0;
    const double pct = lightPct / 100.0;
    const double out = l + (pct >= 0 ? (1.0 - l) * pct : l * pct);
    return clampByte(static_cast<int>(out * 255));
}

} // namespace

// ============================== LevelRange ==============================
bool LevelRange::identity() const
{
    return black == 0 && white == 255 && gamma == 1.0
        && outputBlack == 0 && outputWhite == 255;
}

LevelRange LevelRange::normalized() const
{
    LevelRange r;
    r.black      = std::isfinite(black)   ? clampD(black, 0, 254)               : 0;
    r.white      = std::isfinite(white)   ? clampD(white, r.black + 1, 255)      : 255;
    r.gamma      = std::isfinite(gamma)   ? clampD(gamma, 0.1, 9.99)            : 1.0;
    r.outputBlack = std::isfinite(outputBlack) ? clampD(outputBlack, 0, 255)    : 0;
    r.outputWhite = std::isfinite(outputWhite) ? clampD(outputWhite, r.outputBlack, 255) : 255;
    return r;
}

uint8_t applyLevelRange(uint8_t v, const LevelRange &r)
{
    LevelRange n = r.normalized();
    const double in = clampD((v - n.black) / (n.white - n.black), 0.0, 1.0);
    const double corrected = std::pow(in, 1.0 / n.gamma);
    const double out = (n.outputBlack + corrected * (n.outputWhite - n.outputBlack)) / 255.0;
    return clampByte(static_cast<int>(out * 255));
}

void applyLevels(QImage &dest, const LevelsParams &p)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    // 两个 range 都是 identity → 跳过
    if (p.rgbRange.identity() && p.range.identity())
        return;

    // channel=Rgb → 每像素先过 channel.range,再过 rgbRange
    // channel=R/G/B → 该通道先过 channel.range,再过 rgbRange;其他通道只过 rgbRange
    const LevelRange perChannel = p.range.normalized();
    const LevelRange rgb = p.rgbRange.normalized();

    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a  = qAlpha(px);
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);
            if (p.channel == LevelsParams::Channel::Red
                || p.channel == LevelsParams::Channel::Rgb) {
                r = applyLevelRange(static_cast<uint8_t>(r), perChannel);
            }
            if (p.channel == LevelsParams::Channel::Green
                || p.channel == LevelsParams::Channel::Rgb) {
                g = applyLevelRange(static_cast<uint8_t>(g), perChannel);
            }
            if (p.channel == LevelsParams::Channel::Blue
                || p.channel == LevelsParams::Channel::Rgb) {
                b = applyLevelRange(static_cast<uint8_t>(b), perChannel);
            }
            r = applyLevelRange(static_cast<uint8_t>(r), rgb);
            g = applyLevelRange(static_cast<uint8_t>(g), rgb);
            b = applyLevelRange(static_cast<uint8_t>(b), rgb);
            dest.setPixel(x, y, qRgba(r, g, b, a));
        }
    }
}

// ============================== Hue/Saturation ==============================
void applyHsv(QImage &dest, const HsvParams &p)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    if (p.hue == 0 && p.saturation == 0 && p.lightness == 0)
        return;

    const double hueShift  = clampD(p.hue, -180, 180) / 360.0;
    const double satMul    = 1.0 + clampD(p.saturation, -100, 100) / 100.0;
    const double lightPct  = clampD(p.lightness, -100, 100);

    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a  = qAlpha(px);
            uint8_t r = qRed(px);
            uint8_t g = qGreen(px);
            uint8_t b = qBlue(px);

            if (p.colorize) {
                // 保留亮度、色相变为参数,饱和度固定最大
                double h, s, l;
                rgbToHsl(r, g, b, h, s, l);
                h = std::fmod(h + hueShift + 1.0, 1.0);
                s = 0.5; // 适度饱和(完全饱和会很扎眼)
                hslToRgb(h, s, l, r, g, b);
            } else {
                // 调色
                double h, s, l;
                rgbToHsl(r, g, b, h, s, l);
                if (hueShift != 0)  h = std::fmod(h + hueShift + 1.0, 1.0);
                if (satMul != 1.0)   s = clampD(s * satMul, 0.0, 1.0);
                if (lightPct != 0)  l = clampD(lightPct >= 0
                                                ? l + (1 - l) * (lightPct / 100.0)
                                                : l * (1 + lightPct / 100.0),
                                            0.0, 1.0);
                hslToRgb(h, s, l, r, g, b);
            }
            dest.setPixel(x, y, qRgba(r, g, b, a));
        }
    }
}

// ============================== Brightness/Contrast ==============================
void applyBrightnessContrast(QImage &dest, const BcParams &p)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    if (p.brightness == 0 && p.contrast == 0)
        return;
    // brightness: -100..+100 → 字节偏移 -255..+255
    const int b = (p.brightness * 255 + 50) / 100; // 四舍五入
    // contrast: -100..+100 → 倍率 0..2
    const double c = 1.0 + p.contrast / 100.0;

    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a  = qAlpha(px);
            int r = qRed(px);
            int g = qGreen(px);
            int bch = qBlue(px);
            // 先 brightness(线性加),再 contrast(以 128 为中心拉伸)
            r = qBound(0, r + b, 255);
            g = qBound(0, g + b, 255);
            bch = qBound(0, bch + b, 255);
            r  = qBound(0, int(128 + (r    - 128) * c), 255);
            g  = qBound(0, int(128 + (g    - 128) * c), 255);
            bch = qBound(0, int(128 + (bch  - 128) * c), 255);
            dest.setPixel(x, y, qRgba(r, g, bch, a));
        }
    }
}

// ============================== Invert ==============================
void applyInvert(QImage &dest)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a  = qAlpha(px);
            const int r  = 255 - qRed(px);
            const int g  = 255 - qGreen(px);
            const int b  = 255 - qBlue(px);
            dest.setPixel(x, y, qRgba(r, g, b, a));
        }
    }
}

// ============================== Curves ==============================
namespace {

// 估算一个区间端点的导数(Fritsch-Carlson 切线,直译自上游 Swift)
// 返回值用于 [p[i], p[i+1]] 之间的三次曲线
double curveSlope(const QVector<CurvePoint> &p, int i)
{
    const int n = p.size();
    if (n < 2) return 0;
    // 区段斜率
    auto segSlope = [&](int j) -> double {
        return (p[j+1].y - p[j].y) / (p[j+1].x - p[j].x);
    };
    if (i <= 0)          return segSlope(0);
    if (i >= n - 1)     return segSlope(n - 2);
    const double d0 = segSlope(i - 1);
    const double d1 = segSlope(i);
    // 保形条件:如果相邻区段斜率变号,导数设为 0,避免过冲
    if (d0 * d1 <= 0) return 0;
    // Fritsch-Carlson 调和平均
    return 2.0 / (1.0 / d0 + 1.0 / d1);
}

} // namespace

QVector<int> buildCurveLut(const QVector<CurvePoint> &points)
{
    QVector<int> lut(256, 0);
    if (points.size() < 2)
        return lut;
    // 保证端点固定为 (0,0) 和 (255,255),即使上游没传
    QVector<CurvePoint> p = points;
    if (p.first().x > 0)
        p.prepend({0.0, 0.0});
    if (p.last().x < 255)
        p.append({255.0, 255.0});
    // 强制端点
    p.first() = {0.0, 0.0};
    p.last()  = {255.0, 255.0};

    // 对每个输出字节 0..255,找到包含它的区间并计算三次 Hermite
    int seg = 0;
    for (int x = 0; x <= 255; ++x) {
        // 推进到包含 x 的区间
        while (seg < p.size() - 1 && p[seg + 1].x < x)
            ++seg;
        const CurvePoint &a = p[seg];
        const CurvePoint &b = p[seg + 1];
        const double h = b.x - a.x;
        if (h <= 0) { lut[x] = qBound<int>(0, int(a.y), 255); continue; }
        const double t = qBound(0.0, (x - a.x) / h, 1.0);
        const double m0 = curveSlope(p, seg);
        const double m1 = curveSlope(p, seg + 1);
        const double t2 = t * t;
        const double t3 = t2 * t;
        // 标准三次 Hermite 基函数
        const double h00 =  2 * t3 - 3 * t2 + 1;
        const double h10 =       t3 - 2 * t2 + t;
        const double h01 = -2 * t3 + 3 * t2;
        const double h11 =       t3 -     t2;
        const double y = h00 * a.y + h10 * h * m0
                       + h01 * b.y + h11 * h * m1;
        lut[x] = qBound<int>(0, int(std::lround(y)), 255);
    }
    return lut;
}

void applyCurves(QImage &dest, const CurvesParams &params)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    if (params.points.size() < 2)
        return;
    // identity 曲线(0,0)→(255,255)直接返回
    if (params.points.size() == 2
        && qFuzzyCompare(params.points[0].x, 0.0) && qFuzzyCompare(params.points[0].y, 0.0)
        && qFuzzyCompare(params.points[1].x, 255.0) && qFuzzyCompare(params.points[1].y, 255.0))
        return;

    const QVector<int> lut = buildCurveLut(params.points);
    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a  = qAlpha(px);
            const int r  = lut.at(qRed(px));
            const int g  = lut.at(qGreen(px));
            const int b  = lut.at(qBlue(px));
            dest.setPixel(x, y, qRgba(r, g, b, a));
        }
    }
}

} // namespace adj
