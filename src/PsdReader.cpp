#include "PsdReader.h"

#include <QtGlobal>

#include <QString>
#include <QStringList>
#include <algorithm>
#include <stdexcept>

namespace psd {

namespace {

QString decodeMacRoman(const QByteArray &bytes)
{
    // 优先尝试 UTF-8(现代 Photoshop 实际常用),失败回退到 Latin-1
    QString s = QString::fromUtf8(bytes);
    if (!s.contains(QChar(0xFFFD)))
        return s;
    return QString::fromLatin1(bytes);
}

// 把一个原始 RLE 压缩的 scanline(行)解压成 raw 字节
QByteArray decodeScanline(const QByteArray &input, int width, int depthBytes, int rawLen)
{
    // PSD PackBits:每个 row 独立编码,compressed data 长度 = 2 + sum(decompressed rows)
    // 这里我们传入的就是一条 row 的压缩数据
    return Cursor::packBitsDecode(input, rawLen);
}

struct Layer
{
    QString name;
    int top = 0, left = 0, bottom = 0, right = 0;
    int opacity = 255;
    int visible = 1;
    QString blendMode = "norm";
    // 各通道压缩像素:index 0=R, 1=G, 2=B, 3=A(若存在)
    QByteArray channelData[4];
    int channelDataLen[4] = {0, 0, 0, 0};
    // 解压后每个通道的字节数(width * height)
    int rowBytes = 0;
    int height = 0;
};

// 解码一整个 layer 的所有通道像素(未压缩或 RLE 都处理)
void decodeLayerChannels(Cursor &c, Layer &layer, int canvasW, int canvasH, int depth)
{
    layer.rowBytes = canvasW; // 8-bit 1 通道:每行 canvasW 字节
    layer.height   = canvasH;
    for (int ch = 0; ch < 4; ++ch) {
        if (layer.channelDataLen[ch] <= 0)
            continue;
        const uint16_t encoding = c.u16();
        QByteArray raw;
        const int expected = canvasW * canvasH;
        if (encoding == 0) {
            // Raw
            raw = c.bytes(expected);
        } else if (encoding == 1) {
            // RLE:每行独立压缩,前 2*height 字节是每行压缩后的字节数(大端)
            QVector<uint16_t> rowLen(canvasH);
            for (int y = 0; y < canvasH; ++y)
                rowLen[y] = c.u16();
            raw.reserve(expected);
            for (int y = 0; y < canvasH; ++y) {
                QByteArray comp = c.bytes(rowLen[y]);
                raw.append(Cursor::packBitsDecode(comp, canvasW));
            }
        } else {
            throw QStringLiteral("PSD: 不支持的通道编码 %1").arg(encoding);
        }
        layer.channelData[ch] = raw;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
QByteArray Cursor::bytes(int n)
{
    if (n < 0 || m_offset + n > m_data.size())
        throw QStringLiteral("PSD: out of bounds (%1 bytes at %2)").arg(n).arg(m_offset);
    QByteArray out = m_data.mid(m_offset, n);
    m_offset += n;
    return out;
}

uint8_t Cursor::u8()
{
    if (m_offset + 1 > m_data.size())
        throw QStringLiteral("PSD: out of bounds (u8 at %1)").arg(m_offset);
    return static_cast<uint8_t>(m_data.at(m_offset++));
}

uint16_t Cursor::u16()
{
    const uint8_t hi = u8();
    const uint8_t lo = u8();
    return (static_cast<uint16_t>(hi) << 8) | lo;
}

uint32_t Cursor::u32()
{
    const uint32_t hi = static_cast<uint32_t>(u16());
    return (hi << 16) | u16();
}

int16_t Cursor::i16() { return static_cast<int16_t>(u16()); }
int32_t Cursor::i32() { return static_cast<int32_t>(u32()); }

QByteArray Cursor::packBitsDecode(const QByteArray &input, int expectedLength)
{
    QByteArray out;
    out.reserve(expectedLength);
    int i = 0;
    while (i < input.size() && out.size() < expectedLength) {
        const int8_t n = static_cast<int8_t>(input.at(i++));
        if (n >= 0) {
            const int count = n + 1;
            if (i + count > input.size())
                throw QStringLiteral("PSD: PackBits copy underflow");
            out.append(input.mid(i, count));
            i += count;
        } else if (n != -128) {
            const int count = -n + 1;
            if (i + 1 > input.size())
                throw QStringLiteral("PSD: PackBits repeat underflow");
            out.append(count, input.at(i++));
        }
    }
    if (out.size() != expectedLength)
        throw QStringLiteral("PSD: PackBits decode length mismatch: got %1, expected %2")
            .arg(out.size())
            .arg(expectedLength);
    return out;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------
bool Reader::matches(const QByteArray &data)
{
    return data.size() >= 4 && data.left(4) == QByteArrayLiteral("8BPS");
}

ImportResult Reader::read(const QByteArray &data)
{
    ImportResult r;
    try {
        if (!matches(data))
            throw QStringLiteral("不是 PSD 文件(缺 8BPS 魔数)");

        Cursor c(data);
        c.bytes(4); // 8BPS
        const int version = c.u16();
        if (version != 1 && version != 2)
            throw QStringLiteral("PSD: 不支持的版本 %1").arg(version);
        r.header.version = version;
        c.skip(6);                  // reserved
        c.skip(2);                  // channels
        r.header.canvasHeight = c.u32();
        r.header.canvasWidth  = c.u32();
        r.header.depth        = c.u16();
        r.header.colorMode     = c.u16();
        if (r.header.depth != 8)
            throw QStringLiteral("PSD: 只支持 8 位深度,本文件是 %1").arg(r.header.depth);
        if (r.header.colorMode != 3)
            throw QStringLiteral("PSD: 只支持 RGB (mode 3),本文件是 %1").arg(r.header.colorMode);
        if (r.header.canvasWidth <= 0 || r.header.canvasHeight <= 0
            || r.header.canvasWidth > 30000 || r.header.canvasHeight > 30000)
            throw QStringLiteral("PSD: 画布尺寸超限 %1x%2")
                .arg(r.header.canvasWidth).arg(r.header.canvasHeight);

        // Color Mode Data
        c.skip(c.u32());

        // Image Resources(整体跳过)
        c.skip(c.u32());

        // Layer and Mask Information section
        auto readU64 = [&](bool isPsb) -> int64_t {
            if (isPsb) {
                const uint32_t lo = c.u32();
                const uint32_t hi = c.u32();
                return (static_cast<int64_t>(hi) << 32) | lo;
            }
            return static_cast<int64_t>(c.u32());
        };
        const bool isPsb = (version == 2);
        const int64_t sectionLen = readU64(isPsb);
        const int64_t sectionEnd = c.offset() + sectionLen;
        if (sectionLen < 0)
            throw QStringLiteral("PSD: 负的 layer 段长度");

        QList<Layer> layers;
        if (sectionLen >= 2) {
            // Layer info
            const int64_t layerInfoLen = readU64(isPsb);
            const int64_t layerInfoEnd = c.offset() + layerInfoLen;
            if (layerInfoLen > 0) {
                const int rawCount = c.i16();
                const int count = std::abs(rawCount);
                if (count > 10000)
                    throw QStringLiteral("PSD: 图层数过多 %1").arg(count);
                if (rawCount < 0) {
                    // 透明分组折叠,本 MVP 不展开,视为失败
                    throw QStringLiteral("PSD: 透明图层组折叠(MVP 暂不支持)");
                }
                for (int i = 0; i < count; ++i) {
                    Layer L;
                    L.top    = c.i32();
                    L.left   = c.i32();
                    L.bottom = c.i32();
                    L.right  = c.i32();
                    const int chCount = c.u16();
                    if (chCount < 1 || chCount > 56)
                        throw QStringLiteral("PSD: 通道数异常 %1").arg(chCount);
                    // 通道 id:-1=transparency,0=R,1=G,2=B,3=mask,etc
                    // 本 MVP 只关心 0/1/2(彩色)和 -1(透明度)
                    QList<QPair<int, int64_t>> chans;
                    for (int j = 0; j < chCount; ++j) {
                        const int chId = c.i16();
                        const int64_t chLen = readU64(isPsb);
                        chans.append({chId, chLen});
                    }
                    // 混合模式签名 + key
                    if (c.bytes(4) != QByteArrayLiteral("8BIM"))
                        throw QStringLiteral("PSD: 混合模式签名错误");
                    L.blendMode = QString::fromLatin1(c.bytes(4));
                    L.opacity   = c.u8();
                    c.skip(1); // clipping
                    const uint8_t flags = c.u8();
                    L.visible   = (flags & 2) ? 0 : 255; // bit 1 = visible
                    c.skip(1);
                    const int extraLen = static_cast<int>(c.u32());
                    const int extraEnd = c.offset() + extraLen;
                    // mask data(本 MVP 跳过整个 mask 块)
                    const int maskLen = static_cast<int>(c.u32());
                    c.skip(maskLen);
                    // blending ranges(本 MVP 跳过)
                    const int rangesLen = static_cast<int>(c.u32());
                    c.skip(rangesLen);
                    // name
                    const int nameLen = c.u8();
                    QByteArray nameBytes = c.bytes(nameLen);
                    L.name = decodeMacRoman(nameBytes);
                    // name pad to 4 bytes
                    const int namePad = (4 - ((nameLen + 1) % 4)) % 4;
                    c.skip(namePad);
                    // additional layer info
                    c.seek(extraEnd);

                    // 记录通道数据长度(图层数据段会再次用 readU64 读)
                    for (const auto &ch : chans) {
                        if (ch.first >= 0 && ch.first <= 3)
                            L.channelDataLen[ch.first] = static_cast<int>(ch.second) - 2; // 减去 encoding(2 字节)
                    }
                    layers.append(L);
                }
                // 跳过额外 layer info 块(剩余字节)
                c.seek(layerInfoEnd);

                // Channel image data
                for (auto &L : layers)
                    decodeLayerChannels(c, L, r.header.canvasWidth, r.header.canvasHeight, r.header.depth);
            }
            c.seek(sectionEnd);
        }

        // Image Data(整张合成图,扁平化,通常 4 通道 RGB+A)
        r.composite = QImage(r.header.canvasWidth, r.header.canvasHeight, QImage::Format_ARGB32);
        r.composite.fill(Qt::white);

        const int channelCount = std::min<int>(3, 4); // 我们合成图固定 RGBA
        // PSD 文件中 image data 实际只放 RGB(3 通道,因 alpha 在图层段);3 通道
        // 这里按 3 通道读(R/G/B),后面从 layers 合成 alpha
        QVector<QByteArray> imgChans(3);
        QVector<QVector<uint16_t>> imgRowLens(3);
        for (int ch = 0; ch < 3; ++ch) {
            const uint16_t enc = c.u16();
            const int expected = r.header.canvasWidth * r.header.canvasHeight;
            if (enc == 0) {
                imgChans[ch] = c.bytes(expected);
            } else if (enc == 1) {
                QByteArray raw;
                raw.reserve(expected);
                QVector<uint16_t> rl(r.header.canvasHeight);
                for (int y = 0; y < r.header.canvasHeight; ++y)
                    rl[y] = c.u16();
                for (int y = 0; y < r.header.canvasHeight; ++y) {
                    const QByteArray comp = c.bytes(rl[y]);
                    raw.append(Cursor::packBitsDecode(comp, r.header.canvasWidth));
                }
                imgChans[ch] = raw;
            } else {
                throw QStringLiteral("PSD: image data 编码 %1 不支持").arg(enc);
            }
        }
        // 写进 composite 图像
        for (int y = 0; y < r.header.canvasHeight; ++y) {
            for (int x = 0; x < r.header.canvasWidth; ++x) {
                const int i = y * r.header.canvasWidth + x;
                const int r8 = static_cast<uint8_t>(imgChans[0].at(i));
                const int g8 = static_cast<uint8_t>(imgChans[1].at(i));
                const int b8 = static_cast<uint8_t>(imgChans[2].at(i));
                r.composite.setPixelColor(x, y, QColor(r8, g8, b8));
            }
        }

        // 把 layer 像素**叠**到 composite 上(图层从下到上覆盖)
        for (const Layer &L : layers) {
            LayerInfo info;
            info.name = L.name;
            info.top = L.top; info.left = L.left;
            info.bottom = L.bottom; info.right = L.right;
            info.opacity = L.opacity;
            info.visible = L.visible;
            info.blendMode = L.blendMode;
            r.layers.append(info);
            if (L.visible == 0) continue;
            if (L.bottom <= L.top || L.right <= L.left) continue;
            const int lw = L.right - L.left;
            const int lh = L.bottom - L.top;
            for (int y = 0; y < lh; ++y) {
                const int cy = L.top + y;
                if (cy < 0 || cy >= r.header.canvasHeight) continue;
                for (int x = 0; x < lw; ++x) {
                    const int cx = L.left + x;
                    if (cx < 0 || cx >= r.header.canvasWidth) continue;
                    const int i = y * lw + x;
                    const int r8 = (L.channelData[0].isEmpty() ? 0 : static_cast<uint8_t>(L.channelData[0].at(i)));
                    const int g8 = (L.channelData[1].isEmpty() ? 0 : static_cast<uint8_t>(L.channelData[1].at(i)));
                    const int b8 = (L.channelData[2].isEmpty() ? 0 : static_cast<uint8_t>(L.channelData[2].at(i)));
                    int a8 = (L.channelData[3].isEmpty() ? 255 : static_cast<uint8_t>(L.channelData[3].at(i)));
                    // 透明度按 opacity 缩放
                    a8 = a8 * L.opacity / 255;
                    // 简单 alpha over 合成(normal 模式)
                    if (a8 == 255) {
                        r.composite.setPixelColor(cx, cy, QColor(r8, g8, b8));
                    } else if (a8 > 0) {
                        QColor dst = r.composite.pixelColor(cx, cy);
                        const int inv = 255 - a8;
                        const int nr = (r8 * a8 + dst.red()   * inv) / 255;
                        const int ng = (g8 * a8 + dst.green() * inv) / 255;
                        const int nb = (b8 * a8 + dst.blue()  * inv) / 255;
                        r.composite.setPixelColor(cx, cy, QColor(nr, ng, nb));
                    }
                }
            }
        }

        r.ok = true;
    } catch (const QString &err) {
        r.error = err;
        r.ok = false;
    } catch (const std::exception &e) {
        r.error = QString::fromUtf8(e.what());
        r.ok = false;
    }
    return r;
}

} // namespace psd
