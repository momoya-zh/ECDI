#pragma once

#include"MouseEvent.h"

class MouseWheelEvent :public MouseEvent {
	
public:

	static EventType StaticType()noexcept {

		return EventType::MouseWheel;

	}

	EventType GetType()const noexcept override {

		return StaticType();

	}

	MouseWheelEvent(
		Window* window,
		int mouseX,
		int mouseY,
		int delta
	) :MouseEvent(window, mouseX, mouseY), m_delta(delta) {

	}

	int GetDelta() const noexcept{

		return m_delta;
	
	}

private:
	int m_delta;
};