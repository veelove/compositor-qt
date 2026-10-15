#include "RawProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QtGlobal>

#include <memory>

// 已知相机 RAW 扩展名(大小写不敏感);非穷举,仅覆盖主流
static const QStringList kRawExtensions = {
    "dng", "cr2", "cr3", "nef", "nrw", "arw", "sr2", "srw",
    "rw2", "raf", "orf", "pef", "dcr", "kdc", "mrw", "3fr",
    "iiq", "x3f", "rwl", "r3d", "crw", "erf",
};

bool RawDecoder::isRawFile(const QString &path)
{
    const int dot = path.lastIndexOf('.');
    if (dot < 0)
        return false;
    const QString ext = path.mid(dot + 1).toLower();
    return kRawExtensions.contains(ext);
}

// 探测 dcraw_emu 可执行:常见路径 + PATH
QString RawDecoder::dcrawEmuExecutable()
{
    static const QStringList candidates = {
        "dcraw_emu",     // PATH
        "dcraw",         // 旧版
        "/usr/bin/dcraw_emu",
        "/usr/local/bin/dcraw_emu",
        "/opt/homebrew/bin/dcraw_emu",
        "C:/Program Files/LibRaw/bin/dcraw_emu.exe",
        "C:/LibRaw/bin/dcraw_emu.exe",
    };
    const QString pathEnv = qEnvironmentVariable("PATH");
    for (const QString &c : candidates) {
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

RawDecoder *RawDecoder::create()
{
    const QString exe = dcrawEmuExecutable();
    if (exe.isEmpty())
        return new StubRawDecoder();
    return new LibRawDecoder();
}

bool StubRawDecoder::decode(const QString &path, QImage &out, QString *error)
{
    Q_UNUSED(out);
    if (error) {
        *error = QStringLiteral(
            "未找到 dcraw_emu(LibRaw CLI);无法解码 RAW %1。
"
            "安装 LibRaw 后重试,或将相机 RAW 转为 TIFF/DNG 在 Photoshop 打开。")
            .arg(path);
    }
    return false;
}

bool LibRawDecoder::decode(const QString &path, QImage &out, QString *error)
{
    out = QImage();
    const QString exe = dcrawEmuExecutable();
    if (exe.isEmpty()) {
        if (error) *error = QStringLiteral("dcraw_emu 不可用");
        return false;
    }
    // 临时输出 PNG;dcraw_emu -e(extract thumbnail)或 -q 0(高质)我们选 -q 0(quick&high)
    QTemporaryFile tmp(QStringLiteral("%1/compositor_raw_XXXXXX.png").arg(QDir::tempPath()));
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        if (error) *error = QStringLiteral("无法创建临时文件: %1").arg(tmp.errorString());
        return false;
    }
    const QString tmpPath = tmp.fileName();
    tmp.close();

    // 调用 dcraw_emu: -q 0 (quick) -T (TIFF) 我们选 PNG 导出,所以不用 -T
    // dcraw_emu [options] file → 输出默认 PPM/TIFF → 用 -F 强制 TIFF;或输出同主名 .tiff
    // 简化:用 -e(extract embedded thumb)若可用;否则 -q 0 输出 .tiff
    // 实际方案:把 stdout 设为临时文件名(用 -o)→ 写主名.tiff 在同目录
    // 更简单:用工作目录 + -Z(Zero output,size?) 改用直接 stdout PPM,Qt 读 PPM
    // 选最稳的:让 dcraw_emu 输出 PPM 到 stdout,Qt 直接读 PPM
    QProcess proc;
    proc.setProgram(exe);
    // -w 不改尺寸, -q 0 高质量, -c 不写文件  → stdout 输出 PPM
    QStringList args = { "-q", "0", "-w", "-c", path };
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(5000)) {
        if (error) *error = QStringLiteral("dcraw_emu 启动失败: %1").arg(proc.errorString());
        QFile::remove(tmpPath);
        return false;
    }
    if (!proc.waitForFinished(60000)) { // 大 RAW 1 分钟超时
        if (error) *error = QStringLiteral("dcraw_emu 解码超时");
        proc.kill();
        QFile::remove(tmpPath);
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error) *error = QStringLiteral("dcraw_emu 失败(code=%1): %2")
                                .arg(proc.exitCode())
                                .arg(QString::fromLocal8Bit(proc.readAllStandardError().left(500)));
        return false;
    }
    const QByteArray data = proc.readAllStandardOutput();
    if (data.isEmpty()) {
        if (error) *error = QStringLiteral("dcraw_emu 输出为空");
        return false;
    }
    if (!out.loadFromData(data, "PPM")) {
        // 退到尝试 TIFF
        if (!out.loadFromData(data, "TIFF")) {
            if (error) *error = QStringLiteral("解码数据无法识别为图像(可能不是 PPM/TIFF)");
            return false;
        }
    }
    if (out.isNull()) {
        if (error) *error = QStringLiteral("解码后图像为空");
        return false;
    }
    if (out.format() != QImage::Format_ARGB32)
        out = out.convertToFormat(QImage::Format_ARGB32);
    return true;
}
