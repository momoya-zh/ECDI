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
	float a = 1.0f;		///< alpha／**不透明度** [0,1]：`1.0` = 完全不透明，`0.0` = 全透明（★ **不是「透明度」**——措辞反了会让「0.8」被误读成 80% 透明）。消费方**分后端**（★ 双链不对等 = deferred #71，2026-10-24 登记；当前零消费者，触发条件 = 首个半透明文本/焦点框消费者出现）：两链均消费 = `DrawRect` / `DrawRoundedRect`（经 `BlendAlphaSolid`，Phase 9）+ `DrawArc` / 覆盖度路径（经 `RasterizeMask` 预乘，Phase 8.6/24/33）；**GDI 链 `DrawText` / `DrawFocusRect` 恒丢弃**（`ToColorRef` 只取 RGB——GL 链全原语消费）；`DrawLine` 仅「AA 关 + a<1」边角丢弃。★ **窗口清屏路径不消费**（Phase 18 C4：`COLORREF` 无 alpha 通道——见 `Window::SetBackgroundColor` 注释）

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
