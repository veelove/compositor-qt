#pragma once

#include <QImage>

namespace adj {

// 调色算法集合(MVP):所有调整作用在 QImage 上,8 位 ARGB 格式
//
// 设计:每个算法作为 free function + 参数 struct(无状态),
// 这样测试/未来 GPU 改造只需替换实现,调用方式不变。

// ============================== Levels ==============================
struct LevelRange
{
    double black      = 0;   // 0-254
    double white      = 255; // black+1 ~ 255
    double gamma      = 1.0; // 0.1-9.99
    double outputBlack = 0;   // 0-255
    double outputWhite = 255; // 0-255
    bool   identity() const;
    LevelRange normalized() const;
};

struct LevelsParams
{
    // 0=RGB(作用于所有通道), 1=R, 2=G, 3=B(直译自上游 LevelsChannel)
    enum class Channel { Rgb = 0, Red = 1, Green = 2, Blue = 3 };
    Channel channel = Channel::Rgb;
    LevelRange range;  // 当前通道
    LevelRange rgbRange; // 总开关(若 channel=Rgb 则用这个,否则用每个通道的 range 再叠 rgb)
};

// 一次性 LUT 应用:对一个 8 位值执行黑场/白场/伽马/输出范围
inline uint8_t applyLevelRange(uint8_t v, const LevelRange &r);

// 对整张图像应用 Levels 调整(就地修改 dest)
// algParams:LevelsParams
void applyLevels(QImage &dest, const LevelsParams &params);

// ============================== Hue/Saturation ==============================
struct HsvParams
{
    double hue        = 0;   // -180..+180
    double saturation = 0;   // -100..+100
    double lightness  = 0;   // -100..+100
    bool   colorize   = false; // 把图像转为色相化的单色图像
};

// 对整张图像应用 HSV 调整(就地修改 dest)
void applyHsv(QImage &dest, const HsvParams &params);

} // namespace adj
