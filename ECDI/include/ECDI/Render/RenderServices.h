#pragma once

#include <memory>

namespace ECDI{

class RenderingBackend;   // 前置声明（unique_ptr 成员不需要完整定义）
class TextMeasurer;

/// @brief 渲染服务包（7.1.4：绘制 + 测量两个独立能力一次注入）
/// @details 用户决策 2026-08-16：能力接口分离 + 拆类——unique_ptr 各自独立对象，
/// 无 shared_ptr（默认后端 GDIBackend/GDITextMeasurer 是两个独立类）。
/// 未来 Linux：OpenGLRenderer + FreeTypeTextMeasurer 填同一 bundle（接口分离天然支持）。
/// move-only（unique_ptr 成员）——按值传参 + std::move 是标准惯用法。
struct RenderServices{
	std::unique_ptr<RenderingBackend> renderer;   ///< 绘制能力（GDIBackend）
	std::unique_ptr<TextMeasurer> measurer;       ///< 测量能力（GDITextMeasurer）
};

}
