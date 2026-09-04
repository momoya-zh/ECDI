#pragma once

#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

class Theme;   // 前置声明（ApplyTheme(const Theme&) 引用参数——TextWidget.h 同款）

/// @brief 容器面板 Widget
/// @details
/// 纯容器语义：用于组织和分组子 Widget，自身没有额外行为。
/// 所有能力（AddChild/RemoveChild/HitTest/Bubbling）继承自 Widget 基类。
///
/// 输入透传（2026-08-30 定案，phase9.6-panel-container-semantics v1.1）：
/// ContainsPoint 恒 false——Panel 自身永不参与命中，鼠标事件只由子控件接收；
/// 默认背景透明（RGBA 0,0,0,0，DefaultTheme），需要底色时经 SetStyle 显式设色。
///
/// 使用场景：
/// - 作为其他控件的父容器
/// - 在 Widget 树中形成逻辑分组
/// - 为未来 Layout System 提供容器基础
///
/// @note 当前阶段不实现任何 Layout，子 Widget 的排列方式由用户手动设置。
/// Phase 9：持有 PanelStyle（background）；单实例覆盖经 SetStyle(PanelStyleOverride)（2026-08-29 落地）。
class Panel : public Widget{

public:

	Panel();   // 构造体注入 PanelStyle（ApplyTheme）

	~Panel() override = default;

	/// @brief 应用主题（同步默认值到 m_style，只更新未 Override 属性——D7；Panel 无 Override API 故等价于全量注入）
	void ApplyTheme(const Theme& theme);

	/// @brief Panel 专属样式覆盖（背景色；2026-08-29 落地——此前 MVP 无 Override API）
	/// @details 与 Button::SetStyle / TextBox::SetStyle 同构：StyleField::Set 标记 overridden，
	/// 后续 ApplyTheme 不再覆盖此字段；覆盖后 Invalidate 触发重绘。
	void SetStyle(PanelStyleOverride override);

	/// @brief Panel 永远自身不命中（镶板 = 纯容器语义，2026-08-30 定案）
	/// @details HitTest 子优先递归——鼠标事件由子控件接收，Panel 自身矩形不拦截任何输入。
	/// 固定语义、无开关（YAGNI，需求出现再加）。
	bool ContainsPoint(int x, int y) const noexcept override;

protected:

	/// @brief Panel 专属样式（protected——测试派生类可访问；MVP 无 Override）
	PanelStyle m_style;

	void OnPaint(PaintContext& ctx,int x,int y)override;
};

}
