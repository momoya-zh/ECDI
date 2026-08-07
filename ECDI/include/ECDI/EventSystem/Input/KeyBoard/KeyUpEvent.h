#pragma once

#include "KeyEvent.h"

/// @brief 键盘按键释放事件
/// @details
/// 由 WM_KEYUP / WM_SYSKEYUP 翻译而来。
/// 携带 KeyCode，通过 StaticType() 用于 EventDispatcher 分派。
class KeyUpEvent : public KeyEvent {

public:

	static EventType StaticType() {

		return EventType::KeyUp;

	}

	EventType GetType() const noexcept override {

		return StaticType();

	}

public:

	KeyUpEvent(
		Window* window,
		KeyCode keyCode
	) : KeyEvent(window, keyCode) {

	}

};
