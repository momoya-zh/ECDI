#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Rect.h"

namespace ECDI::DesktopNest {

/// @brief 调色板——自绘层（框背景/标题条分隔线/浮层/徽标）的颜色单一真相源。
/// @details 为什么单独提出（ModelProbe::Palette 先例）：这些色由 4 个 TU 共享
///          （BoxView / MemberListPopup / BoxWindow / DesktopNest），放 BoxView.h 会造
///          BoxView↔MemberListPopup 循环包含，放 BoxModel.h 会破「Model 零 ECDI 依赖」。
///          命名取**用途**而非颜色——改配色只动这里，语义不变。
/// @note 按钮保持框架默认 ButtonStyle（M1 消费现有主题能力，不自绘皮肤——YAGNI；见详设 §8 D-Q1）。
struct Palette {
	/// @brief 框背景——#2b2f38
	static constexpr Color BoxBackground() noexcept { return Color::FromRGBA8(43, 47, 56, 255); }

	/// @brief 标题条分隔线——#101216
	static constexpr Color TitleSeparator() noexcept { return Color::FromRGBA8(16, 18, 22, 255); }

	/// @brief 标题文本——#e8e8e8
	static constexpr Color TextForeground() noexcept { return Color::FromRGBA8(232, 232, 232, 255); }

	/// @brief 内容假行——#9aa0aa
	static constexpr Color SecondaryText() noexcept { return Color::FromRGBA8(154, 160, 170, 255); }

	/// @brief 浮层背景——#33373f
	static constexpr Color PopupBackground() noexcept { return Color::FromRGBA8(51, 55, 63, 255); }

	/// @brief 徽标底——#c75450
	static constexpr Color BadgeBackground() noexcept { return Color::FromRGBA8(199, 84, 80, 255); }

	/// @brief 徽标字——#ffffff
	static constexpr Color BadgeText() noexcept { return Color::FromRGBA8(255, 255, 255, 255); }
};

/// @brief 布局度量（DIP——公共 API 语义恒为 DIP，Phase 20 口径）。
struct Metrics {
	static constexpr int kWindowWidth = 220;        ///< 框宽（需求 K1：roadmap §8.1 示例值）
	static constexpr int kWindowHeight = 320;       ///< 框高（展开态）
	static constexpr int kCaptionHeight = 32;       ///< 标题条高（= CaptionBar::kDefaultHeight）
	static constexpr int kEntryButtonWidth = 96;    ///< 入口按钮宽
	static constexpr int kCollapseButtonSize = 32;  ///< 折叠按钮（正方形）
	static constexpr int kBarPad = 8;               ///< 标题条内边距
	static constexpr int kBoxRadius = 12;           ///< 框背景圆角
	static constexpr int kPopupWidth = 180;         ///< 浮层宽
	static constexpr int kPopupRowHeight = 24;      ///< 浮层行高
	static constexpr int kPopupRowPad = 4;          ///< 浮层行侧留白
	static constexpr int kPopupRadius = 8;          ///< 浮层圆角
	static constexpr int kBadgeDiameter = 18;       ///< 徽标圆直径
	static constexpr int kCascadeStep = 28;         ///< 级联偏移步长（§9-6：一律级联）
	static constexpr int kFakeContentRows = 5;      ///< 内容区假数据行数
};

/// @brief 级联偏移（§9-6 简化拍板：不判重叠、一律级联——源框当前位置 + 步长）。
/// @details 纯函数——可无头测试（T-M1-2②）。
inline Rect CascadedPlacement(const Rect& sourceBounds) {
	return Rect{
		sourceBounds.x + Metrics::kCascadeStep,
		sourceBounds.y + Metrics::kCascadeStep,
		sourceBounds.width,
		sourceBounds.height};
}

}   // namespace ECDI::DesktopNest
