#pragma once

#include <optional>
#include <Windows.h>

#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"

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

	/// @brief 将 Win32 鼠标消息翻译为框架层 MouseButton 枚举
	static MouseButton TranslateMouseButton(UINT message, WPARAM wParam);

	/// @brief 将 Win32 虚拟键码翻译为框架层 KeyCode 枚举
	/// @details 通过 lParam 位区分左右修饰键（Shift/Ctrl/Alt）和主键盘/小键盘 Enter
	static KeyCode TranslateKeyCode(WPARAM wParam, LPARAM lParam);
};
