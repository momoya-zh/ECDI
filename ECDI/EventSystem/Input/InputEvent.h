#pragma once

#include"Event.h"

class InputEvent :public Event {

protected:

	explicit InputEvent(Window* window):Event(window){

	}

};