#include "ECDI/Widget/Panel.h"

namespace ECDI{

void Panel::OnPaint(PaintContext& ctx,int x,int y){

	// 决策 6：用最终坐标 x/y + GetWidth/GetHeight（不能用 GetRect()，那是局部坐标）
	ctx.DrawRect(
		Rect{
			static_cast<float>(x),
			static_cast<float>(y),
			static_cast<float>(GetWidth()),
			static_cast<float>(GetHeight())
		},
		Color::Gray()
	);
}

}
