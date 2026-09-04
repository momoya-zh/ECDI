#include "ECDI/Widget/Radio.h"

#include "ECDI/Render/PaintContext.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Widget/Widget.h"

#include <algorithm>

namespace ECDI{

Radio::Radio(): StateWidget(){
	// TextWidget 构造已注入 TextStyle；Radio 再注入 RadioStyle
	ApplyTheme(GetDefaultTheme());
}

Radio::Radio(const std::string& text): StateWidget(text){
	ApplyTheme(GetDefaultTheme());
}

// ── 互斥（契约 4/5：同父互斥 + 交互不可取消）──

void Radio::SetChecked(bool checked){

	if (checked)
		UncheckSiblings();   // 先取消兄弟（互斥——先释放旧选择）
	StateWidget::SetChecked(checked);   // 再设自身（相同值 no-op 保持：交互不可取消）

}

void Radio::UncheckSiblings(){

	if (Widget* parent = GetParent()){
		const size_t count = parent->GetChildCount();
		for (size_t i = 0; i < count; ++i){
			if (Radio* sibling = dynamic_cast<Radio*>(parent->GetChildAt(i))){
				if (sibling != this && sibling->IsChecked())
					sibling->StateWidget::SetChecked(false);   // 直接调基类——强制取消，不重入互斥策略
			}
		}
	}

}

void Radio::OnClickToggle(){

	SetChecked(true);   // 非反转（契约 5——已选中再点无变化：SetChecked 相同值 no-op）

}

// ── Phase 9 主题（D7——Apply 只更新未 Override 属性）──

void Radio::ApplyTheme(const Theme& theme){

	TextWidget::ApplyTheme(theme);   // ① 先注入 TextStyle（foreground/font）
	RadioStyle defaults = theme.GetRadioStyle();   // ② 再注入 RadioStyle
	m_style.border.Apply(defaults.border.value);
	m_style.borderWidth.Apply(defaults.borderWidth.value);
	m_style.background.Apply(defaults.background.value);
	m_style.dot.Apply(defaults.dot.value);
	m_style.focusBorder.Apply(defaults.focusBorder.value);
	m_style.circleSize.Apply(defaults.circleSize.value);
	Invalidate();

}

void Radio::SetStyle(RadioStyleOverride override){

	if (override.border)       m_style.border.Set(*override.border);
	if (override.borderWidth)  m_style.borderWidth.Set(*override.borderWidth);
	if (override.background)   m_style.background.Set(*override.background);
	if (override.dot)          m_style.dot.Set(*override.dot);
	if (override.focusBorder)  m_style.focusBorder.Set(*override.focusBorder);
	if (override.circleSize)   m_style.circleSize.Set(*override.circleSize);
	Invalidate();

}

// ── 文本位置（与外圆垂直中心对齐——外圆 16px 在控件顶部，默认控件居中会偏下半个字）──

Point Radio::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	(void)textWidth;   // 水平左对齐（x 已含圆偏移——DrawTextContent 调用时传 x+circleSize+4）
	const float circleSize = (std::max)(0.0f, m_style.circleSize.value);
	const float offsetY = (std::max)(0.0f, (circleSize - lineHeight) / 2.0f);
	return Point{ static_cast<float>(x), static_cast<float>(y) + offsetY };

}

// ── 绘制（外圆 + 圆点——Phase 8 DrawRoundedRect 能力）──

void Radio::OnPaint(PaintContext& ctx, int x, int y){

	// 外圆：DrawRoundedRect(cornerRadius = circleSize/2)（Phase 8 能力——真实圆）
	// 几何输入防御（v1.2）：同 CheckBox——负值/0 尺寸直接跳过
	const float size = (std::max)(0.0f, m_style.circleSize.value);
	if (size <= 0.0f)
		return;
	const float radius = size / 2.0f;
	const float bw = (std::max)(0.0f, m_style.borderWidth.value);
	// 焦点态边框色（focusBorder）vs 普通（border）；9.6 收尾方案 A：ShowFocusRect 开关（默认 true）
	const Color border = (HasFocus() && ShowFocusRect()) ? m_style.focusBorder.value : m_style.border.value;

	ctx.DrawRoundedRect(Rect{ (float)x, (float)y, size, size }, radius, border);
	ctx.DrawRoundedRect(Rect{ (float)x + bw, (float)y + bw, size - 2.0f*bw, size - 2.0f*bw },
		(std::max)(0.0f, radius - bw), m_style.background.value);   // 几何防御（同 Button v1.4）

	// 选中：内圆点（circleSize 的 40% 比例——视觉比例决策，非独立参数 YAGNI）
	if (IsChecked()){
		const float dot = size * 0.4f;
		const float dotOffset = (size - dot) / 2.0f;
		ctx.DrawRoundedRect(Rect{ (float)x + dotOffset, (float)y + dotOffset, dot, dot },
			dot / 2.0f, m_style.dot.value);
	}

	// 文本：圆右侧偏移（circleSize + 4px 间距），垂直居中
	DrawTextContent(ctx, x + static_cast<int>(size) + 4, y);

}

}
