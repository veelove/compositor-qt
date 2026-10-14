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

// ============================== Black & White ==============================
namespace {

// 6 个色域桶的中点色相(0..1),顺序:红 黄 绿 青 蓝 品红
constexpr double kBwBucketHues[6] = {0.0/6, 1.0/6, 2.0/6, 3.0/6, 4.0/6, 5.0/6};

inline double bwBucketWeight(double h, const int weights[6])
{
    // 在环形色相空间[h ∈ 0,1)上找最近的两个桶,三角加权
    double bestD = 1.0; int bestI = 0;
    double sndD  = 1.0; int sndI  = 0;
    for (int i = 0; i < 6; ++i) {
        double d = std::abs(h - kBwBucketHues[i]);
        if (d > 0.5) d = 1.0 - d; // 环形距离
        if (d < bestD) { sndD = bestD; sndI = bestI; bestD = d; bestI = i; }
        else if (d < sndD) { sndD = d; sndI = i; }
    }
    const double totalD = bestD + sndD;
    if (totalD <= 0) return weights[bestI] / 100.0;
    const double wB = sndD / totalD; // 距离大者权重小
    const double wS = bestD / totalD;
    return (weights[bestI] * wB + weights[sndI] * wS) / 100.0;
}

} // namespace

void applyBlackWhite(QImage &dest, const BwParams &p)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    // 全 0 权重 → 仍会出黑图(0 灰度),不算 identity
    const int w[6] = {p.reds, p.yellows, p.greens, p.cyans, p.blues, p.magentas};
    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a = qAlpha(px);
            const uint8_t r = qRed(px);
            const uint8_t g = qGreen(px);
            const uint8_t b = qBlue(px);
            // 1) 基础亮度(ITU-R BT.601)
            const double baseLum = 0.299 * r + 0.587 * g + 0.114 * b;
            // 2) 按色相分桶算权重
            double hh, ss, ll;
            rgbToHsl(r, g, b, hh, ss, ll);
            const double bw = bwBucketWeight(hh, w); // 0..3
            // 3) 调整后灰度:Photoshop 算法 = baseLum * (0.5 + bw)
            //   0 权重 → 0.5*base(原 50%亮度);100 权重 → 1.5*base(150%亮度)
            int gray = qBound(0, int(baseLum * (0.5 + bw)), 255);
            // 4) tint 上色
            uint8_t r2 = gray, g2 = gray, b2 = gray;
            if (p.tint && p.tintSaturation > 0) {
                const double sat = p.tintSaturation / 100.0;
                const double hue = p.tintHue / 360.0;
                hslToRgb(hue, sat, ll, r2, g2, b2);
                // 与灰度按饱和度混合:tintSat=0→纯灰
                r2 = uint8_t(qBound(0, int(r2 * sat + gray * (1.0 - sat)), 255));
                g2 = uint8_t(qBound(0, int(g2 * sat + gray * (1.0 - sat)), 255));
                b2 = uint8_t(qBound(0, int(b2 * sat + gray * (1.0 - sat)), 255));
            }
            dest.setPixel(x, y, qRgba(r2, g2, b2, a));
        }
    }
}

// ============================== Color Balance ==============================
namespace {

// 按 (cyanRed, magentaGreen, yellowBlue) 偏移修改 RGB
// Photoshop "Color Balance" 方向:滑块向红色/品红/黄色方向拉 = 正值(加红/加品红/加黄),
//   向青色/绿色/蓝色方向拉 = 负值(加青/加绿/加蓝)。
//   青色是 G+B, 红色是 R;品红是 R+B, 绿色是 G;黄色是 R+G, 蓝色是 B
// 我们把 cR/mG/yB 看作"对各互补色的加色强度"(正值,0..100):
//   cR 加青: G += cR, B += cR, R -= cR
//   cR 加红: R += cR, G -= cR/2, B -= cR/2(让 G/B 略减保持亮度)
//   但 cR 是有符号,我们按 |cR| 加主色,|cR| 反方向扣减补色的一半
inline void cbAdjust(int &r, int &g, int &b, int cR, int mG, int yB)
{
    // cR:>0 加红, <0 加青
    if (cR > 0) {
        r = qBound(0, r + cR * 128 / 100, 255);
        g = qBound(0, g - cR *  64 / 100, 255);
        b = qBound(0, b - cR *  64 / 100, 255);
    } else {
        const int amt = -cR * 128 / 100;
        g = qBound(0, g + amt, 255);
        b = qBound(0, b + amt, 255);
        r = qBound(0, r - amt, 255);
    }
    // mG:>0 加品红, <0 加绿
    if (mG > 0) {
        r = qBound(0, r + mG *  64 / 100, 255);
        b = qBound(0, b + mG * 128 / 100, 255);
        g = qBound(0, g - mG * 128 / 100, 255);
    } else {
        const int amt = -mG * 128 / 100;
        g = qBound(0, g + amt, 255);
        r = qBound(0, r - amt / 2, 255);
        b = qBound(0, b - amt / 2, 255);
    }
    // yB:>0 加黄, <0 加蓝
    if (yB > 0) {
        r = qBound(0, r + yB *  64 / 100, 255);
        g = qBound(0, g + yB * 128 / 100, 255);
        b = qBound(0, b - yB * 128 / 100, 255);
    } else {
        const int amt = -yB * 128 / 100;
        b = qBound(0, b + amt, 255);
        r = qBound(0, r - amt / 2, 255);
        g = qBound(0, g - amt / 2, 255);
    }
}

// 按"软阈值"算像素在阴影/中间调/高光的权重(0..1)
// 阴影:亮度<85 完全在,>128 离开;中间:85-170 区间;
// 高光:>170 完全在,<128 离开
// 用三角形权重:在过渡区间线性插值
inline void cbToneWeights(int lum, double &ws, double &wm, double &wh)
{
    // 亮度 0..255,阴影中心 ~64,中间 ~128,高光 ~192
    if (lum < 128) {
        ws = 1.0 - lum / 128.0;          // 0..1 暗色多
        wm = lum / 128.0;                // 0..1 偏亮
        wh = 0;
    } else {
        ws = 0;
        wm = 1.0 - (lum - 128) / 127.0;   // 1..0
        wh = (lum - 128) / 127.0;        // 0..1
    }
}

} // namespace

