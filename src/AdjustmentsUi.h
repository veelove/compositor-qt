#pragma once

#include "Adjustments.h"
#include "AdjustLut.h"

#include <optional>

class QWidget;

namespace adjui {

// 各调色参数对话框:返回 std::nullopt = 用户取消,否则返回填写好的参数
// LUT 类(Levels/Curves/Exposure/BC)只弹参数,具体 LUT 由调用方构建

std::optional<LevelsParams> promptLevels(QWidget *parent);
std::optional<HsvParams> promptHueSaturation(QWidget *parent);
std::optional<BcParams> promptBrightnessContrast(QWidget *parent);
std::optional<ExposureParams> promptExposure(QWidget *parent);
std::optional<CurvesParams> promptCurves(QWidget *parent);

// 完整的"应用 LUT"流程:弹对话框 → 构建 LUT → 过滤恒等 → 返回
// 恒等时返回 std::nullopt(避免撤销栈里堆 0 像素的 undo)
struct LutApplyResult
{
    AdjustLut lut;
    QString label;  // Undo 菜单显示名
};
std::optional<LutApplyResult> promptAndBuildLevels(QWidget *parent);
std::optional<LutApplyResult> promptAndBuildBrightnessContrast(QWidget *parent);
std::optional<LutApplyResult> promptAndBuildExposure(QWidget *parent);
std::optional<LutApplyResult> promptAndBuildCurves(QWidget *parent);

} // namespace adjui
