#pragma once

#include <cstdint>

namespace ECDI
{

/// @brief 颜色（float RGBA，决策 21/22/23）
/// @details
/// 公共基础类型：全框架共用。
/// - 分量语义约定 [0.0f, 1.0f]（决策 22）
/// - 纯数据零约束：构造不 Clamp，超范围由 Backend 消费时 Clamp（决策 23）
/// - Alpha 保留在数据模型（完整部分），第一版 GDIBackend 不消费（决策 21 澄清）
struct Color
{
	float r = 0.0f;		///< 红 [0,1]
	float g = 0.0f;		///< 绿 [0,1]
	float b = 0.0f;		///< 蓝 [0,1]
	float a = 1.0f;		///< alpha／**不透明度** [0,1]：`1.0` = 完全不透明，`0.0` = 全透明（★ **不是「透明度」**——措辞反了会让「0.8」被误读成 80% 透明）。★ **#71 收口后（2026-10-25）两链全原语消费**：GDI 侧 `DrawRect` / `DrawRoundedRect`（`BlendAlphaSolid`，Phase 9）· `DrawArc` / 覆盖度路径（`RasterizeMask` 预乘，Phase 8.6/24/33）· `DrawText` / `DrawFocusRect`（白墨光栅 luminance 覆盖度 + `BlendCoverageSolid` 预乘合成，#71ⓐ——半透明文本/焦点框的字形边缘为灰度近似，非 ClearType 亚像素）· `DrawLine`（含 AA 关 + a<1 的 ⓑ 收口——覆盖度路径消费）；GL 链全原语消费（`glColor4f` 携带 a）。a=1 的所有路径逐位不变（#71 红线）。★ **窗口清屏路径不消费**（Phase 18 C4：`COLORREF` 无 alpha 通道——见 `Window::SetBackgroundColor` 注释）

	static constexpr Color White()   noexcept { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
	static constexpr Color Black()   noexcept { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
	static constexpr Color Red()     noexcept { return { 1.0f, 0.0f, 0.0f, 1.0f }; }
	static constexpr Color Green()   noexcept { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
	static constexpr Color Blue()    noexcept { return { 0.0f, 0.0f, 1.0f, 1.0f }; }
	static constexpr Color Gray()    noexcept { return { 0.5f, 0.5f, 0.5f, 1.0f }; }

	/// @brief 8-bit 直觉入口（r/g/b/a ∈ [0,255]）
	static constexpr Color FromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept
	{
		return
		{
			r / 255.0f,
			g / 255.0f,
			b / 255.0f,
			a / 255.0f
		};
	}

	/// @brief 相等比较（C++20 默认，r/g/b/a 四字段全比较；Alpha 就位后自动生效）
	constexpr bool operator==(const Color&) const noexcept = default;
};

}
