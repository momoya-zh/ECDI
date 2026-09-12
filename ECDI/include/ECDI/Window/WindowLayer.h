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
	/// @details ⚠️ 实现路线待 spike 验证；spike 未通过前此档位不承诺可用——
	/// 调用后退化为 Bottom 语义并记 Warning 日志。
	/// **API 承诺与当前平台能力刻意解耦**：枚举值保留，未来 Windows 版本可行时
	/// 只需替换实现，公共 API 零变更。
	/// ★ 语义状态 ≠ 实现路径（详设 D-DESK-1）：降级的是「实现如何执行」（当前按
	/// Bottom 路径执行），**不是「状态是什么」**——本档位的请求语义恒为 Desktop，
	/// 未来若新增查询 API（如 GetWindowLayer），spike 未通过时必须仍返回 Desktop。
	Desktop
};

}
