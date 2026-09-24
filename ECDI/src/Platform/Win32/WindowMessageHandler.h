#pragma once

#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/Platform/PlatformWindowHost.h"   // Host& 成员/构造参数——完整定义

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护：DrawText → DrawTextW 会污染公共头的方法名（与 GDIBackend.h 同款）
#endif

#include <optional>

namespace ECDI{

class Window;   // 保留（Handle 参数 Window*——仅前置声明，cpp 不再 include Window.h）

/// @brief Win32 消息 → Framework Event 翻译器（7.1.2：纯翻译 + 经 Host 派发）
/// @details
/// 将 Win32 的 HWND / UINT / WPARAM / LPARAM 翻译为类型安全的 Framework Event，
/// 经 m_host.OnEvent() 派发（Dispatch 一级：翻译器 → 框架契约 → Window → 应用层）。
///
/// 职责边界（7.1.2 GPT 三轮方案 B——职责纯粹化）：
/// - 翻译：Win32 消息 → Framework Event（类型安全）
/// - 派发：m_host.OnEvent()（不再直连应用层入口——C1）
/// - ⚠️ WM_IME_* 已移出（7.1.2 方案 B）：IME 属输入法子系统（TSF/IMM/候选窗），
///   由 Win32PlatformWindow::HandleMessage 状态同步区处理（host.OnIMEComposition()）
/// - 不负责：Widget HitTest、事件传播、窗口内部状态维护
class WindowMessageHandler
{
public:
	explicit WindowMessageHandler(PlatformWindowHost& host) noexcept;

	/// @brief 处理一条 Win32 消息
	/// @return nullopt = 调用方走 DefWindowProc；有值 = 调用方用该值返回
	std::optional<LRESULT> Handle(
		Window* window,
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam
	);

	/// @brief 设置当前窗口 DPI（Phase 20 Q6——**平台翻译器持有的正常状态**，非测试缝）
	/// @param dpi 当前窗口 DPI（`GetDpiForWindow` 的返回值；`<= 0` ⇒ 视作 96）
	/// @details 由 `Win32PlatformWindow` 在**构造末尾**与 **`WM_DPICHANGED`** 时写入；
	///          换算用它把物理坐标折成 DIP（`PixelsToDip(·, m_dpi)`）。
	/// @note ★ 为什么是「正常状态」而非「测试缝」：本类是 **Win32 专有件**——换平台时
	///       整个组件被替换 ⇒ 持有当前窗口 DPI 是它的**职责**；且默认 96 让既有用例
	///       行为**逐位不变**（`dpi == 96` ⇒ 换算恒等）。
	void SetDpi(int dpi) noexcept{ m_dpi = (dpi > 0) ? dpi : 96; }

private:
	PlatformWindowHost& m_host;	///< 框架契约（非拥有；7.1.2 替代应用层指针）

	/// @brief 当前窗口 DPI（Phase 20；默认 96 = 恒等换算 ⇒ 既有行为零回归）
	int m_dpi = 96;

	/// @brief 等待配对的 UTF-16 高位代理（0 = 无；代理对组合状态机的实例状态）
	wchar_t m_pendingHighSurrogate = 0;

	/// @brief 将 Win32 鼠标消息翻译为框架层 MouseButton 枚举
	static MouseButton TranslateMouseButton(UINT message, WPARAM wParam);

	/// @brief 将 Win32 虚拟键码翻译为框架层 KeyCode 枚举
	/// @details 通过 lParam 位区分左右修饰键（Shift/Ctrl/Alt）和主键盘/小键盘 Enter
	static KeyCode TranslateKeyCode(WPARAM wParam, LPARAM lParam);

	/// @brief 消费一个 UTF-16 码元，可能产出完整 Unicode 码点
	/// @details 代理对状态机：高位代理暂存等待低位；低位与暂存组合成码点；
	/// 孤立低位代理丢弃（框架不负责 Unicode error recovery）。
	/// @return nullopt = 需要更多码元或非法序列（不产生事件）；有值 = 完整码点
	std::optional<char32_t> ConsumeCodeUnit(wchar_t unit);
};

}
