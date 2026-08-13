#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Widget/Widget.h"

#include <string>

namespace ECDI{

class Label: public Widget{

public:

	Label() = default;

	explicit Label(const std::string& text);

	explicit Label(std::string&& text);

	void SetText(const std::string& text);

	void SetText(std::string&& text);

	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);

	const Color& GetTextColor() const noexcept;

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

private:

	std::string m_text;

	Color m_textColor = Color::Black();

	Font m_font{};  // L4 预留：未来 SetFont() 一行接入（m_font = font），OnPaint 零改动

};

}
