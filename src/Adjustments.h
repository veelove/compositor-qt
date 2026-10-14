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

// ============================== Brightness/Contrast ==============================
// Photoshop 经典实现:
//   brightness ∈ [-100, +100]:线性偏移(Photoshop 早期是减法,现代实现是直接加)
//   contrast   ∈ [-100, +100]:以 128 为中心线性拉伸
//     result = 128 + (v - 128) * (1 + contrast/100)
struct BcParams
{
    int brightness = 0;
    int contrast   = 0;
};

void applyBrightnessContrast(QImage &dest, const BcParams &params);

// ============================== Invert ==============================
// 简单反相:R = 255 - R, G = 255 - G, B = 255 - B,alpha 保持
void applyInvert(QImage &dest);

// ============================== Curves ==============================
// 曲线调色:每个通道(R/G/B + Composite)各一条曲线,
// 由控制点序列定义(端点固定 (0,0) 和 (255,255))
// 使用保形三次 Hermite 插值(shape-preserving cubic)避免控制点之间的过冲
struct CurvePoint
{
    double x = 0;
    double y = 0;
};

struct CurvesParams
{
    // 通道 index:0=RGB(对每通道先用本通道 LUT,再用 RGB 通道 LUT 复合),
    //             1=R, 2=G, 3=B(只查该通道,再用 RGB 通道 LUT 复合)
    // 与上游 LevelsChannel 顺序一致
    int channel = 0; // 0=RGB, 1=R, 2=G, 3=B
    QVector<CurvePoint> points; // 每条曲线的控制点(每通道各自存)
};

// 内部:对单一通道 (x, y) 控制点序列,生成 256 项 LUT
// (保形三次 Hermite 插值,Fritsch-Carlson 切线估计)
QVector<int> buildCurveLut(const QVector<CurvePoint> &points);

void applyCurves(QImage &dest, const CurvesParams &params);

// ============================== Black & White ==============================
// Photoshop "Black & White" 调整:
//   6 个色域权重(红/黄/绿/青/蓝/品红,单位 %),各控制对应颜色区域对灰度贡献的强度
//   默认值(40, 60, 40, 60, 20, 80)≈ Photoshop 默认
//   整图灰度:对每个像素,按色相分到 6 个颜色桶之一,按桶权重算灰度贡献
//   简化实现:每像素先做 ITU-R BT.601 亮度(0.299R+0.587G+0.114B),
//   再按"当前像素色相 vs 桶中点色相"距离 + 桶权重做修正(柔过渡)
struct BwParams
{
    int reds     =  40;
    int yellows  =  60;
    int greens   =  40;
    int cyans    =  60;
    int blues    =  20;
    int magentas =  80;
    bool tint = false;
    int tintHue = 40;        // 0-360
    int tintSaturation = 20; // 0-100
};

void applyBlackWhite(QImage &dest, const BwParams &params);

// ============================== Color Balance ==============================
// Photoshop "Color Balance": 阴影/中间调/高光三组 CMY 加/减
//   范围 -100..+100,0=不变
//   算法:对每像素先算亮度,按阈值分入 shadow(亮度<85)/mid(85-170)/highlight(>170),
//   对应组别的 CMY 偏移用加法叠加,preserveLuminosity 决定是否最后归一化到原亮度
struct CbParams
{
    // 阴影 0..255 的中心 85,中间 128,高光 170
    int shadowCyanRed       = 0;
    int shadowMagentaGreen  = 0;
    int shadowYellowBlue    = 0;
    int midCyanRed          = 0;
    int midMagentaGreen     = 0;
    int midYellowBlue       = 0;
    int highlightCyanRed    = 0;
    int highlightMagentaGreen= 0;
    int highlightYellowBlue  = 0;
    bool preserveLuminosity = true;
};

void applyColorBalance(QImage &dest, const CbParams &params);

} // namespace adj
