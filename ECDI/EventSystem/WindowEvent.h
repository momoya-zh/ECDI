#pragma once

#include "Event.h"

class Window;

class WindowEvent : public Event{
public:

	Window* GetWindow() const noexcept {
		return m_window;
	}

protected:

	Window* m_window;

	explicit WindowEvent(Window* window):m_window(window) {
	
	};
};
