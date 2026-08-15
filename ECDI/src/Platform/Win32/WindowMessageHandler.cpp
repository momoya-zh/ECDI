#include "ECDI/Platform/Win32/WindowMessageHandler.h"

#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"
#include "ECDI/EventSystem/Window/WindowDestroyEvent.h"
#include "ECDI/EventSystem/Window/WindowResizedEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/Core/ECDIAssert.h"

#include <Windows.h>
#include <windowsx.h>// GET_X_LPARAM / GET_Y_LPARAM / GET_WHEEL_DELTA_WPARAM 等宏

namespace ECDI{

namespace{   // 匿名 namespace：翻译器内部辅助（不暴露）

/// @brief 翻译当前修饰键状态（5.5.2；平台翻译器内查询 GetKeyState——分层允许
/// "Win32 负责翻译"本职；各平台实现不同（X11/Wayland/macOS），不抽公共函数）
KeyModifier TranslateModifier(){
	KeyModifier m = KeyModifier::None;
	const auto AddIfDown = [&m](int vk, KeyModifier mod){
		if (GetKeyState(vk) & 0x8000)
			m = m | mod;
	};
	AddIfDown(VK_SHIFT,   KeyModifier::Shift);
	AddIfDown(VK_CONTROL, KeyModifier::Ctrl);
	AddIfDown(VK_MENU,    KeyModifier::Alt);
	return m;
}

}

WindowMessageHandler::WindowMessageHandler(PlatformWindowHost& host) noexcept: m_host(host){}

std::optional<LRESULT> WindowMessageHandler::Handle(
	Window* window,
	HWND hwnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam
){
	switch (msg){

	// ── 窗口生命周期 ──────────────────────────────────

	case WM_CLOSE: {
		// 用户点击关闭按钮或 Alt+F4 → 翻译为 WindowCloseRequestedEvent
		WindowCloseRequestedEvent event(window);

		m_host.OnEvent(event);   // 7.1.2：经 Host 派发（不再直连应用层入口）

		return 0;
	}

	case WM_DESTROY: {

		// 窗口即将销毁 → 翻译为 WindowDestroyedEvent
		WindowDestroyedEvent event(window);

		m_host.OnEvent(event);

		return 0;
	}

	case WM_SIZE: {
		// 窗口大小变化 → 翻译为 WindowResizedEvent
		const int width = LOWORD(lParam);

		const int height = HIWORD(lParam);

		WindowResizedEvent event(
			window,
			width,
			height
		);

		m_host.OnEvent(event);

		// 返回 nullopt 让调用方继续走 DefWindowProc（内部状态已在 Win32PlatformWindow 中同步）
		return std::nullopt;
	}

	// ── 鼠标事件 ───────────────────────────────────────

	case WM_MOUSEMOVE: {

		int x = GET_X_LPARAM(lParam);
		int y = GET_Y_LPARAM(lParam);

		MouseMoveEvent event(
			window,
			x,
			y
		);

		m_host.OnEvent(event);

		return std::nullopt;
	}

	// 鼠标按键按下（合并处理左/右/中/扩展键）
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN: {

		MouseButtonDownEvent event(
			window,
			GET_X_LPARAM(lParam),
			GET_Y_LPARAM(lParam),
			TranslateMouseButton(msg, wParam)
		);

		m_host.OnEvent(event);

		return std::nullopt;
	}

	// 鼠标按键释放
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP: {

		MouseButtonUpEvent event(
			window,
			GET_X_LPARAM(lParam),
			GET_Y_LPARAM(lParam),
			TranslateMouseButton(msg, wParam)
		);

		m_host.OnEvent(event);

		return std::nullopt;
	}

	// 鼠标滚轮（注意：lParam 是屏幕坐标，需转为客户区坐标）
	case WM_MOUSEWHEEL:
	{
		POINT point{};

		point.x = GET_X_LPARAM(lParam);
		point.y = GET_Y_LPARAM(lParam);

		ScreenToClient(hwnd, &point);

		const int delta = GET_WHEEL_DELTA_WPARAM(wParam);

		MouseWheelEvent event(
			window,
			point.x,
			point.y,
			delta
		);

		m_host.OnEvent(event);

		return std::nullopt;
	}

	// ── 键盘事件 ───────────────────────────────────────

	// WM_SYSKEYDOWN/UP 与 WM_KEYDOWN/UP fall-through 合并
	// break 到 DefWindowProc 不 return 0（保留 Alt+F4/Alt+Tab 系统行为）
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	{
		KeyDownEvent event(
			window,
			TranslateKeyCode(
				wParam,
				lParam
			),
			TranslateModifier()
		);

		m_host.OnEvent(event);

		return std::nullopt;
	}

	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		KeyUpEvent event(
			window,
			TranslateKeyCode(
				wParam,
				lParam
			),
			TranslateModifier()
		);

		m_host.OnEvent(event);

		return std::nullopt;
	}

	// 字符输入：WM_CHAR/WM_SYSCHAR 的 wParam 是 UTF-16 码元流。
	// 共享代理对状态（本质都是 UTF-16 input unit stream，不维护两套状态）
	case WM_CHAR:
	case WM_SYSCHAR:{
		// UTF-16 码元 → 代理对组合 → 完整 Unicode 码点；nullopt 时无事件产出
		if (auto codepoint = ConsumeCodeUnit(static_cast<wchar_t>(wParam))){

			CharInputEvent event(window, *codepoint);

			m_host.OnEvent(event);
		}

		return std::nullopt;
	}

	// ⚠️ WM_IME_* 已移出（7.1.2 方案 B）：IME 属输入法子系统（TSF/IMM/候选窗），
	// 由 Win32PlatformWindow::HandleMessage 状态同步区处理（host.OnIMEComposition()）
	// ——翻译器职责纯粹化（Translate → Event → Host）

	}

