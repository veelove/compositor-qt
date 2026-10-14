#pragma once

#include "Adjustments.h"

#include <QImage>

namespace adj {

// 调色 LUT:256x1 的 ARGB32 图像,索引 x = 输入字节(0..255),
// R/G/B = 各通道的映射输出,A = 恒等(alpha 不参与调色)。
//
// 用途:把"逐通道"类调整(Levels/Curves/Exposure/BrightnessContrast)
// 预计算成一张查表,渲染期在 GPU(fragment shader)或 CPU 上逐像素查表,
// 从而实现非破坏性实时调色 —— 图层像素本身不被修改。
//
// 语义约定:
//   - 空 data(isNull) = 恒等(无调色)
//   - lutCompose(first, second) = 先 first 后 second,即 second ∘ first
//   - HSV / Black&White / ColorBalance 需跨通道计算,不适合纯 LUT,不在本管线内
struct AdjustLut
{
    QImage data; // 256x1, Format_ARGB32

    bool isNull() const { return data.isNull(); }

    // 每个输入字节都映射到自身 → 恒等
    bool isIdentity() const
    {
        if (data.isNull())
            return true;
        for (int v = 0; v < 256; ++v) {
            const QRgb px = data.pixel(v, 0);
            if (qRed(px) != v || qGreen(px) != v || qBlue(px) != v)
                return false;
        }
        return true;
    }

    uint8_t mapR(uint8_t v) const { return uint8_t(qRed(data.pixel(v, 0))); }
    uint8_t mapG(uint8_t v) const { return uint8_t(qGreen(data.pixel(v, 0))); }
    uint8_t mapB(uint8_t v) const { return uint8_t(qBlue(data.pixel(v, 0))); }
};

// 恒等 LUT(返回空,即无调色)
AdjustLut identityLut();

// 从各调色参数构建 LUT;参数为恒等时返回空
AdjustLut lutFromLevels(const LevelsParams &params);
AdjustLut lutFromCurves(const CurvesParams &params);
AdjustLut lutFromExposure(const ExposureParams &params);
AdjustLut lutFromBrightnessContrast(const BcParams &params);

// 复合两张 LUT:先 first 后 second(second ∘ first);任一为空则返回另一张
AdjustLut lutCompose(const AdjustLut &first, const AdjustLut &second);

// 对整张图像查表(就地,CPU 路径;与 GPU 查表结果一致)
void applyLutToImage(QImage &dest, const AdjustLut &lut);

} // namespace adj
