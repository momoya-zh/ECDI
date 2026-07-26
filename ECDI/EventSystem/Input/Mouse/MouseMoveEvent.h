#pragma once

#include "MouseEvent.h"


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