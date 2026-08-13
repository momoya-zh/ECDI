#include "ECDI/Widget/Button.h"

#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"

#include <utility>

namespace ECDI{

Button::Button(const std::string& text): TextWidget(text){

	// B2：默认白字叠蓝底（构造直接初始化成员——C++ 惯例；ThemeSystem 阶段改 ApplyTheme/SetStyle）
	m_textColor = Color::White();

}

Button::Button(std::string&& text): TextWidget(std::move(text)){

	m_textColor = Color::White();

}

// ── 文本位置（P3：水平居中 + 垂直居中；负 offset 合法不修正）────────────

Point Button::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	const float offsetX = (static_cast<float>(GetWidth()) - textWidth) / 2.0f;
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	return Point{ static_cast<float>(x) + offsetX, static_cast<float>(y) + offsetY };

}

void Button::OnMouseButtonDown(const MouseButtonDownEvent&){

	// 5.4.5：按下态 + 重绘（Invalidate 机制 5.4.1；Capture 保证 Up 必达 5.4.2）
	m_pressed = true;

	Invalidate();

}

void Button::OnMouseButtonUp(const MouseButtonUpEvent& event){

	// 5.4.5：I6 修正——拖出释放取消点击（Up 时鼠标在自身内才 OnClick）
	const Point abs = GetAbsolutePosition();

	const float mx = static_cast<float>(event.GetMouseX());

	const float my = static_cast<float>(event.GetMouseY());

	const bool inside =
		mx >= abs.x && mx < abs.x + static_cast<float>(GetWidth()) &&
		my >= abs.y && my < abs.y + static_cast<float>(GetHeight());

	// D5 GPT 修正：先恢复视觉（m_pressed=false + 重绘）再 OnClick（用户直觉）
	m_pressed = false;

	Invalidate();

	if (inside){

		OnClick();

	}

}

void Button::OnClick(){}

void Button::OnPaint(PaintContext& ctx,int x,int y){

	// 5.4.5：按下变深（凹陷直觉）
	const Color background = m_pressed
		? Color::FromRGBA8(60, 90, 180)
		: Color::FromRGBA8(80, 120, 220);

	if (HasFocus()){

		// 5.4.5 焦点边框：内框方案——先画边框色全块，再画背景内缩 2px → 露出 2px 白环
		// （不越界、不覆盖相邻控件；零命令扩展，GPT 确认）
		ctx.DrawRect(
			Rect{
				static_cast<float>(x),
				static_cast<float>(y),
				static_cast<float>(GetWidth()),
				static_cast<float>(GetHeight())
			},
			Color::White()
		);

		ctx.DrawRect(
			Rect{
				static_cast<float>(x) + 2.0f,
				static_cast<float>(y) + 2.0f,
				static_cast<float>(GetWidth()) - 4.0f,
				static_cast<float>(GetHeight()) - 4.0f
			},
			background
		);

	} else {

		ctx.DrawRect(
			Rect{
				static_cast<float>(x),
				static_cast<float>(y),
				static_cast<float>(GetWidth()),
				static_cast<float>(GetHeight())
			},
			background
		);

	}

	// B3：白字居中叠背景；空文本只跳文本，背景照画
	DrawTextContent(ctx, x, y);

}

}
