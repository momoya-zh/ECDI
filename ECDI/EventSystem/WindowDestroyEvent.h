#pragma once

#include "WindowEvent.h"

class WindowDestroyedEvent : public WindowEvent{
public:

	explicit WindowDestroyedEvent(Window* window):WindowEvent(window){

	}

	EventType GetType() const override
	{
		return StaticType();
	}
	static EventType StaticType()
	{
		return EventType::WindowDestroyed;
	}
};