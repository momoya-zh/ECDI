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
/// 使用场景：
/// - 作为其他控件的父容器
/// - 在 Widget 树中形成逻辑分组
/// - 为未来 Layout System 提供容器基础
///
/// @note 当前阶段不实现任何 Layout 能子 Widget 的排列方式由用户手动设置。
/// Phase 9：持有 PanelStyle（background）；MVP 不支持 Override（无 PanelStyleOverride——YAGNI）。
class Panel : public Widget{

public:

	Panel();   // 构造体注入 PanelStyle（ApplyTheme）

	~Panel() override = default;

	/// @brief 应用主题（同步默认值到 m_style，只更新未 Override 属性——D7；Panel 无 Override API 故等价于全量注入）
	void ApplyTheme(const Theme& theme);

protected:

	/// @brief Panel 专属样式（protected——测试派生类可访问；MVP 无 Override）
	PanelStyle m_style;

	void OnPaint(PaintContext& ctx,int x,int y)override;
};

}
