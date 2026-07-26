#pragma once
#include "WindowEvent.h"

class WindowResizedEvent : public WindowEvent{

public:

	WindowResizedEvent(
		Window* window,
		int width,
		int height):
		WindowEvent(window),
		m_width(width),
		m_height(height){

	}

	static EventType StaticType(){

		return EventType::WindowResized;

	}

	EventType GetType() const override{

		return StaticType();

	}


	int GetWidth() const noexcept{

		return m_width;

	}


	int GetHeight() const noexcept{

		return m_height;

	}


private:

	int m_width;

	int m_height;
};