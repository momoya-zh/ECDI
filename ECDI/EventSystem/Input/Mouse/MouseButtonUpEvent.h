#pragma once

#include"MouseButtonEvent.h"

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