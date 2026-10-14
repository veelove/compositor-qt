#include "AdjustmentsUi.h"

#include "AdjustLut.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

namespace adjui {

namespace {

class ParamDialog : public QDialog
{
public:
    explicit ParamDialog(const QString &title, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setModal(true);
        m_form = new QFormLayout(this);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        m_form->addRow(bb);
    }

    QSpinBox *addInt(const QString &label, int min, int max, int def)
    {
        auto *s = new QSpinBox(this);
        s->setRange(min, max);
        s->setValue(def);
        m_form->insertRow(m_form->rowCount() - 1, label, s);
        return s;
    }
    QDoubleSpinBox *addDouble(const QString &label, double min, double max, double def, int decimals = 1)
    {
        auto *s = new QDoubleSpinBox(this);
        s->setRange(min, max);
        s->setDecimals(decimals);
        s->setValue(def);
        m_form->insertRow(m_form->rowCount() - 1, label, s);
        return s;
    }
    QComboBox *addChoice(const QString &label, const QStringList &items, int def)
    {
        auto *c = new QComboBox(this);
        c->addItems(items);
        c->setCurrentIndex(def);
        m_form->insertRow(m_form->rowCount() - 1, label, c);
        return c;
    }
    QCheckBox *addBool(const QString &label, bool def)
    {
        auto *c = new QCheckBox(this);
        c->setChecked(def);
        m_form->insertRow(m_form->rowCount() - 1, label, c);
        return c;
    }

private:
    QFormLayout *m_form = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
std::optional<adj::LevelsParams> promptLevels(QWidget *parent)
{
    ParamDialog d(QObject::tr("色阶"), parent);
    auto *black = d.addInt(QObject::tr("黑场"), 0, 254, 0);
    auto *white = d.addInt(QObject::tr("白场"), 1, 255, 255);
    auto *gamma = d.addDouble(QObject::tr("伽马"), 0.1, 9.99, 1.0, 2);
    auto *outBlack = d.addInt(QObject::tr("输出黑场"), 0, 255, 0);
    auto *outWhite = d.addInt(QObject::tr("输出白场"), 0, 255, 255);
    auto *channel = d.addChoice(QObject::tr("通道"),
                                { QObject::tr("RGB"), QObject::tr("红"), QObject::tr("绿"), QObject::tr("蓝") }, 0);
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    adj::LevelsParams p;
    p.range.black      = black->value();
    p.range.white      = white->value();
    p.range.gamma      = gamma->value();
    p.range.outputBlack = outBlack->value();
    p.range.outputWhite = outWhite->value();
    p.channel = static_cast<adj::LevelsParams::Channel>(channel->currentIndex());
    return p;
}

std::optional<adj::HsvParams> promptHueSaturation(QWidget *parent)
{
    ParamDialog d(QObject::tr("色相/饱和度"), parent);
    auto *hue = d.addInt(QObject::tr("色相"), -180, 180, 0);
    auto *sat = d.addInt(QObject::tr("饱和度"), -100, 100, 0);
    auto *light = d.addInt(QObject::tr("明度"), -100, 100, 0);
    auto *colorize = d.addBool(QObject::tr("上色(纯色化)"), false);
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    adj::HsvParams p;
    p.hue = hue->value();
    p.saturation = sat->value();
    p.lightness = light->value();
    p.colorize = colorize->isChecked();
    return p;
}

std::optional<adj::BcParams> promptBrightnessContrast(QWidget *parent)
{
    ParamDialog d(QObject::tr("亮度/对比度"), parent);
    auto *b = d.addInt(QObject::tr("亮度"), -100, 100, 0);
    auto *c = d.addInt(QObject::tr("对比度"), -100, 100, 0);
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    return adj::BcParams{ b->value(), c->value() };
}

std::optional<adj::ExposureParams> promptExposure(QWidget *parent)
{
    ParamDialog d(QObject::tr("曝光"), parent);
    auto *ev = d.addDouble(QObject::tr("曝光(EV)"), -20.0, 20.0, 0.0, 2);
    auto *offset = d.addDouble(QObject::tr("偏移"), -0.5, 0.5, 0.0, 3);
    auto *gamma = d.addDouble(QObject::tr("伽马"), 0.01, 9.99, 1.0, 2);
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    return adj::ExposureParams{ ev->value(), offset->value(), gamma->value() };
}

std::optional<adj::CurvesParams> promptCurves(QWidget *parent)
{
    // MVP:提供 4 个内置样条:恒等/提亮/压暗/高反差,避免用户手绘复杂交互
    ParamDialog d(QObject::tr("曲线"), parent);
    auto *preset = d.addChoice(QObject::tr("预设"),
                               { QObject::tr("恒等(不应用)"),
                                 QObject::tr("提亮中调"),
                                 QObject::tr("压暗中调"),
                                 QObject::tr("高反差(S 型)") },
                               0);
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    adj::CurvesParams p;
    switch (preset->currentIndex()) {
    case 1: // 提亮中调:中段抬高
        p.points = { {0, 0}, {64, 90}, {128, 170}, {192, 230}, {255, 255} };
        break;
    case 2: // 压暗中调
        p.points = { {0, 0}, {64, 25}, {128, 85}, {192, 160}, {255, 255} };
        break;
    case 3: // S 型
        p.points = { {0, 0}, {64, 40}, {128, 128}, {192, 215}, {255, 255} };
        break;
    default:
        return std::nullopt;
    }
    return p;
}

// ---------------------------------------------------------------------------
// 完整的"对话框 → LUT"流程:弹窗 + 构建 LUT,恒等时返回 std::nullopt
// ---------------------------------------------------------------------------
std::optional<LutApplyResult> promptAndBuildLevels(QWidget *parent)
{
    const auto p = promptLevels(parent);
    if (!p)
        return std::nullopt;
    AdjustLut lut = lutFromLevels(*p);
    if (lut.isNull())
        return std::nullopt;
    return LutApplyResult{ lut, QObject::tr("Levels") };
}

std::optional<LutApplyResult> promptAndBuildBrightnessContrast(QWidget *parent)
{
    const auto p = promptBrightnessContrast(parent);
    if (!p)
        return std::nullopt;
    AdjustLut lut = lutFromBrightnessContrast(*p);
    if (lut.isNull())
        return std::nullopt;
    return LutApplyResult{ lut, QObject::tr("Brightness/Contrast") };
}

std::optional<LutApplyResult> promptAndBuildExposure(QWidget *parent)
{
    const auto p = promptExposure(parent);
    if (!p)
        return std::nullopt;
    AdjustLut lut = lutFromExposure(*p);
    if (lut.isNull())
        return std::nullopt;
    return LutApplyResult{ lut, QObject::tr("Exposure") };
}

std::optional<LutApplyResult> promptAndBuildCurves(QWidget *parent)
{
    const auto p = promptCurves(parent);
    if (!p)
        return std::nullopt;
    AdjustLut lut = lutFromCurves(*p);
    if (lut.isNull())
        return std::nullopt;
    return LutApplyResult{ lut, QObject::tr("Curves") };
}

} // namespace adjui
