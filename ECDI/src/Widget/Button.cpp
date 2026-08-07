#include"ECDI/Widget/Button.h"
#include"ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include"ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"

#include<Windows.h>
#include<utility>

Button::Button(const std::wstring& text):m_text(text){

}

Button::Button(std::wstring&& text):m_text(std::move(text)){

}

void Button::SetText(const std::wstring& text){

	m_text = text;

}

void Button::SetText(std::wstring&& text){

	m_text = std::move(text);

}

const std::wstring& Button::GetText() const noexcept{

	return m_text;

}

void Button::OnMouseButtonDown(const MouseButtonDownEvent&){
	
	m_pressed = true;

}

void Button::OnMouseButtonUp(const MouseButtonUpEvent&){
	
	if (m_pressed){

		m_pressed = false;

		OnClick();
	}
}

void Button::OnClick(){}

void Button::OnPaint(
	HDC hdc,
	int x,
	int y)
{
	RECT rect{

		x,

		y,

		x + GetWidth(),

		y + GetHeight()

	};

	HBRUSH brush = CreateSolidBrush(RGB(80,120,220));
	FillRect(hdc,&rect,brush);
	DeleteObject(brush);
}