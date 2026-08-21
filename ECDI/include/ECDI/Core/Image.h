#pragma once

#include <cstdint>
#include <vector>

namespace ECDI
{

/// @brief 已解码图像数据（Phase 8 只负责绘制；文件格式加载属未来 ImageLoader）
/// @details 值语义（可拷贝进 RenderCommand）；像素格式固定 32bpp premultiplied BGRA。
/// - premultiplied BGRA：RGB 通道已预先乘以 Alpha（AC_SRC_ALPHA 要求，否则混合色偏）
/// - width/height：非负值；width == 0 || height == 0 视为空图像，DrawImage 不产生绘制
/// - stride：每行字节数（>= width*4，4 字节对齐）
/// - pixels.size() >= stride*height；逐行读取时按 row*stride 定位（不能整体 memcpy）
/// - 行序：top-to-bottom（row 0 = 图像顶行）
struct Image
{
	int width = 0;                    ///< 像素宽度（>= 0）
	int height = 0;                   ///< 像素高度（>= 0）
	int stride = 0;                   ///< 每行字节数（>= width*4）
	std::vector<std::uint8_t> pixels; ///< premultiplied BGRA 像素数据（>= stride*height）
};

}