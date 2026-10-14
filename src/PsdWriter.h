#pragma once

#include <QByteArray>
#include <QString>

class Document;

// PSD 导出器 — 把文档扁平化写为 Photoshop .psd 文件(RGB, 8 位)
//
// 规范:Adobe Photoshop File Formats Specification (2019 HTML edition)
// 范围(本 MVP):
//   - 8 位 RGB
//   - 所有可见图层经渲染器合成成单张扁平图(无图层结构)
//   - 压缩方式 1 (PackBits/RLE)
//   - 可选带 alpha(4 通道)或不带(3 通道)
// 不支持的写盘特性(留待未来):图层结构/蒙版/文字/矢量/16bit/CMYK
namespace psd {

struct ExportOptions
{
    bool includeAlpha = true;   // 写 4 通道(RGB+A)或 3 通道(RGB)
};

// 导出 Document → .psd 二进制字节
// 失败:返回空 QByteArray 且 error 非空
QByteArray writePsd(const Document &document, const ExportOptions &opts = {}, QString *error = nullptr);

// 调试辅助:验证自己写的 .psd 能被 Reader 读回(round-trip 测试用)
// 直接调 Reader::read 即可,无需额外函数

} // namespace psd
