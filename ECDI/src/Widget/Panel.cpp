#include "ECDI/Widget/Panel.h"

#include "ECDI/Theme/DefaultTheme.h"

namespace ECDI{

Panel::Panel(){
	// Phase 9：构造注入 PanelStyle（生命周期契约——Style 进入绘制前必须 ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

void Panel::ApplyTheme(const Theme& theme){

	PanelStyle defaults = theme.GetPanelStyle();
	m_style.background.Apply(defaults.background.value);
	Invalidate();

}

void Panel::OnPaint(PaintContext& ctx,int x,int y){

	// 决策 6：用最终坐标 x/y + GetWidth/GetHeight（不能用 GetRect()，那是局部坐标）
	ctx.DrawRect(
		Rect{
			static_cast<float>(x),
			static_cast<float>(y),
			static_cast<float>(GetWidth()),
			static_cast<float>(GetHeight())
		},
		m_style.background.value
	);
}

}