void applyColorBalance(QImage &dest, const CbParams &p)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    // identity 检查:所有 9 个偏移为 0
    const bool ident =
        p.shadowCyanRed==0 && p.shadowMagentaGreen==0 && p.shadowYellowBlue==0 &&
        p.midCyanRed==0 && p.midMagentaGreen==0 && p.midYellowBlue==0 &&
        p.highlightCyanRed==0 && p.highlightMagentaGreen==0 && p.highlightYellowBlue==0;
    if (ident)
        return;
    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            const int a = qAlpha(px);
            int r = qRed(px);
            int g = qGreen(px);
            int b = qBlue(px);
            // 亮度:ITU-R BT.601
            const int lum = int(0.299 * r + 0.587 * g + 0.114 * b);
            double ws, wm, wh;
            cbToneWeights(lum, ws, wm, wh);
            // 各组 CMY 偏移按权重混合
            const int cR  = int(p.shadowCyanRed * ws    + p.midCyanRed       * wm + p.highlightCyanRed    * wh);
            const int mG  = int(p.shadowMagentaGreen * ws + p.midMagentaGreen  * wm + p.highlightMagentaGreen * wh);
            const int yB  = int(p.shadowYellowBlue   * ws + p.midYellowBlue    * wm + p.highlightYellowBlue  * wh);
            int r2 = r, g2 = g, b2 = b;
            cbAdjust(r2, g2, b2, cR, mG, yB);
            // preserveLuminosity:本 MVP 简化为只注释提示
            // (完整实现:按新像素的色调把亮度归一化回原值,避免改变整体明暗)
            dest.setPixel(x, y, qRgba(r2, g2, b2, a));
        }
    }
}

// ============================== Exposure ==============================
namespace {

// sRGB 字节 → 线性光 [0,1](IEC 61966-2-1 反伽马)
// 直译自上游 ExposureSettings.table
inline double srgbToLinear(double v)
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

// 线性光 [0,1] → sRGB 字节 [0,1]
inline double linearToSrgb(double v)
{
    v = std::max(0.0, v);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

} // namespace

// 生成 Exposure 调色的 256 项 LUT(直译自上游 ExposureSettings.table)
QVector<int> buildExposureLut(const ExposureParams &p)
{
    QVector<int> lut(256);
    const double scale = std::pow(2.0, p.exposure);
    // 直译上游:pow(..., 1 / gamma) → 1/p.gamma;
    // 上游注释:"gamma above 1 brightens the midtones"
    // 在线性光域中,g 值作用于归一化前;把"亮度归一化"理解为:
    //   linear = pow(linear_orig, gamma)   (gamma>1 让中调变亮)
    // 等价于上式的 pow(linear, 1/p.gamma) — 上游用 1/gamma
    const double gammaExp = 1.0 / std::max(0.01, p.gamma);
    for (int i = 0; i < 256; ++i) {
        const double encoded = i / 255.0;
        double linear = srgbToLinear(encoded);
        // 线性光域:乘 2^ev,加 offset,gamma 校正
        linear = std::pow(std::max(0.0, linear * scale + p.offset), gammaExp);
        const double out = linearToSrgb(linear);
        lut[i] = int(std::lround(std::clamp(out, 0.0, 1.0) * 255.0));
    }
    return lut;
}

void applyExposure(QImage &dest, const ExposureParams &params)
{
    if (dest.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    // identity:exposure=0, offset=0, gamma=1
    if (qFuzzyIsNull(params.exposure) && qFuzzyIsNull(params.offset) && qFuzzyCompare(params.gamma, 1.0))
        return;
    const QVector<int> lut = buildExposureLut(params);
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
