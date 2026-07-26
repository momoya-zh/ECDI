#pragma once

#include "KeyEvent.h"

class KeyDownEvent : public KeyEvent{

public:

	static EventType StaticType(){

		return EventType::KeyDown;

	}

	EventType GetType() const noexcept override{

		return StaticType();

	}

public:

	KeyDownEvent(
		Window* window,
		KeyCode keyCode
	): KeyEvent(window, keyCode){

	}

};