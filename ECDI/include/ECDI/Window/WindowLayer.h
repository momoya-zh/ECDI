#pragma once

namespace ECDI{

/// @brief 窗口层级档位（Phase 12 R10——桌面常驻应用前置）
/// @details **本枚举只描述「窗口所处层级」这一语义事实**，
/// 与任何平台实现手段（WorkerW / Progman / SetParent / SetWinEventHook / SetWindowPos）
/// **完全解耦**——平台层如何达成该层级是实现的自由，公共 API 不做任何绑定。
/// 档位按 **Win+D 行为** 区分：Normal / Bottom 会被 Win+D 一起隐藏，Desktop 不会
/// （决策依据见 docs/desktopnest-roadmap.md §1.1）。
enum class WindowLayer{

	/// @brief 默认：普通窗口层，无特殊 z 序处理
	Normal = 0,

	/// @brief 普通窗口层的**底部位置**
	/// @details 语义为「框架**持续维护**该窗口处于非置顶普通窗口中的底部位置」——
	/// ⚠️ **不承诺**「绝对位于最底层」：Windows z 序是动态的，受激活、拥有关系、
	/// 系统策略以及第三方程序主动 SetWindowPos 影响；外部行为造成的**瞬时** z 序
	/// 变化不属于本契约的违反（框架会在下一次 z 序变更机会中重新维护底部位置）。
	/// Win+D 时本档窗口**与普通窗口一起被隐藏**。
	Bottom,

	/// @brief 桌面驻留层：被应用窗口覆盖，且 **Win+D 后仍保持可见**
	/// @details **实现已落地**（Phase 16，2026-09-19）——Win32 实现 = 窗口以
	/// `GetWindow(桌面窗口, GW_HWNDPREV)` 为目标位置持续维护「紧贴桌面窗口正上方」，
	/// 并移除 `WS_MINIMIZEBOX`（「显示桌面」只最小化**可最小化**窗口）。
	/// **API 承诺与平台能力解耦**：本枚举只描述层级语义，与之无关的平台手段
	/// （`WS_POPUP` / `WorkerW` 挂载 / `SetParent`）一律不进入公共契约。
	/// ★ 语义状态 ≠ 实现路径（详设 D-DESK-1）：平台实现无论经哪条路径执行，
	/// 本档位的请求语义恒为 Desktop（未来若新增查询 API，必须如实返回 Desktop）。
	Desktop
};

}
