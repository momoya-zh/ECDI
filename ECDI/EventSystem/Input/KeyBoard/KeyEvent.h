#pragma once

#include "Input/InputEvent.h"
#include "KeyCode.h"

class KeyEvent : public InputEvent{

public:

	KeyCode GetKeyCode() const noexcept{

		return m_keyCode;

	}

protected:

	KeyEvent(
		Window* window,
		KeyCode keyCode
	): InputEvent(window),m_keyCode(keyCode)	{
	
	}

private:

	KeyCode m_keyCode;

};