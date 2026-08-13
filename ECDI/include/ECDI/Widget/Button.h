#pragma once

#include "ECDI/Widget/Widget.h"

#include <string>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

class Button:public Widget{

public:

	Button() = default;

	explicit Button(const std::wstring& text);

	explicit Button(std::wstring&& text);

	void SetText(const std::wstring& text);

	void SetText(std::wstring&& text);

	bool CanFocus() const noexcept override { return true; }

	const std::wstring& GetText()const noexcept;

private:

	std::wstring m_text;

	bool m_pressed = false;

protected:

	void OnMouseButtonDown(const MouseButtonDownEvent&)override;

	void OnMouseButtonUp(const MouseButtonUpEvent&)override;

	virtual void OnClick();

	void OnPaint(PaintContext& ctx,int x,int y) override;
};

}
