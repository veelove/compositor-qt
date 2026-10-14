#include "FiltersUi.h"

#include "Filters.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>

namespace filtui {

namespace {

// 色块按钮:左侧色块 + 颜色名,点击弹颜色选择对话框
QPushButton *makeColorButton(const QColor &color)
{
    auto *btn = new QPushButton;
    btn->setProperty("filterColor", color);
    const auto applySwatch = [btn] {
        const QColor c = btn->property("filterColor").value<QColor>();
        btn->setText(c.name());
        btn->setStyleSheet(QStringLiteral("background-color:%1;color:%2;")
                               .arg(c.name())
                               .arg(c.lightness() > 128 ? QStringLiteral("#000") : QStringLiteral("#fff")));
    };
    QObject::connect(btn, &QPushButton::clicked, btn, [btn, applySwatch] {
        const QColor old = btn->property("filterColor").value<QColor>();
        const QColor c = QColorDialog::getColor(old, btn, QObject::tr("选择颜色"));
        if (c.isValid()) {
            btn->setProperty("filterColor", c);
            applySwatch();
        }
    });
    applySwatch();
    return btn;
}

} // namespace

// ---------------------------------------------------------------------------
// ParamDialog
// ---------------------------------------------------------------------------
ParamDialog::ParamDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);

    m_form = new QFormLayout(this);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_form->addRow(buttons);
}

QSpinBox *ParamDialog::addInt(const QString &label, int min, int max, int def)
{
    auto *spin = new QSpinBox(this);
    spin->setRange(min, max);
    spin->setValue(def);
    m_form->insertRow(m_form->rowCount() - 1, label, spin); // 按钮行保持最后
    return spin;
}

QDoubleSpinBox *ParamDialog::addDouble(const QString &label, double min, double max, double def, int decimals)
{
    auto *spin = new QDoubleSpinBox(this);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setValue(def);
    m_form->insertRow(m_form->rowCount() - 1, label, spin);
    return spin;
}

QCheckBox *ParamDialog::addBool(const QString &label, bool def)
{
    auto *check = new QCheckBox(this);
    check->setChecked(def);
    m_form->insertRow(m_form->rowCount() - 1, label, check);
    return check;
}

QPushButton *ParamDialog::addColor(const QString &label, const QColor &def)
{
    auto *btn = makeColorButton(def);
    m_form->insertRow(m_form->rowCount() - 1, label, btn);
    return btn;
}

QColor ParamDialog::colorOf(QPushButton *btn)
{
    return btn->property("filterColor").value<QColor>();
}

// ---------------------------------------------------------------------------
// 各滤镜参数对话框
// ---------------------------------------------------------------------------
std::optional<GaussianParams> promptGaussianBlur(QWidget *parent)
{
    ParamDialog dlg(QObject::tr("高斯模糊"), parent);
    auto *radius = dlg.addInt(QObject::tr("半径(像素)"), 0, 100, 5);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    return GaussianParams{ double(radius->value()) };
}

std::optional<MotionBlurParams> promptMotionBlur(QWidget *parent)
{
    ParamDialog dlg(QObject::tr("动感模糊"), parent);
    auto *distance = dlg.addInt(QObject::tr("距离(像素)"), 1, 100, 10);
    auto *angle = dlg.addInt(QObject::tr("角度(度)"), 0, 360, 0);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    return MotionBlurParams{ double(distance->value()), double(angle->value()) };
}

std::optional<NoiseParams> promptAddNoise(QWidget *parent)
{
    ParamDialog dlg(QObject::tr("添加杂色"), parent);
    auto *amount = dlg.addInt(QObject::tr("数量(0-100)"), 0, 100, 20);
    auto *gaussian = dlg.addBool(QObject::tr("高斯分布"), true);
    auto *mono = dlg.addBool(QObject::tr("单色"), false);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    // 固定 seed 便于复现;UI 不暴露 seed,每次随机
    const quint32 seed = quint32(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFu);
    return NoiseParams{ double(amount->value()), gaussian->isChecked(), mono->isChecked(), seed };
}

std::optional<VignetteParams> promptVignette(QWidget *parent)
{
    ParamDialog dlg(QObject::tr("暗角"), parent);
    auto *amount = dlg.addInt(QObject::tr("数量(0-100)"), 0, 100, 50);
    auto *color = dlg.addColor(QObject::tr("边缘颜色"), Qt::black);
    auto *midpoint = dlg.addInt(QObject::tr("中点(0-100)"), 0, 100, 50);
    auto *roundness = dlg.addInt(QObject::tr("圆度(-100~100)"), -100, 100, 100);
    auto *feather = dlg.addInt(QObject::tr("羽化(0-100)"), 0, 100, 60);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    return VignetteParams{ double(amount->value()), ParamDialog::colorOf(color),
                           double(midpoint->value()), double(roundness->value()),
                           double(feather->value()) };
}

} // namespace filtui
