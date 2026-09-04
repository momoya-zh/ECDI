#include "ECDI/Widget/TextWidget.h"

#include "ECDI/Core/Size.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/TextMeasurer.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Window/Window.h"

#include <utility>

namespace ECDI{

TextWidget::TextWidget(): m_text(){
	// Phase 9：默认构造也注入主题默认样式（生命周期契约——任何 Style 进入绘制前必须 ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

TextWidget::TextWidget(const std::string& text): m_text(text){
	// Phase 9：从主题注入默认样式（虚函数在基类构造期静态派发到 TextWidget::ApplyTheme——
	// Button/TextBox 派生类构造函数需再次调用以注入专属 Style）
	ApplyTheme(GetDefaultTheme());
}

TextWidget::TextWidget(std::string&& text): m_text(std::move(text)){
	ApplyTheme(GetDefaultTheme());
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

	// 旧 API 转发（单一状态来源——经 SetStyle 产生 Override 标记，后续 ApplyTheme 不覆盖）
	TextStyleOverride style;
	style.foreground = color;
	SetStyle(style);

}

const Color& TextWidget::GetTextColor() const noexcept{

	return m_style.foreground.value;

}

void TextWidget::SetFont(const Font& font){

	// 旧 API 转发（单一状态来源——经 SetStyle 产生 Override 标记）
	TextStyleOverride style;
	style.font = font;
	SetStyle(style);

}

// ── Phase 9：主题应用与样式覆盖（D7——Apply 只更新未 Override 属性）────────

void TextWidget::ApplyTheme(const Theme& theme){

	TextStyle defaults = theme.GetTextStyle();   // 消费 GetTextStyle（非 GetLabelStyle——v1.1 修正）
	m_style.foreground.Apply(defaults.foreground.value);
	m_style.font.Apply(defaults.font.value);
	Invalidate();

}

void TextWidget::SetStyle(TextStyleOverride override){

	if (override.foreground) m_style.foreground.Set(*override.foreground);
	if (override.font)        m_style.font.Set(*override.font);
	Invalidate();

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
	const Size textSize = ctx.MeasureText(m_style.font.value, m_text);

	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, textSize.height),
		m_text, m_style.foreground.value, m_style.font.value
	);

}

Size TextWidget::GetPreferredSize() const{

	// 9.8：有测量器（正常运行 Window / 测试注入）→ 内容测量；无 → 运行时 fallback 当前尺寸
	if (TextMeasurer* measurer = ResolveMeasurer())
		return DoMeasureText(*measurer);
	return Widget::GetPreferredSize();

}

TextMeasurer* TextWidget::ResolveMeasurer() const{

	// const 方法内 GetWindow() 返回 const Window*——GetTextMeasurer 非 const，只读测量经 const_cast（GetLineHeight 同款先例）
	if (Window* window = const_cast<Window*>(GetWindow()))
		return &window->GetTextMeasurer();
	return nullptr;

}

Size TextWidget::DoMeasureText(TextMeasurer& measurer) const{

	// Label/Button 0 inset（§3.2 冻结）：{文本测量宽, 行高}；空文本 MeasureText 返回 {0,0} → 宽 0 诚实
	const Size textSize = measurer.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);
	return Size{ textSize.width, lineHeight };

}

}
