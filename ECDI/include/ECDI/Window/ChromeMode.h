#pragma once

namespace ECDI{

/// @brief 窗口 chrome 形态（Phase 12 R1）
/// @details 只描述「系统边框/标题栏是否保留」这一件事——
/// 标题栏高度、缩放热区宽度是**独立可调参数**（见 Window::SetCaptionHeight / SetResizeInset），
/// 不塞进枚举（否则组合爆炸：Borderless 小标题栏 / Borderless 大标题栏…）。
enum class ChromeMode{

	/// @brief 系统标题栏 + 边框（默认——与 Phase 12 之前行为完全一致）
	Normal = 0,

	/// @brief 无边框：客户区扩满整窗，标题栏/边框由应用自绘
	/// @details 保留底层 WS_OVERLAPPEDWINDOW 样式——系统动画 / Aero Snap /
	/// 最小化动画 / Alt+Space 系统菜单全部保留（技术路线见需求 §2）。
	Borderless
};

}
