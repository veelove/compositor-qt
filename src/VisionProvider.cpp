#include "VisionProvider.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPoint>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace {

// 探测 vision_tool.py 候选路径(PATH + 应用同目录 + 系统位置)
QString detectScript()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    // 源码树布局 tools/vision_tool.py(开发/CI)
    // 通过可执行文件路径向上找 tools/vision_tool.py,最多回溯 4 层。
    QStringList srcTreeCandidates;
    {
        QDir dir(appDir);
        for (int i = 0; i < 4; ++i) {
            if (dir.exists(QStringLiteral("tools/vision_tool.py"))) {
                srcTreeCandidates << dir.absoluteFilePath(QStringLiteral("tools/vision_tool.py"));
                break;
            }
            if (!dir.cdUp())
                break;
        }
    }
    const QStringList candidates = {
        appDir + QStringLiteral("/vision_tool.py"),
        appDir + QStringLiteral("/../share/compositor-qt/vision_tool.py"),
        appDir + QStringLiteral("/../../share/compositor-qt/vision_tool.py"),
        QStringLiteral("/usr/local/share/compositor-qt/vision_tool.py"),
        QStringLiteral("/opt/compositor-qt/vision_tool.py"),
        QStringLiteral("/opt/homebrew/share/compositor-qt/vision_tool.py"),
        QStringLiteral("C:/Program Files/CompositorQt/vision_tool.py"),
        QStringLiteral("vision_tool.py"), // PATH
    };
    const QStringList allCandidates = srcTreeCandidates + candidates;
    const QString pathEnv = qEnvironmentVariable("PATH");
    for (const QString &c : allCandidates) {
        if (c.contains('/')) {
            if (QFileInfo::exists(c))
                return c;
        } else {
            for (const QString &dir : pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
                const QString full = QDir(dir).filePath(c);
                if (QFileInfo::exists(full))
                    return full;
            }
        }
    }
    return {};
}

} // namespace

QString VisionProvider::scriptPath()
{
    return detectScript();
}

VisionProvider *VisionProvider::create()
{
    if (detectScript().isEmpty())
        return new StubVisionProvider();
    return new ProcessVisionProvider();
}

bool StubVisionProvider::selectObject(const QImage &, const QPoint &, QImage &, QString *error)
{
    if (error) {
        *error = QStringLiteral(
            "对象选择未实现:未找到 vision_tool.py(视觉智能 Python 桥)。安装 Python + onnxruntime + U²-Net 权重后,放置 vision_tool.py 到应用同目录或 PATH 中,即可启用 Magic Wand 工具。详见 VisionProvider.h。");
    }
    return false;
}

bool ProcessVisionProvider::selectObject(const QImage &image, const QPoint &point,
                                       QImage &mask, QString *error)
{
    mask = QImage();
    if (image.isNull()) {
        if (error) *error = QStringLiteral("输入图像为空");
        return false;
    }
    const QString script = detectScript();
    if (script.isEmpty()) {
        if (error) *error = QStringLiteral("vision_tool.py 不可用");
        return false;
    }

    // 把图像编码为 PNG 字节流,通过 stdin 喂给 Python(避免临时文件)
    QByteArray imgBytes;
    {
        QBuffer buf(&imgBytes);
        buf.open(QIODevice::WriteOnly);
        if (!image.save(&buf, "PNG")) {
            if (error) *error = QStringLiteral("图像编码 PNG 失败");
            return false;
        }
    }

    // 找 Python 解释器
    QString pythonBin = QStringLiteral("python3");
#ifdef Q_OS_WIN
    pythonBin = QStringLiteral("python");
#endif
    const QString pyPath = qEnvironmentVariable("PYTHON");
    if (!pyPath.isEmpty())
        pythonBin = pyPath;

    QProcess proc;
    proc.setProgram(pythonBin);
    QStringList args = { script, QString::number(point.x()), QString::number(point.y()) };
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(5000)) {
        if (error) *error = QStringLiteral("Python 启动失败: %1").arg(proc.errorString());
        return false;
    }
    proc.write(imgBytes);
    proc.closeWriteChannel();
    // 模型推理 + PNG 输出可能慢
    if (!proc.waitForFinished(120000)) {
        if (error) *error = QStringLiteral("vision_tool.py 超时");
        proc.kill();
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error) *error = QStringLiteral("vision_tool.py 失败(code=%1): %2")
                                .arg(proc.exitCode())
                                .arg(QString::fromLocal8Bit(proc.readAllStandardError().left(500)));
        return false;
    }
    const QByteArray data = proc.readAllStandardOutput();
    if (data.isEmpty()) {
        if (error) *error = QStringLiteral("vision_tool.py 输出为空");
        return false;
    }
    if (!mask.loadFromData(data, "PNG")) {
        if (error) *error = QStringLiteral("vision_tool.py 输出无法解析为 PNG");
        return false;
    }
    if (mask.size() != image.size()) {
        // 部分模型输出低分辨率 mask,统一缩放回原图
        mask = mask.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (mask.format() != QImage::Format_Grayscale8) {
        mask = mask.convertToFormat(QImage::Format_Grayscale8);
    }
    return true;
}
