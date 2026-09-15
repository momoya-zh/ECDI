#include "Window/CaptionButton.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/Render/PaintContext.h"

#include <utility>

namespace ECDI{

namespace{

	// 视觉常量（v0.1 实现内常量——D5：样式 API 留待二次用例；主题集成为本阶段非目标）
	constexpr float kGlyphSize = 10.0f;	///< glyph 图标框边长（10×10 逻辑像素）
	constexpr float kLineWidth = 1.0f;	///< 线宽（DrawLine 默认）

	const Color kGlyph        = Color::FromRGBA8(220, 222, 226, 255);	///< 常态笔色
	const Color kGlyphOnRed   = Color::FromRGBA8(255, 255, 255, 255);	///< close 悬停笔色（红底白图）
	const Color kHoverBg      = Color::FromRGBA8(255, 255, 255, 26);	///< 悬停底（半透明白 ≈10%）
	const Color kPressedBg    = Color::FromRGBA8(255, 255, 255, 45);	///< 按下底（≈18%）
	const Color kCloseHover   = Color::FromRGBA8(196, 43, 28, 255);		///< close 悬停底（系统惯例红）
	const Color kClosePressed = Color::FromRGBA8(160, 32, 20, 255);		///< close 按下底

}

CaptionButton::CaptionButton(Glyph glyph, std::function<void()> onClick)
	: m_glyph(glyph), m_onClick(std::move(onClick)){

}

void CaptionButton::RaiseClick(){

	if (m_onClick){

		m_onClick();

	}

}

void CaptionButton::OnMouseButtonDown(const MouseButtonDownEvent&){

	// 与 Button 一致：不区分按键（Phase 13 无右键语义需求——YAGNI）；隐式 Capture 保证 Up 必达
	m_pressed = true;

	Invalidate();

}

void CaptionButton::OnMouseButtonUp(const MouseButtonUpEvent& event){

	// 拖出释放取消点击（Button 的 I6 修正同款）——Up 时鼠标仍在自身矩形内才触发命令
	const Point abs = GetAbsolutePosition();

	const float mx = static_cast<float>(event.GetMouseX());

	const float my = static_cast<float>(event.GetMouseY());

	const bool inside =
		mx >= abs.x && mx < abs.x + static_cast<float>(GetWidth()) &&
		my >= abs.y && my < abs.y + static_cast<float>(GetHeight());

	m_pressed = false;

	Invalidate();

	if (inside){

		RaiseClick();

	}

}

void CaptionButton::OnMouseEnter(){

	m_hovered = true;

	Invalidate();

}

void CaptionButton::OnMouseLeave(){

	m_hovered = false;

	Invalidate();

}

void CaptionButton::OnPaint(PaintContext& ctx, int x, int y){

	const Rect self{ static_cast<float>(x), static_cast<float>(y),
	                 static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };

	const bool isClose = (m_glyph == Glyph::Close);

	// ① 背景（仅悬停 / 按下——常态不画，露出标题栏底色）
	if (m_pressed){

		ctx.DrawRect(self, isClose ? kClosePressed : kPressedBg);

	}
	else if (m_hovered){

		ctx.DrawRect(self, isClose ? kCloseHover : kHoverBg);

	}

	// ② glyph：居中 10×10 图标框 [gx, gx2] × [gy, gy2]
	const Color pen = (m_hovered && isClose) ? kGlyphOnRed : kGlyph;

	const int cx = x + GetWidth() / 2;

	const int cy = y + GetHeight() / 2;

	const float gx  = static_cast<float>(cx) - kGlyphSize * 0.5f;

	const float gy  = static_cast<float>(cy) - kGlyphSize * 0.5f;

	const float gx2 = gx + kGlyphSize;

	const float gy2 = gy + kGlyphSize;

	const Point lt{ gx, gy };

	const Point rt{ gx2, gy };

	const Point lb{ gx, gy2 };

	const Point rb{ gx2, gy2 };

	switch (m_glyph){

		case Glyph::Minimize:

			// 1 条——水平线（垂直居中）
			ctx.DrawLine(Point{ gx, static_cast<float>(cy) }, Point{ gx2, static_cast<float>(cy) },
			             kLineWidth, pen);

			break;

		case Glyph::Maximize:

			// 4 条——10×10 描边方框（PaintContext 无描边矩形能力 ⇒ 四条边线；先例：CheckBox 用 DrawLine 画勾）
			ctx.DrawLine(lt, rt, kLineWidth, pen);

			ctx.DrawLine(lb, rb, kLineWidth, pen);

			ctx.DrawLine(lt, lb, kLineWidth, pen);

			ctx.DrawLine(rt, rb, kLineWidth, pen);

			break;

		case Glyph::Restore:

			// 6 条——两个错位方框（后框只画上边 + 右边；前框 8×8 完整）
			ctx.DrawLine(Point{ gx + 3.0f, gy }, Point{ gx2, gy }, kLineWidth, pen);

			ctx.DrawLine(Point{ gx2, gy }, Point{ gx2, gy + 7.0f }, kLineWidth, pen);

			ctx.DrawLine(Point{ gx, gy + 3.0f }, Point{ gx + 7.0f, gy + 3.0f }, kLineWidth, pen);

			ctx.DrawLine(Point{ gx, gy + 3.0f }, Point{ gx, gy2 }, kLineWidth, pen);

			ctx.DrawLine(Point{ gx, gy2 }, Point{ gx + 7.0f, gy2 }, kLineWidth, pen);

			ctx.DrawLine(Point{ gx + 7.0f, gy + 3.0f }, Point{ gx + 7.0f, gy2 }, kLineWidth, pen);

			break;

		case Glyph::Close:

			// 2 条——对角交叉
			ctx.DrawLine(lt, rb, kLineWidth, pen);

			ctx.DrawLine(rt, lb, kLineWidth, pen);

			break;

	}

}

}