	return std::nullopt;
}

std::optional<char32_t> WindowMessageHandler::ConsumeCodeUnit(wchar_t unit){

	// 高位代理 0xD800-0xDBFF：暂存等待低位配对
	if (unit >= 0xD800 && unit <= 0xDBFF){

		// 若已有未配对高位：替换（旧序列非法，简单丢弃——不负责 error recovery）
		m_pendingHighSurrogate = unit;

		return std::nullopt;
	}

	// 低位代理 0xDC00-0xDFFF：与暂存高位组合成完整码点
	if (unit >= 0xDC00 && unit <= 0xDFFF){

		if (m_pendingHighSurrogate != 0){

			const char32_t codepoint =
				0x10000
				+ (static_cast<char32_t>(m_pendingHighSurrogate) - 0xD800) * 0x400
				+ (static_cast<char32_t>(unit) - 0xDC00);

			m_pendingHighSurrogate = 0;

			return codepoint;
		}

		// 孤立低位代理：非法输入，丢弃（不负责 Unicode error recovery）
		return std::nullopt;
	}

	// 普通 BMP 字符：清空待配对状态，独立事件
	m_pendingHighSurrogate = 0;

	return static_cast<char32_t>(unit);
}

MouseButton WindowMessageHandler::TranslateMouseButton(UINT message,WPARAM wParam) {

	switch (message) {

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:

		return MouseButton::Left;

	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:

		return MouseButton::Right;

	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:

		return MouseButton::Middle;

	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP: {

		WORD button = GET_XBUTTON_WPARAM(wParam);

		if (button == XBUTTON1)
			return MouseButton::X1;

		if (button == XBUTTON2)
			return MouseButton::X2;

		break;
	}

	}

	FRAMEWORK_ASSERT(false);

	return MouseButton::Left;

}

