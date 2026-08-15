#pragma once

#include "ECDI/EventSystem/Input/KeyBoard/KeyEvent.h"

namespace ECDI{

/// @brief 键盘按键按下事件
/// @details
/// 由 WM_KEYDOWN / WM_SYSKEYDOWN 翻译而来。
/// 携带 KeyCode，通过 StaticType() 用于 EventDispatcher 分派。
class KeyDownEvent : public KeyEvent{

public:

	static EventType StaticType(){

		return EventType::KeyDown;

	}

	EventType GetType() const noexcept override{

		return StaticType();

	}

public:

	KeyDownEvent(
		Window* window,
		KeyCode keyCode,
		KeyModifier modifier
	): KeyEvent(window, keyCode, modifier){

	}

};

}
