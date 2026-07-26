#pragma once

#include "KeyEvent.h"

class KeyUpEvent : public KeyEvent {

public:

	static EventType StaticType() {

		return EventType::KeyUp;

	}

	EventType GetType() const noexcept override {

		return StaticType();

	}

public:

	KeyUpEvent(
		Window* window,
		KeyCode keyCode
	) : KeyEvent(window, keyCode) {

	}

};