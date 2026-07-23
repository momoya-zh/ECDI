#pragma once

#include "Event.h"


class WindowEvent : public Event{
public:


protected:

	explicit WindowEvent(Window* window): Event(window) {
	
	};
};
