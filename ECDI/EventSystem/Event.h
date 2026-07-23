#pragma once

#include "EventType.h"

class Window;

class Event{
public:

	virtual ~Event() = default;

	Window* GetWindow() const noexcept{
		return m_window;
	}

	virtual EventType GetType() const = 0;

protected:

	explicit Event(Window* window): m_window(window) {

	}

protected:

	Window* m_window=nullptr;
};

