#pragma once

namespace ECDI{

/// @brief 框架层键盘按键枚举（与 Win32 VK_* 解耦）
/// @details
/// 覆盖标准 VK_* 中所有键盘消息能传递的键。
/// 不包含 Fn/Power/多媒体键（这些键不产生 WM_KEYDOWN/WM_CHAR）。
/// 修饰键（Shift/Ctrl/Alt）通过 lParam 位区分左右。
enum class KeyCode{
	Unknown,

	// ── 字母键 ──────────────────────────────────────
	A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,

	// ── 数字键（主键盘上方）──────────────────────────
	Digit0,
	Digit1,
	Digit2,
	Digit3,
	Digit4,
	Digit5,
	Digit6,
	Digit7,
	Digit8,
	Digit9,

	// ── 功能键 ──────────────────────────────────────
	F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,F13,F14,F15,F16,F17,F18,F19,F20,F21,F22,F23,F24,

	// ── 修饰键（区分左右）────────────────────────────
	LeftShift,
	RightShift,
	LeftCtrl,
	RightCtrl,
	LeftAlt,
	RightAlt,
	LeftWin,
	RightWin,

	// ── 编辑键 ──────────────────────────────────────
	Enter,
	Escape,
	Backspace,
	Tab,
	Space,
	Insert,
	Delete,

	// ── 导航键 ──────────────────────────────────────
	Left,
	Right,
	Up,
	Down,
	Home,
	End,
	PageUp,
	PageDown,

	// ── 锁定键 ──────────────────────────────────────
	CapsLock,
	NumLock,
	ScrollLock,

	// ── 系统键 ──────────────────────────────────────
	PrintScreen,
	Pause,
	Menu,

	// ── 符号键（OEM 格式，位置因键盘布局而异）────────
	Semicolon,
	Apostrophe,
	Comma,
	Period,
	Slash,
	Backslash,
	Backquote,
	Minus,
	Equals,
	LeftBracket,
	RightBracket,

	// ── 小键盘 ──────────────────────────────────────
	Numpad0,
	Numpad1,
	Numpad2,
	Numpad3,
	Numpad4,
	Numpad5,
	Numpad6,
	Numpad7,
	Numpad8,
	Numpad9,

	NumpadAdd,
	NumpadSubtract,
	NumpadMultiply,
	NumpadDivide,
	NumpadDecimal,
	NumpadEnter,

};

}
