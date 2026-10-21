#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <cstdint>

namespace psd {

// PSD 文件头(8BPS Section)
struct Header
{
    int version = 0;          // 1=PSD, 2=PSB
    int channels = 0;         // 通道数(header 字段)
    int canvasWidth = 0;
    int canvasHeight = 0;
    int depth = 0;            // 期望 8(本 MVP 只支持 8 位)
    int colorMode = 0;        // 期望 3=RGB 或 4=CMYK(本 MVP 只支持 3)
};

// 简洁只读游标(大端,PSD 字段全是大端)
class Cursor
{
public:
    explicit Cursor(const QByteArray &data) : m_data(data) {}

    int offset() const { return m_offset; }
    void seek(int pos) { m_offset = pos; }
    void skip(int n) { m_offset += n; }

    bool atEnd() const { return m_offset >= m_data.size(); }
    int remaining() const { return m_data.size() - m_offset; }

    // 读取失败抛 QString 异常
    QByteArray bytes(int n);
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    int16_t i16();
    int32_t i32();

    // PackBits 解压(PackBitsRunLengthEncoding,PSD Spec 6.2 RLE)
//   - 输入:已读出的"压缩字节"数组(每行/每通道一串)
//   - 输出:解压后的字节数组(长度 = uncompressedLength,来自 channel.length - 2)
    static QByteArray packBitsDecode(const QByteArray &input, int expectedLength);

private:
    QByteArray m_data;
    int m_offset = 0;
};

// PSD 导入结果(扁平化:一张合成图 + 图层元数据列表)
struct LayerInfo
{
    QString name;
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    int opacity = 255; // 0-255
    int visible = 1;  // 0/1
    QString blendMode; // "norm" / "mult" / ... (MVP 暂不全用)
};

struct ImportResult
{
    bool ok = false;
    QString error;       // ok=false 时填充
    Header header;
    QImage composite;    // 合并所有可见图层后的 RGBA 图像(画布尺寸)
    QList<LayerInfo> layers;
};

class Reader
{
public:
    // 解析 PSD 数据,失败时 result.ok=false 且 result.error 含原因
    static ImportResult read(const QByteArray &data);
    static bool matches(const QByteArray &data); // 检查 magic 头 "8BPS"
};

} // namespace psd
