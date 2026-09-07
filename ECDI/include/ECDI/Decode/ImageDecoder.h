#pragma once

#include "ECDI/Core/Image.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ECDI::Decode
{

/// @brief 图片解码（Phase 11——WIC 后端，文件/内存 → Image）
/// @details 无状态自由函数 API：失败一律返回空 Image（width==0，DrawImage 天然跳过）
///          并经 Logger 记 Error；不抛异常。实际支持格式由系统 WIC 解码器决定
///          （BMP/PNG/JPEG/GIF/TIFF/ICO 等——ECDI 不对 WIC 可识别格式做独立兼容承诺）。
///          像素输出固定 premultiplied BGRA / top-down / stride = width*4（32bppPBGRA 直灌）。

/// @brief 从内存字节流解码图像
/// @param data  原始编码字节（PNG/JPEG/... 二进制内容，非像素数据）
/// @param size  字节数
/// @return 解码成功返回填充的 Image；失败返回空 Image
[[nodiscard]] Image DecodeMemory(const std::uint8_t* data, std::size_t size);

/// @brief 从文件解码
/// @param utf8Path 文件路径（UTF-8——框架公共 API 编码契约，内部 UTF8ToWide）
[[nodiscard]] Image DecodeFile(const std::string& utf8Path);

}
