#pragma once
#include "MouseButtonEvent.h"

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