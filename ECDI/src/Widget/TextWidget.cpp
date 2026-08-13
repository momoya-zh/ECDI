#include "ECDI/Widget/TextWidget.h"

#include "ECDI/Core/Size.h"
#include "ECDI/Render/PaintContext.h"

#include <utility>

namespace ECDI{

TextWidget::TextWidget(const std::string& text): m_text(text){

}

TextWidget::TextWidget(std::string&& text): m_text(std::move(text)){

}

void TextWidget::SetText(const std::string& text){

	m_text = text;

}

void TextWidget::SetText(std::string&& text){

	m_text = std::move(text);

}

const std::string& TextWidget::GetText() const noexcept{

	return m_text;

}

void TextWidget::SetTextColor(const Color& color){

	m_textColor = color;

}

const Color& TextWidget::GetTextColor() const noexcept{

	return m_textColor;

}

// ── 文本绘制（B3：对齐策略虚方法 + 统一绘制入口）────────────

Point TextWidget::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	// 默认：水平左对齐 + 垂直居中（P7 定案；负 offsetY 合法不修正——控件比文本小是布局问题）
	(void)textWidth;

	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	return Point{ static_cast<float>(x), static_cast<float>(y) + offsetY };

}

void TextWidget::DrawTextContent(PaintContext& ctx, int x, int y){

	// 空文本：零命令零绘制
	if (m_text.empty())
		return;

	// D5：MeasureText 一次拿宽高（居中需要宽度；height 与 LineHeight 同源）
	const Size textSize = ctx.MeasureText(m_font, m_text);

	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, textSize.height),
		m_text, m_textColor, m_font
	);

}

}
