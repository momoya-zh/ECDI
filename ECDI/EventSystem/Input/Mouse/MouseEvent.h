#pragma once

#include"Input/InputEvent.h"
class MouseEvent :public InputEvent {

protected:

	MouseEvent(
		Window* window,
		int mouseX,
		int mouseY): InputEvent(window),m_mouseX(mouseX),m_mouseY(mouseY){

	}

public:

	int GetMouseX() const noexcept{

		return m_mouseX;

	}

	int GetMouseY() const noexcept{

		return m_mouseY;

	}

private:

	int m_mouseX;

	int m_mouseY;


 };