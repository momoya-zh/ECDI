#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Widget/TextWidget.h"

#include <functional>
#include <string>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

/// @brief 按钮控件（5.3：文本完整化；蓝底白字、水平垂直居中）
/// @details 点击行为：OnMouseButtonDown/Up 管理 m_pressed + RaiseClick；
/// 按下态视觉（m_pressed 用于 OnPaint 变色）归 5.4（Invalidate 未实现）。
/// Phase 9：TextStyle（继承自 TextWidget）+ ButtonStyle（本类持有）——两个 Style 组合（has-a，非继承）。
class Button: public TextWidget{

public:

	Button();   // 默认构造——构造体注入 ButtonStyle（TextStyle 已由 TextWidget 构造注入）

	explicit Button(const std::string& text);

	explicit Button(std::string&& text);

	bool CanFocus() const noexcept override { return true; }

	// ── Phase 9：主题与样式（using 防名字隐藏——保留基类 SetStyle(TextStyleOverride)，可分别设置两 Style）──

	using TextWidget::SetStyle;

	void ApplyTheme(const Theme& theme) override;   // 扩展：先 TextStyle（基类）再 ButtonStyle

	/// @brief Button 专属样式覆盖（文字颜色/字体经 TextWidget::SetStyle(TextStyleOverride)）
	void SetStyle(ButtonStyleOverride override);

	// ── 回调注册（7.5 新增：业务便利层）────────────────

	using ClickCallback = std::function<void()>;   ///< 点击回调类型

	/// @brief 注册点击回调（覆盖式：后注册覆盖前者；传空 = 解除注册）
	/// @details 回调在 RaiseClick() 内、OnClick() 虚方法之后调用——
	/// 子类 override OnClick 不影响回调触发（D4 RaiseXxx 分离模式）
	void SetOnClick(ClickCallback callback);

protected:

	/// @brief P3：Button 水平居中 + 垂直居中（override 对齐策略）
	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;

	void OnMouseButtonDown(const MouseButtonDownEvent&)override;

	void OnMouseButtonUp(const MouseButtonUpEvent&)override;

	/// @brief 点击虚方法（子类可 override 扩展行为；空实现）
	/// @details 调用链：OnMouseButtonUp → RaiseClick → OnClick() + m_onClick()
	virtual void OnClick();

	void OnPaint(PaintContext& ctx,int x,int y) override;

	/// @brief Button 专属样式（protected——测试派生类 TestableButton 可访问；不含 foreground——TextStyle 是文字唯一来源）
	ButtonStyle m_style;

private:

	/// @brief 点击通知入口（非虚，内部唯一入口——D9 契约）
	/// @details 先调 OnClick() 虚方法，再调 m_onClick() 回调——彼此独立
	void RaiseClick();

	bool m_pressed = false;

	ClickCallback m_onClick;   ///< 点击回调（7.5 新增：业务便利层）

};

}
