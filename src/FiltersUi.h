#pragma once

#include <QColor>
#include <QDialog>

#include <optional>

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QPushButton;
class QSpinBox;

namespace filtui {

// ============================== 通用参数对话框 ==============================
// 便捷构建"滤镜参数"对话框:整数/浮点/布尔/颜色参数,exec 后按返回值读取
class ParamDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ParamDialog(const QString &title, QWidget *parent = nullptr);

    QSpinBox *addInt(const QString &label, int min, int max, int def);
    QDoubleSpinBox *addDouble(const QString &label, double min, double max, double def, int decimals = 1);
    QCheckBox *addBool(const QString &label, bool def);
    QPushButton *addColor(const QString &label, const QColor &def);
    // 读取 addColor 创建的色块按钮当前颜色
    static QColor colorOf(QPushButton *btn);

private:
    QFormLayout *m_form = nullptr;
};

// ============================== 各滤镜参数 ==============================
struct GaussianParams
{
    double radius;
};
std::optional<GaussianParams> promptGaussianBlur(QWidget *parent);

struct MotionBlurParams
{
    double distance;
    double angle;
};
std::optional<MotionBlurParams> promptMotionBlur(QWidget *parent);

struct NoiseParams
{
    double amount;
    bool gaussian;
    bool monochromatic;
    quint32 seed;
};
std::optional<NoiseParams> promptAddNoise(QWidget *parent);

struct VignetteParams
{
    double amount;
    QColor color;
    double midpoint;
    double roundness;
    double feather;
};
std::optional<VignetteParams> promptVignette(QWidget *parent);

} // namespace filtui
