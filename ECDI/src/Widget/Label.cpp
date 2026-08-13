#include "ECDI/Widget/Label.h"

#include <utility>

namespace ECDI{

Label::Label(const std::string& text): m_text(text){

}

Label::Label(std::string&& text): m_text(std::move(text)){

}


void Label::SetText(const std::string& text){

	m_text = text;

}

void Label::SetText(std::string&& text){

	m_text = std::move(text);

}

const std::string& Label::GetText() const noexcept{

	return m_text;

}

void Label::SetTextColor(const Color& color){

	m_textColor = color;

}

const Color& Label::GetTextColor() const noexcept{

	return m_textColor;

}

// ── 绘制（L3/L5/P2：只画文本，透明背景，垂直居中）────────────

void Label::OnPaint(PaintContext& ctx, int x, int y){

	// 空文本：零命令零绘制（P2-a）
	if (m_text.empty())
		return;

	// 垂直居中（P2-b：LineHeight 单次调用；负 offsetY 合法不修正——控件比文本小是布局问题）
	const float lineHeight = ctx.LineHeight(m_font);
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	ctx.DrawText(
		Point{ static_cast<float>(x), static_cast<float>(y) + offsetY },
		m_text, m_textColor, m_font
	);

}

}
