#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUuid>

// 图层:持有像素数据(QImage,原型阶段;未来可换成 GPU 纹理)与显示属性。
class Layer : public QObject
{
    Q_OBJECT
public:
    explicit Layer(const QString &name, const QSize &size, const QColor &fill = Qt::transparent,
                   QObject *parent = nullptr);

    QUuid id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString &name);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    QImage &pixels() { return m_pixels; }
    const QImage &pixels() const { return m_pixels; }

signals:
    void changed(); // 像素或显示属性变化

private:
    QUuid m_id;
    QString m_name;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    QImage m_pixels;
};
