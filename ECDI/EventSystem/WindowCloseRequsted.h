#pragma once

#include "WindowEvent.h"


class WindowCloseRequestedEvent : public WindowEvent{
public:

	explicit WindowCloseRequestedEvent(Window* window):WindowEvent(window){
	}


	EventType GetType() const override{
		return StaticType();
	}


	static EventType StaticType(){
		return EventType::WindowCloseRequested;
	}
};