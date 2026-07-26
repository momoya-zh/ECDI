#pragma once

enum class KeyCode
{
	Unknown,

	// Letters
	A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,

	// Digits
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

	// Function Keys
	F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,F13,F14,F15,F16,F17,F18,F19,F20,F21,F22,F23,F24,

	// Modifier Keys
	LeftShift,
	RightShift,
	LeftCtrl,
	RightCtrl,
	LeftAlt,
	RightAlt,
	LeftWin,
	RightWin,

	// Editing Keys
	Enter,
	Escape,
	Backspace,
	Tab,
	Space,
	Insert,
	Delete,

	// Navigation Keys
	Left,
	Right,
	Up,
	Down,
	Home,
	End,
	PageUp,
	PageDown,

	// Lock Keys
	CapsLock,
	NumLock,
	ScrollLock,

	// System Keys
	PrintScreen,
	Pause,
	Menu,

	// Symbol Keys
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

	// Numpad
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