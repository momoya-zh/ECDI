#include"ECDI/Widget/Button.h"
#include"ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include"ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"

#include<utility>

namespace ECDI{


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

void Button::OnPaint(PaintContext& ctx,int x,int y){

	// 决策 6：最终坐标 x/y + GetWidth/GetHeight；颜色用 FromRGBA8 精确保持原 RGB(80,120,220)
	ctx.DrawRect(
		Rect{
			static_cast<float>(x),
			static_cast<float>(y),
			static_cast<float>(GetWidth()),
			static_cast<float>(GetHeight())
		},
		Color::FromRGBA8(80, 120, 220)
	);
}

}
