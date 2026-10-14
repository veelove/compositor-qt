#include "PsdWriter.h"

#include "Document.h"
#include "PainterRenderer.h"

#include <QByteArray>
#include <QDataStream>
#include <QImage>

namespace psd {

namespace {

// 写 u16/u32 大端(PSD 规范全大端)
void putU16(QByteArray &out, quint16 v)
{
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

void putU32(QByteArray &out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

void putI32(QByteArray &out, qint32 v)
{
    putU32(out, static_cast<quint32>(v));
}

// PackBits 编码单行(width 字节) → 压缩字节
// 输出与解压端(packBitsDecode)完全互逆
QByteArray packBitsEncode(const QByteArray &row)
{
    QByteArray out;
    const int n = row.size();
    int i = 0;
    while (i < n) {
        // 找重复块
        int run = 1;
        while (i + run < n && row.at(i + run) == row.at(i) && run < 128)
            ++run;
        if (run >= 2) {
            // repeat run: n = 1 - len (即 -(len-1))
            out.append(char(static_cast<qint8>(1 - run)));
            out.append(row.at(i));
            i += run;
        } else {
            // literal run:尽量多收 1..128 字节,直到遇到下一个 >=2 的重复块
            int lit = 1;
            while (i + lit < n && lit < 128) {
                const int j = i + lit;
                int probe = 1;
                while (j + probe < n && row.at(j + probe) == row.at(j) && probe < 128)
                    ++probe;
                if (probe >= 2)
                    break;
                ++lit;
            }
            out.append(char(static_cast<qint8>(lit - 1)));
            out.append(row.mid(i, lit));
            i += lit;
        }
    }
    return out;
}

} // namespace

QByteArray writePsd(const Document &document, const ExportOptions &opts, QString *error)
{
    QByteArray out;
    const auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        out.clear();
        return out;
    };

    if (document.size().isEmpty() || document.size().width() <= 0 || document.size().height() <= 0)
        return fail(QStringLiteral("PSD 导出:画布尺寸无效"));

    // 1) 渲染合成图(所有可见图层)
    PainterRenderer renderer;
    QImage composite = renderer.render(document);
    if (composite.isNull())
        return fail(QStringLiteral("PSD 导出:渲染合成图失败"));
    if (composite.format() != QImage::Format_ARGB32)
        composite = composite.convertToFormat(QImage::Format_ARGB32);

    const int width  = composite.width();
    const int height = composite.height();
    const int channels = opts.includeAlpha ? 4 : 3;

    // 2) 文件头(26 字节)
    out.append("8BPS", 4);
    putU16(out, 1);                    // version 1 (PSD, 非 PSB)
    out.append(QByteArray(6, '\0'));   // reserved
    putU16(out, static_cast<quint16>(channels)); // channels
    putU32(out, static_cast<quint32>(height));   // height
    putU32(out, static_cast<quint32>(width));    // width
    putU16(out, 8);                    // depth 8 bit
    putU16(out, 3);                    // colorMode 3 = RGB

    // 3) Color Mode Data (长度 0)
    putU32(out, 0);

    // 4) Image Resources (长度 0)
    putU32(out, 0);

    // 5) Layer and Mask Information (长度 0 → 扁平化,无图层)
    putU32(out, 0);

    // 6) Image Data
    //    压缩方式 1 (RLE/PackBits)
    putU16(out, 1);

    //    提取每个通道为按行排列的字节
    //    注意:PSD 像素数据行序 = 从上到下(Row 0 是顶部)
    //    QImage ARGB32 行序也是从上到下,但需要逐像素提取通道
    QVector<QByteArray> planes(4); // R,G,B,A
    for (int ch = 0; ch < 4; ++ch)
        planes[ch].resize(width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const QRgb px = composite.pixel(x, y);
            const int idx = y * width + x;
            planes[0][idx] = char(qRed(px));
            planes[1][idx] = char(qGreen(px));
            planes[2][idx] = char(qBlue(px));
            planes[3][idx] = char(qAlpha(px));
        }
    }

    //    每通道:RLE(每行独立 PackBits)
    //    先写每行压缩后字节数(u16),再写压缩数据
    //    PSB 用 u32,PSD 用 u16
    for (int ch = 0; ch < channels; ++ch) {
        // a) 每行压缩后字节数(先占位,填 u16)
        QByteArray rowLengths;
        rowLengths.reserve(height * 2);
        QByteArray payload;
        for (int y = 0; y < height; ++y) {
            const QByteArray row(planes[ch].constData() + y * width, width);
            const QByteArray enc = packBitsEncode(row);
            rowLengths.append(char((enc.size() >> 8) & 0xFF));
            rowLengths.append(char(enc.size() & 0xFF));
            payload.append(enc);
        }
        // b) 写长度表 + 数据
        out.append(rowLengths);
        out.append(payload);
    }

    return out;
}

} // namespace psd
