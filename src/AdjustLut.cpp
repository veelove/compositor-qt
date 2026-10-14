#include "AdjustLut.h"

#include <QtGlobal>

namespace adj {

namespace {

// 把一张 256 项 int LUT 包装成 ARGB32 图像(A 通道恒等)
void fillFromIntLut(QImage &img, const QVector<int> &lut)
{
    img = QImage(256, 1, QImage::Format_ARGB32);
    for (int v = 0; v < 256; ++v) {
        const int m = qBound(0, lut.at(v), 255);
        img.setPixel(v, 0, qRgba(m, m, m, v));
    }
}

} // namespace

AdjustLut identityLut()
{
    return AdjustLut{};
}

AdjustLut lutFromLevels(const LevelsParams &p)
{
    AdjustLut lut;
    if (p.rgbRange.identity() && p.range.identity())
        return lut;
    const LevelRange per = p.range.normalized();
    const LevelRange rgb = p.rgbRange.normalized();
    lut.data = QImage(256, 1, QImage::Format_ARGB32);
    for (int v = 0; v < 256; ++v) {
        int r = v, g = v, b = v;
        // 与 applyLevels 一致:channel 决定哪些通道先过 per,rgb 总过
        if (p.channel == LevelsParams::Channel::Rgb || p.channel == LevelsParams::Channel::Red)
            r = applyLevelRange(static_cast<uint8_t>(r), per);
        if (p.channel == LevelsParams::Channel::Rgb || p.channel == LevelsParams::Channel::Green)
            g = applyLevelRange(static_cast<uint8_t>(g), per);
        if (p.channel == LevelsParams::Channel::Rgb || p.channel == LevelsParams::Channel::Blue)
            b = applyLevelRange(static_cast<uint8_t>(b), per);
        r = applyLevelRange(static_cast<uint8_t>(r), rgb);
        g = applyLevelRange(static_cast<uint8_t>(g), rgb);
        b = applyLevelRange(static_cast<uint8_t>(b), rgb);
        lut.data.setPixel(v, 0, qRgba(r, g, b, v));
    }
    return lut;
}

AdjustLut lutFromCurves(const CurvesParams &p)
{
    AdjustLut lut;
    if (p.points.size() < 2)
        return lut;
    // identity 曲线(0,0)→(255,255)返回空(与 applyCurves 判断一致)
    if (p.points.size() == 2
        && qFuzzyCompare(p.points[0].x, 0.0) && qFuzzyCompare(p.points[0].y, 0.0)
        && qFuzzyCompare(p.points[1].x, 255.0) && qFuzzyCompare(p.points[1].y, 255.0))
        return lut;
    fillFromIntLut(lut.data, buildCurveLut(p.points));
    return lut;
}

AdjustLut lutFromExposure(const ExposureParams &p)
{
    AdjustLut lut;
    if (qFuzzyIsNull(p.exposure) && qFuzzyIsNull(p.offset) && qFuzzyCompare(p.gamma, 1.0))
        return lut;
    fillFromIntLut(lut.data, buildExposureLut(p));
    return lut;
}

AdjustLut lutFromBrightnessContrast(const BcParams &p)
{
    AdjustLut lut;
    if (p.brightness == 0 && p.contrast == 0)
        return lut;
    // 与 applyBrightnessContrast 一致:先亮度(线性加)再对比度(以 128 为中心)
    const int b = (p.brightness * 255 + 50) / 100;
    const double c = 1.0 + p.contrast / 100.0;
    lut.data = QImage(256, 1, QImage::Format_ARGB32);
    for (int v = 0; v < 256; ++v) {
        int m = qBound(0, v + b, 255);
        m = qBound(0, int(128 + (m - 128) * c), 255);
        lut.data.setPixel(v, 0, qRgba(m, m, m, v));
    }
    return lut;
}

AdjustLut lutCompose(const AdjustLut &first, const AdjustLut &second)
{
    // 语义:先 first 后 second → result[v] = second[first[v]]
    if (first.isNull())
        return second;
    if (second.isNull())
        return first;
    AdjustLut out;
    out.data = QImage(256, 1, QImage::Format_ARGB32);
    for (int v = 0; v < 256; ++v) {
        const QRgb p1 = first.data.pixel(v, 0);
        const int r2 = qRed(second.data.pixel(qRed(p1), 0));
        const int g2 = qGreen(second.data.pixel(qGreen(p1), 0));
        const int b2 = qBlue(second.data.pixel(qBlue(p1), 0));
        out.data.setPixel(v, 0, qRgba(r2, g2, b2, v));
    }
    return out;
}

void applyLutToImage(QImage &dest, const AdjustLut &lut)
{
    if (dest.isNull() || lut.isNull() || dest.format() != QImage::Format_ARGB32)
        return;
    for (int y = 0; y < dest.height(); ++y) {
        for (int x = 0; x < dest.width(); ++x) {
            const QRgb px = dest.pixel(x, y);
            dest.setPixel(x, y, qRgba(lut.mapR(static_cast<uint8_t>(qRed(px))),
                                      lut.mapG(static_cast<uint8_t>(qGreen(px))),
                                      lut.mapB(static_cast<uint8_t>(qBlue(px))),
                                      qAlpha(px)));
        }
    }
}

} // namespace adj
