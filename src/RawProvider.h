#pragma once

#include <QImage>
#include <QString>

// 原始 RAW 图像解码器(相机 RAW → 8/16-bit RGB)
//
// 设计:上游 Compositor 用 Core Image CIRAWFilter 调系统 RAW 引擎(支持 30+ 相机格式)。
//      macOS 独占。
// 跨平台替代方案(参考评估报告 / darktable):
//   方案 A:LibRaw(广泛,支持 30+ 格式,LGPL,头文件 libraw/libraw.h,C++ wrapper class LibRaw)
//          示例:
//            LibRaw p;
//            p.open_file("foo.dng");
//            p.unpack();
//            p.dcraw_process();
//            p.imgdata.params.output_bps = 8;
//            p.dcraw_make_mem_image(&p.imgdata.sizes);
//          风险:LibRaw API 庞大(~1MB 头),版权协议审查;CI 装包(LGPL 动态库)
//   方案 B:dcraw 进程级(命令行调用 dcraw_emu)→ PNG
//          极简,免链接,但延迟 + 需附装 dcraw
//   方案 C:Qt Multimedia + 系统 RAW(Windows 通过 WIC / macOS 通过 ImageIO / Linux 通过 libraw)
//          维护成本低,但能力受限
// MVP 选型(待 spike 后定):方案 A 的薄封装,只暴露 camera + 解码 → 8-bit QImage
//
// 现状(MVP):仅 stub 实现,能识别 RAW 扩展名;实际解码抛 RawError::NotImplemented
class RawDecoder
{
public:
    virtual ~RawDecoder() = default;
    // 检测 path 是否为相机 RAW
    static bool isRawFile(const QString &path);
    // 解码 → 8-bit QImage;失败返回 false + error 非空
    virtual bool decode(const QString &path, QImage &out, QString *error) = 0;
};
