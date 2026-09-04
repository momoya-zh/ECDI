#include "ECDI/Widget/Panel.h"

#include "ECDI/Theme/DefaultTheme.h"

#include <algorithm>

namespace ECDI{

Panel::Panel(){
	// Phase 9：构造注入 PanelStyle（生命周期契约——Style 进入绘制前必须 ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

void Panel::ApplyTheme(const Theme& theme){

	PanelStyle defaults = theme.GetPanelStyle();
	m_style.background.Apply(defaults.background.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);
	m_style.borderWidth.Apply(defaults.borderWidth.value);
	m_style.borderColor.Apply(defaults.borderColor.value);
	Invalidate();

}

void Panel::SetStyle(PanelStyleOverride override){
	// 同 Button::SetStyle：仅覆盖传入字段（std::optional 表达"是否覆盖"），未传字段保持原值/D7 主题值
	if (override.background)    m_style.background.Set(*override.background);
	if (override.cornerRadius)  m_style.cornerRadius.Set(*override.cornerRadius);
	if (override.borderWidth)   m_style.borderWidth.Set(*override.borderWidth);
	if (override.borderColor)   m_style.borderColor.Set(*override.borderColor);
	Invalidate();
}

bool Panel::ContainsPoint(int x, int y) const noexcept{
	// 2026-08-30 定案（phase9.6-panel-container-semantics v1.1）：
	// 镶板 = 纯容器，自身永不参与命中；鼠标事件只由子控件接收（HitTest 子优先递归，子未命中也不回落到 Panel 自身）
	(void)x;
	(void)y;
	return false;
}

void Panel::OnPaint(PaintContext& ctx,int x,int y){

	// 决策 6：用最终坐标 x/y + GetWidth/GetHeight（不能用 GetRect()，那是局部坐标）
	// 2026-08-30：默认透明（a==0）时跳过 DrawRect——性能优化；PushClip/PopClip 仍由 Widget::Paint 基类管线发出
	// P1 形态（modelprobe-p1-detailed-design §5）：cornerRadius → DrawRoundedRect；
	// borderWidth>0 → 双矩形描边环（几何公式同 TextBox §3.3——外层 borderColor + 内层背景内缩）
	const float fw = static_cast<float>(GetWidth());
	const float fh = static_cast<float>(GetHeight());
	const float radius = m_style.cornerRadius.value;
	const bool drawBorder = m_style.borderWidth.value > 0.0f && m_style.borderColor.value.a > 0.0f;
	const float bw = m_style.borderWidth.value;
	const Color& background = m_style.background.value;

	if (drawBorder){
		// ① 外层环色（全尺寸）
		if (radius > 0.0f){
			ctx.DrawRoundedRect(Rect{ static_cast<float>(x), static_cast<float>(y), fw, fh }, radius, m_style.borderColor.value);
		}
		else{
			ctx.DrawRect(Rect{ static_cast<float>(x), static_cast<float>(y), fw, fh }, m_style.borderColor.value);
		}
		// ② 内层背景（四边内缩；内半径 = max(0, 外半径 - 描边宽)；退化保护）
		const float innerRadius = (std::max)(0.0f, radius - bw);
		const float iw = fw - 2.0f * bw;
		const float ih = fh - 2.0f * bw;
		if (background.a > 0.0f && iw > 0.0f && ih > 0.0f){
			if (innerRadius > 0.0f){
				ctx.DrawRoundedRect(Rect{ static_cast<float>(x) + bw, static_cast<float>(y) + bw, iw, ih }, innerRadius, background);
			}
			else{
				ctx.DrawRect(Rect{ static_cast<float>(x) + bw, static_cast<float>(y) + bw, iw, ih }, background);
			}
		}
		return;   // 边框分支已处理背景（透明背景 → 只画边框环——语义自洽）
	}

	if (background.a > 0.0f){
		if (radius > 0.0f){
			ctx.DrawRoundedRect(Rect{ static_cast<float>(x), static_cast<float>(y), fw, fh }, radius, background);
		}
		else{
			ctx.DrawRect(Rect{ static_cast<float>(x), static_cast<float>(y), fw, fh }, background);
		}
	}
}

}
