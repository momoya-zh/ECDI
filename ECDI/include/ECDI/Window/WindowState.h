#pragma once

namespace ECDI{

/// @brief 窗口状态（Phase 12 R7）
/// @details 归 Window 领域而非 Event 领域——本枚举描述「窗口**处于什么状态**」
/// 这一基础事实，`WindowStateChangedEvent` 只是它的变化通知机制。
/// 依赖方向：`WindowStateChangedEvent` → `WindowState`（反向不成立）。
enum class WindowState{

	restored = 0,   ///< 还原态（普通态——最小化/最大化均未生效；也是窗口创建后的初始态）
	minimized,      ///< 已最小化
	maximized       ///< 已最大化
};

}
