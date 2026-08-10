#pragma once

#include"MouseButtonEvent.h"
namespace ECDI
{

/// @brief 鼠标按键释放事件
/// @details
/// 由 WM_LBUTTONUP / WM_RBUTTONUP / WM_MBUTTONUP / WM_XBUTTONUP 翻译而来。
class MouseButtonUpEvent :public MouseButtonEvent {

public:

	MouseButtonUpEvent(
		Window* window,
		int mouseX,
		int mouseY,
		MouseButton button
	) :MouseButtonEvent(window, mouseX, mouseY, button) {

	}

	static EventType StaticType() noexcept{

		return EventType::MouseButtonUp;

	}

	EventType GetType() const noexcept override{

		return StaticType();
	
	}

};

}