KeyCode WindowMessageHandler::TranslateKeyCode(WPARAM wParam,LPARAM lParam){
	switch (wParam)
	{
		// ── Letters ──────────────────────────────────────
	case 'A': return KeyCode::A;
	case 'B': return KeyCode::B;
	case 'C': return KeyCode::C;
	case 'D': return KeyCode::D;
	case 'E': return KeyCode::E;
	case 'F': return KeyCode::F;
	case 'G': return KeyCode::G;
	case 'H': return KeyCode::H;
	case 'I': return KeyCode::I;
	case 'J': return KeyCode::J;
	case 'K': return KeyCode::K;
	case 'L': return KeyCode::L;
	case 'M': return KeyCode::M;
	case 'N': return KeyCode::N;
	case 'O': return KeyCode::O;
	case 'P': return KeyCode::P;
	case 'Q': return KeyCode::Q;
	case 'R': return KeyCode::R;
	case 'S': return KeyCode::S;
	case 'T': return KeyCode::T;
	case 'U': return KeyCode::U;
	case 'V': return KeyCode::V;
	case 'W': return KeyCode::W;
	case 'X': return KeyCode::X;
	case 'Y': return KeyCode::Y;
	case 'Z': return KeyCode::Z;


		// ── Digits ───────────────────────────────────────
	case '0': return KeyCode::Digit0;
	case '1': return KeyCode::Digit1;
	case '2': return KeyCode::Digit2;
	case '3': return KeyCode::Digit3;
	case '4': return KeyCode::Digit4;
	case '5': return KeyCode::Digit5;
	case '6': return KeyCode::Digit6;
	case '7': return KeyCode::Digit7;
	case '8': return KeyCode::Digit8;
	case '9': return KeyCode::Digit9;


		// ── Function Keys ────────────────────────────────
	case VK_F1:  return KeyCode::F1;
	case VK_F2:  return KeyCode::F2;
	case VK_F3:  return KeyCode::F3;
	case VK_F4:  return KeyCode::F4;
	case VK_F5:  return KeyCode::F5;
	case VK_F6:  return KeyCode::F6;
	case VK_F7:  return KeyCode::F7;
	case VK_F8:  return KeyCode::F8;
	case VK_F9:  return KeyCode::F9;
	case VK_F10: return KeyCode::F10;
	case VK_F11: return KeyCode::F11;
	case VK_F12: return KeyCode::F12;
	case VK_F13: return KeyCode::F13;
	case VK_F14: return KeyCode::F14;
	case VK_F15: return KeyCode::F15;
	case VK_F16: return KeyCode::F16;
	case VK_F17: return KeyCode::F17;
	case VK_F18: return KeyCode::F18;
	case VK_F19: return KeyCode::F19;
	case VK_F20: return KeyCode::F20;
	case VK_F21: return KeyCode::F21;
	case VK_F22: return KeyCode::F22;
	case VK_F23: return KeyCode::F23;
	case VK_F24: return KeyCode::F24;

		// ── Modifier Keys（通过 lParam 位区分左右）──────
	case VK_SHIFT:{

		UINT scanCode = (lParam >> 16) & 0xFF;
		UINT vk = MapVirtualKey(scanCode, MAPVK_VSC_TO_VK_EX);
		return vk == VK_RSHIFT ?
			KeyCode::RightShift :
			KeyCode::LeftShift;

	}

	case VK_CONTROL:{

		bool extended = (lParam & (1 << 24)) != 0;
		return extended ?
			KeyCode::RightCtrl :
			KeyCode::LeftCtrl;

	}

	case VK_MENU:{

		bool extended = (lParam & (1 << 24)) != 0;
		return extended ?
			KeyCode::RightAlt :
			KeyCode::LeftAlt;

	}

	// ── Win Keys ────────────────────────────────────────
	case VK_LWIN:  return KeyCode::LeftWin;
	case VK_RWIN:  return KeyCode::RightWin;

		// Enter（主键盘 Enter / 小键盘 Enter，通过 lParam extended bit 区分）
	case VK_RETURN:{

		bool extended = (lParam & (1 << 24)) != 0;
		return extended ?
			KeyCode::NumpadEnter :
			KeyCode::Enter;

	}

	// ── Numpad Digits ───────────────────────────────────
	case VK_NUMPAD0: return KeyCode::Numpad0;
	case VK_NUMPAD1: return KeyCode::Numpad1;
	case VK_NUMPAD2: return KeyCode::Numpad2;
	case VK_NUMPAD3: return KeyCode::Numpad3;
	case VK_NUMPAD4: return KeyCode::Numpad4;
	case VK_NUMPAD5: return KeyCode::Numpad5;
	case VK_NUMPAD6: return KeyCode::Numpad6;
	case VK_NUMPAD7: return KeyCode::Numpad7;
	case VK_NUMPAD8: return KeyCode::Numpad8;
	case VK_NUMPAD9: return KeyCode::Numpad9;

		// ── Numpad Operators ─────────────────────────────
	case VK_ADD:      return KeyCode::NumpadAdd;
	case VK_SUBTRACT: return KeyCode::NumpadSubtract;
	case VK_MULTIPLY: return KeyCode::NumpadMultiply;
	case VK_DIVIDE:   return KeyCode::NumpadDivide;
	case VK_DECIMAL:  return KeyCode::NumpadDecimal;

		// ── Editing Keys ─────────────────────────────────
	case VK_ESCAPE:  return KeyCode::Escape;
	case VK_BACK:    return KeyCode::Backspace;
	case VK_TAB:     return KeyCode::Tab;
	case VK_SPACE:   return KeyCode::Space;
	case VK_INSERT:  return KeyCode::Insert;
	case VK_DELETE:  return KeyCode::Delete;

		// ── Navigation Keys ──────────────────────────────
	case VK_LEFT:  return KeyCode::Left;
	case VK_RIGHT: return KeyCode::Right;
	case VK_UP:    return KeyCode::Up;
	case VK_DOWN:  return KeyCode::Down;
	case VK_HOME:  return KeyCode::Home;
	case VK_END:   return KeyCode::End;
	case VK_PRIOR: return KeyCode::PageUp;
	case VK_NEXT:  return KeyCode::PageDown;

		// ── Lock Keys ────────────────────────────────────
	case VK_CAPITAL: return KeyCode::CapsLock;
	case VK_NUMLOCK: return KeyCode::NumLock;
	case VK_SCROLL:  return KeyCode::ScrollLock;

		// ── System Keys ──────────────────────────────────
	case VK_APPS:     return KeyCode::Menu;
	case VK_SNAPSHOT: return KeyCode::PrintScreen;
	case VK_PAUSE:    return KeyCode::Pause;

		// ── Symbol Keys（OEM 格式，位置因键盘布局而异）───
	case VK_OEM_1:      return KeyCode::Semicolon;
	case VK_OEM_2:      return KeyCode::Slash;
	case VK_OEM_3:      return KeyCode::Backquote;
	case VK_OEM_4:      return KeyCode::LeftBracket;
	case VK_OEM_5:      return KeyCode::Backslash;
	case VK_OEM_6:      return KeyCode::RightBracket;
	case VK_OEM_7:      return KeyCode::Apostrophe;
	case VK_OEM_PLUS:   return KeyCode::Equals;
	case VK_OEM_MINUS:  return KeyCode::Minus;
	case VK_OEM_COMMA:  return KeyCode::Comma;
	case VK_OEM_PERIOD: return KeyCode::Period;

	}

	return KeyCode::Unknown;
}

}
