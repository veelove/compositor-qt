#pragma once

#include <QImage>
#include <QString>

// 原始 RAW 图像解码器(相机 RAW → 8/16-bit RGB)
//
// 设计:上游 Compositor 用 Core Image CIRAWFilter 调系统 RAW 引擎(支持 30+ 相机格式)。
//      macOS 独占。
// 跨平台替代方案(参考评估报告 / darktable):
//   方案 A:LibRaw 库链接(广泛,LGPL,头文件 libraw/libraw.h,API 庞大)
//   方案 B:dcraw_emu 进程级(命令行调用)→ PNG/TIFF 临时文件
//          极简,免链接;LibRaw 自带,LGPL 协议仅限工具分发
//   方案 C:Qt Multimedia + 系统 RAW(WIC/ImageIO/libraw)
// MVP 选型:方案 B —— 通过 `dcraw_emu` 子进程把 RAW → 临时 PNG,再读回。
//          优点:零编译依赖,跨平台一致;缺点:每张图启动进程(~30-200ms 开销)。
//          若 dcraw_emu 不可用,降级到 StubRawDecoder 抛 NotImplemented。
class RawDecoder
{
public:
    virtual ~RawDecoder() = default;
    // 检测 path 是否为相机 RAW
    static bool isRawFile(const QString &path);
    // 探测系统 dcraw_emu 可执行路径(空 = 不可用)
    static QString dcrawEmuExecutable();
    // 工厂:有 dcraw_emu → LibRawDecoder,否则 → StubRawDecoder
    static RawDecoder *create();
    // 解码 → 8-bit QImage;失败返回 false + error 非空
    virtual bool decode(const QString &path, QImage &out, QString *error) = 0;
};

// LibRaw 进程级实现:调 dcraw_emu 输 PNG 到临时文件
class LibRawDecoder : public RawDecoder
{
public:
    bool decode(const QString &path, QImage &out, QString *error) override;
};

// 退化实现:能识别扩展名但不解码(给清晰错误信息)
class StubRawDecoder : public RawDecoder
{
public:
    bool decode(const QString &path, QImage &out, QString *error) override;
};
