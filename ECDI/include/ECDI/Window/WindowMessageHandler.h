#pragma once

#include <optional>
#include <Windows.h>

#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"

namespace ECDI{

class Window;
class Application;

/// @brief Win32 消息 → Framework Event 翻译器
/// @details
/// 负责将 Win32 的 HWND / UINT / WPARAM / LPARAM 翻译为类型安全的 Framework Event，
/// 并通过 Application::OnEvent() 派发给 EventRouter。
///
/// 职责边界：
/// - 翻译：Win32 消息 → Framework Event（类型安全）
/// - 派发：调用 Application::OnEvent()
/// - 不负责：Widget HitTest、事件传播、窗口内部状态维护
class WindowMessageHandler
{
public:
	explicit WindowMessageHandler(Application* app) noexcept;

	/// @brief 处理一条 Win32 消息
	/// @return nullopt = 调用方走 DefWindowProc；有值 = 调用方用该值返回
	std::optional<LRESULT> Handle(
		Window* window,
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam
	);

private:
	Application* m_application;

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
