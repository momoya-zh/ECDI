#pragma once

#include "MouseEvent.h"
#include "MouseButton.h"


class MouseButtonEvent : public MouseEvent{

protected:

	MouseButtonEvent(
		Window* window,
		int mouseX,
		int mouseY,
		MouseButton button
	):MouseEvent(window, mouseX, mouseY),m_button(button){

	}


public:

	MouseButton GetButton() const noexcept{

		return m_button;

	}


private:

	MouseButton m_button;

};