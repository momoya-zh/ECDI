#pragma once

#include "MouseButtonEvent.h"

/// @brief 鼠标按键按下事件
/// @details
/// 由 WM_LBUTTONDOWN / WM_RBUTTONDOWN / WM_MBUTTONDOWN / WM_XBUTTONDOWN 翻译而来。
class MouseButtonDownEvent : public MouseButtonEvent{

public:

	MouseButtonDownEvent(
		Window* window,
		int mouseX,
		int mouseY,
		MouseButton button
	):MouseButtonEvent(window,mouseX,mouseY,button){

	}


	static EventType StaticType() noexcept{

		return EventType::MouseButtonDown;

	}


	EventType GetType() const noexcept override{

		return StaticType();

	}

};
