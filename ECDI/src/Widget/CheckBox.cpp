#include "ECDI/Widget/CheckBox.h"

#include "ECDI/Render/PaintContext.h"
#include "ECDI/Theme/DefaultTheme.h"

#include <algorithm>

namespace ECDI{

CheckBox::CheckBox(): StateWidget(){
	// TextWidget 构造已注入 TextStyle；CheckBox 再注入 CheckBoxStyle
	// （基类构造期虚函数静态派发——必须在此重新调用以覆盖 CheckBox::ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

CheckBox::CheckBox(const std::string& text): StateWidget(text){
	ApplyTheme(GetDefaultTheme());
}

// ── Phase 9 主题（D7——Apply 只更新未 Override 属性）──

void CheckBox::ApplyTheme(const Theme& theme){

	TextWidget::ApplyTheme(theme);   // ① 先注入 TextStyle（foreground/font）
	CheckBoxStyle defaults = theme.GetCheckBoxStyle();   // ② 再注入 CheckBoxStyle
	m_style.border.Apply(defaults.border.value);
	m_style.borderWidth.Apply(defaults.borderWidth.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);
	m_style.background.Apply(defaults.background.value);
	m_style.checkedBackground.Apply(defaults.checkedBackground.value);
	m_style.checkmark.Apply(defaults.checkmark.value);
	m_style.focusBorder.Apply(defaults.focusBorder.value);
	m_style.boxSize.Apply(defaults.boxSize.value);
	Invalidate();

}

void CheckBox::SetStyle(CheckBoxStyleOverride override){

	if (override.border)            m_style.border.Set(*override.border);
	if (override.borderWidth)       m_style.borderWidth.Set(*override.borderWidth);
	if (override.cornerRadius)      m_style.cornerRadius.Set(*override.cornerRadius);
	if (override.background)        m_style.background.Set(*override.background);
	if (override.checkedBackground) m_style.checkedBackground.Set(*override.checkedBackground);
	if (override.checkmark)         m_style.checkmark.Set(*override.checkmark);
	if (override.focusBorder)       m_style.focusBorder.Set(*override.focusBorder);
	if (override.boxSize)           m_style.boxSize.Set(*override.boxSize);
	Invalidate();

}

// ── 文本位置（与状态框垂直中心对齐——状态框 16px 在控件顶部，默认控件居中会偏下半个字）──

Point CheckBox::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	(void)textWidth;   // 水平左对齐（x 已含框偏移——DrawTextContent 调用时传 x+boxSize+4）
	const float boxSize = (std::max)(0.0f, m_style.boxSize.value);
	const float offsetY = (std::max)(0.0f, (boxSize - lineHeight) / 2.0f);
	return Point{ static_cast<float>(x), static_cast<float>(y) + offsetY };

}

// ── 绘制（框 + 勾——Phase 8 DrawRoundedRect/DrawLine 能力）──

void CheckBox::OnPaint(PaintContext& ctx, int x, int y){

	// 状态框：控件左上角，boxSize 边长（默认 16）
	// 几何输入防御（v1.2）：size/bw 都是用户可改 Style——负值会产生非法 Rect
	const float size = (std::max)(0.0f, m_style.boxSize.value);
	if (size <= 0.0f)
		return;   // 0 尺寸直接跳过（不产生 0×0 RenderCommand）
	const float bw   = (std::max)(0.0f, m_style.borderWidth.value);
	// 焦点态边框色（focusBorder）vs 普通（border）；9.6 收尾方案 A：ShowFocusRect 开关（默认 true）
	const Color border = (HasFocus() && ShowFocusRect()) ? m_style.focusBorder.value : m_style.border.value;
	// 内框几何防御：innerSize 可能为负（borderWidth > size/2）——与 Radio/Button v1.4 同级防御
	const float innerSize = (std::max)(0.0f, size - 2.0f * bw);
	const float innerRadius = (std::max)(0.0f, m_style.cornerRadius.value - bw);   // 内框圆角随内缩缩小

	if (m_style.cornerRadius.value > 0.0f){
		ctx.DrawRoundedRect(Rect{ (float)x, (float)y, size, size },
			m_style.cornerRadius.value, border);
	}
	else{
		ctx.DrawRect(Rect{ (float)x, (float)y, size, size }, border);
	}
	// 内背景（v1.1：cornerRadius>0 时内层也必须圆角——否则方形填充越界圆角边框区）
	const Color innerColor = IsChecked() ? m_style.checkedBackground.value : m_style.background.value;
	if (m_style.cornerRadius.value > 0.0f){
		ctx.DrawRoundedRect(Rect{ (float)x + bw, (float)y + bw, innerSize, innerSize },
			innerRadius, innerColor);
	}
	else{
		ctx.DrawRect(Rect{ (float)x + bw, (float)y + bw, innerSize, innerSize }, innerColor);
	}

	// 选中：画勾（DrawLine 两段折线——左下 → 中 → 右上；比例固定）
	if (IsChecked()){
		const float s = size;
		ctx.DrawLine(Point{ (float)x + s*0.25f, (float)y + s*0.55f },
		             Point{ (float)x + s*0.45f, (float)y + s*0.75f }, bw, m_style.checkmark.value);
		ctx.DrawLine(Point{ (float)x + s*0.45f, (float)y + s*0.75f },
		             Point{ (float)x + s*0.78f, (float)y + s*0.28f }, bw, m_style.checkmark.value);
	}

	// 文本：框右侧偏移（boxSize + 4px 间距），垂直居中
	// （DrawTextContent(ctx, x, y) 的 x 是相对控件原点的绝对偏移——"控件内部自定义文字位置"场景）
	DrawTextContent(ctx, x + static_cast<int>(size) + 4, y);

}

}
