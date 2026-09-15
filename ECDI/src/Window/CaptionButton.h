#pragma once

#include "ECDI/Widget/Widget.h"

#include <functional>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

/// @brief CaptionBar 内部按钮（矢量自绘——min / max·restore / close 四种 glyph）
/// @details **内部头**（`src/` 下沉先例）：不进 Public 头计数、不对外承诺。
/// **不复用 Button**（详设 §3.4 决策）：Button 带文本 / 圆角 / 主题 / 点击语义，
/// 与「无文本 glyph + 窗口命令」不是同一抽象——为复用而复用会把两套语义绑在一起。
/// 二态 glyph 由 owner（CaptionBar）在绘制前设置，**无状态订阅、无 Invalidate**。
class CaptionButton final : public Widget{
public:

	enum class Glyph{ Minimize, Maximize, Restore, Close };

	/// @param glyph   初始 glyph
	/// @param onClick 点击回调（Up 且鼠标仍在按钮内时触发；可为空）
	CaptionButton(Glyph glyph, std::function<void()> onClick);

	/// @brief 切换 glyph（二态：Maximize ⇄ Restore——owner 于绘制前设置）
	void SetGlyph(Glyph glyph) noexcept{ m_glyph = glyph; }

	/// @brief D9 能力声明：按钮消费鼠标输入 → 命中该按钮的 caption 区返回 HTCLIENT
	bool ConsumesMouseInput() const noexcept override{ return true; }

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;

	void OnMouseButtonUp(const MouseButtonUpEvent& event) override;

	void OnMouseEnter() override;

	void OnMouseLeave() override;

private:

	/// @brief 触发点击回调（非虚——内部类不对外承诺扩展点）
	void RaiseClick();

	Glyph m_glyph;

	std::function<void()> m_onClick;

	bool m_hovered = false;

	bool m_pressed = false;

};

}
