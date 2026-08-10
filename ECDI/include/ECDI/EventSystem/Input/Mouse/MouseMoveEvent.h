#pragma once

#include "MouseEvent.h"

namespace ECDI{

/// @brief 鼠标移动事件
/// @details
/// 由 WM_MOUSEMOVE 翻译而来。
class MouseMoveEvent : public MouseEvent{

public:

	static EventType StaticType() noexcept {

		return EventType::MouseMove;

	}

	EventType GetType() const noexcept override{

		return StaticType();

	}

	MouseMoveEvent(
		Window* window,
		int mouseX,
		int mouseY
	):MouseEvent(window, mouseX, mouseY){

	}

};

}
