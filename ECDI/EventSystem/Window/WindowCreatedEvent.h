#pragma once

#include "WindowEvent.h"


class WindowCreatedEvent : public WindowEvent{

public:

	explicit WindowCreatedEvent(Window* window):WindowEvent(window){

	}


	EventType GetType() const override{

		return StaticType();

	}


	static EventType StaticType(){

		return EventType::WindowCreated;

	}

};