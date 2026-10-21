#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

// 选区运算 —— 让 Delete / 滤镜 / 裁剪 真正尊重 mask 形状而非 bounding box。
//
// 背景:Magic Wand 通过 VisionProvider 拿到的是全图灰度 mask(0=背景,255=前景),
//      而历史上 CanvasView 只保存了一个 bounding rect,导致矩形选区工具与
//      智能选择的行为不一致(矩形外的部分也被处理)。
// 本库把"mask 即选区"作为一等概念:
//   - 所有函数都接受可选 mask(Format_Grayscale8,与图像同尺寸)
//   - mask 为 null 时退化为纯矩形行为,保持向后兼容(RectSelect 工具不受影响)
namespace sel {

// 生成一个"矩形区域为 255、其余为 0"的灰度 mask。
// 纯矩形选区(RectSelect 工具)用它来复用同一套 mask 路径。
QImage maskFromRect(const QRect &rect, const QSize &imageSize);

// mask 中非零像素数(用于状态栏/校验;全 0 mask 返回 0)。
int coverage(const QImage &mask);

// 把 mask 缩放到指定尺寸(内部用 smooth 插值;null mask 返回 null)。
// VisionProvider 已保证 mask 与图像同尺寸,此函数用于尺寸不一致时的兜底。
QImage resampleMask(const QImage &mask, const QSize &targetSize);

// 核心:按 mask 把 processed 混合回 base。
//   - mask == 0   → 保留 base(选区外不受影响)
//   - mask == 255 → 完全使用 processed(选区内完全生效)
//   - 中间值      → 线性插值,天然支持羽化/抗锯齿边缘
// 要求 base 与 processed 同尺寸;mask 为 null 或全 0 时原样返回 base。
QImage blendByMask(const QImage &base, const QImage &processed, const QImage &mask);

// 按 mask 把像素置为透明(选区形状的"删除")。
// 返回 null 表示 mask 为空(无可清除区域),调用方应跳过以免产生空撤销记录。
// 实现:对 mask>0 的像素写 alpha=0;mask 为 null 时退化为清空 rect。
QImage clearByMask(const QImage &src, const QRect &rect, const QImage &mask);

// 按 mask 裁剪:保留 mask bbox 内的像素,bbox 外填透明。
// 这是 Photoshop "Crop to Selection" 的行为(mask 形状本身不裁剪像素,
// 因为裁剪是矩形操作;mask 只决定裁剪范围)。
// mask 为 null 时按 rect 裁剪;rect 为空返回 null。
QImage cropToMask(const QImage &src, const QRect &rect, const QImage &mask);

// ============================== 布尔运算 ==============================
// 选区组合模式,对齐 Photoshop 的 Shift/Alt 修饰键行为。
enum class CombineOp {
    Union,      // 加选:a ∪ b
    Subtract,   // 减选:a - b
    Intersect   // 交集:a ∩ b
};

// 把 base 与 add 按 op 组合,返回新的 mask。
// 任一为 null 时按"空集"处理:Union 返回另一个,Subtract/Intersect 返回全 0。
// 两边尺寸不一致时以 targetSize 为准(内部 smooth 缩放)。
QImage combine(const QImage &base, const QImage &add, CombineOp op, const QSize &targetSize);

// ============================== 选区修整(Photoshop Select→Modify) ==============================
// 羽化:对 mask 做半径 radius 的盒式模糊,产生半透明过渡边缘。
// radius<=0 返回原 mask;这是"扩展/收缩"的共同基础。
QImage featherMask(const QImage &mask, double radius);

// 扩展(grow):半径 radius 的形态学膨胀,选区轮廓向外扩张 radius 像素。
// radius<=0 返回原 mask。
QImage growMask(const QImage &mask, int radius);

// 收缩(shrink):半径 radius 的形态学腐蚀,选区轮廓向内收缩 radius 像素。
// radius<=0 返回原 mask;收缩到全空时返回全 0 mask(而不是 null)。
QImage shrinkMask(const QImage &mask, int radius);

} // namespace sel