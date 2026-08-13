#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Widget/TextWidget.h"

#include <string>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

/// @brief 按钮控件（5.3：文本完整化；蓝底白字、水平垂直居中）
/// @details 点击行为：OnMouseButtonDown/Up 管理 m_pressed + OnClick；
/// 按下态视觉（m_pressed 用于 OnPaint 变色）归 5.4（Invalidate 未实现）。
class Button: public TextWidget{

public:

	Button() = default;

	explicit Button(const std::string& text);

	explicit Button(std::string&& text);

	bool CanFocus() const noexcept override { return true; }

protected:

	/// @brief P3：Button 水平居中 + 垂直居中（override 对齐策略）
	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;

	void OnMouseButtonDown(const MouseButtonDownEvent&)override;

	void OnMouseButtonUp(const MouseButtonUpEvent&)override;

	virtual void OnClick();

	void OnPaint(PaintContext& ctx,int x,int y) override;

private:

	bool m_pressed = false;

};

}
