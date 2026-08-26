#include "ECDI/Widget/Button.h"

#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/Theme/DefaultTheme.h"

#include <algorithm>
#include <utility>

namespace ECDI{

Button::Button(): TextWidget(){
	// TextWidget 构造已注入 TextStyle；Button 再注入 ButtonStyle
	// （基类构造期虚函数静态派发——必须在此重新调用以覆盖 Button::ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

Button::Button(const std::string& text): TextWidget(text){

	// Phase 9：TextWidget 构造已注入 TextStyle；Button 再注入 ButtonStyle
	// （基类构造期虚函数静态派发——必须在此重新调用以覆盖 Button::ApplyTheme）
	ApplyTheme(GetDefaultTheme());

}

Button::Button(std::string&& text): TextWidget(std::move(text)){

	ApplyTheme(GetDefaultTheme());

}

// ── Phase 9：主题应用与样式覆盖（D7——Apply 只更新未 Override 属性）────────

void Button::ApplyTheme(const Theme& theme){

	TextWidget::ApplyTheme(theme);   // ① 先注入 TextStyle（全局默认 foreground=Black——Label/TextBox 用）
	// ⚠️ 设计补正（2026-08-25 实现期）：Button 文字默认白（迁移前 m_textColor = White——白字叠蓝底是
	// Button 视觉身份，非全局主题值）。Apply(White) 语义 = "未覆盖时注入默认值"——用户
	// SetStyle(TextStyleOverride.foreground) 后 overridden=true → 不再覆盖（D7）。
	TextWidget::m_style.foreground.Apply(Color::White());
	ButtonStyle defaults = theme.GetButtonStyle();   // ② 再注入 ButtonStyle
	m_style.background.Apply(defaults.background.value);
	m_style.border.Apply(defaults.border.value);
	m_style.borderWidth.Apply(defaults.borderWidth.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);
	m_style.pressedBackground.Apply(defaults.pressedBackground.value);
	Invalidate();

}

void Button::SetStyle(ButtonStyleOverride override){

	if (override.background)         m_style.background.Set(*override.background);
	if (override.border)             m_style.border.Set(*override.border);
	if (override.borderWidth)        m_style.borderWidth.Set(*override.borderWidth);
	if (override.cornerRadius)       m_style.cornerRadius.Set(*override.cornerRadius);
	if (override.pressedBackground)  m_style.pressedBackground.Set(*override.pressedBackground);
	Invalidate();

}

// ── 文本位置（P3：水平居中 + 垂直居中；负 offset 合法不修正）────────────

Point Button::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	const float offsetX = (static_cast<float>(GetWidth()) - textWidth) / 2.0f;
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	return Point{ static_cast<float>(x) + offsetX, static_cast<float>(y) + offsetY };

}

void Button::OnMouseButtonDown(const MouseButtonDownEvent&){

	// 5.4.5：按下态 + 重绘（Invalidate 机制 5.4.1；Capture 保证 Up 必达 5.4.2）
	m_pressed = true;

	Invalidate();

}

void Button::OnMouseButtonUp(const MouseButtonUpEvent& event){

	// 5.4.5：I6 修正——拖出释放取消点击（Up 时鼠标在自身内才 OnClick）
	const Point abs = GetAbsolutePosition();

	const float mx = static_cast<float>(event.GetMouseX());

	const float my = static_cast<float>(event.GetMouseY());

	const bool inside =
		mx >= abs.x && mx < abs.x + static_cast<float>(GetWidth()) &&
		my >= abs.y && my < abs.y + static_cast<float>(GetHeight());

	// D5 GPT 修正：先恢复视觉（m_pressed=false + 重绘）再 OnClick（用户直觉）
	m_pressed = false;

	Invalidate();

	if (inside){

		RaiseClick();   // 7.5：RaiseXxx 分离模式——虚方法 + 回调独立通道（D4）

	}

}

void Button::OnClick(){}

// ── 回调通知（7.5：D4 三段式——RaiseClick 内部先虚方法后回调，彼此独立）──

void Button::RaiseClick(){

	OnClick();                    // ① 虚方法（子类可 override 扩展）

	if (m_onClick)                // ② 回调（独立通道，override 无法吞掉）

		m_onClick();

}

void Button::SetOnClick(ClickCallback callback){

	m_onClick = std::move(callback);

}

void Button::OnPaint(PaintContext& ctx,int x,int y){

	// Phase 9：视觉全部来自 Style（5.4.5 按下变深——凹陷直觉）
	const Color background = m_pressed
		? m_style.pressedBackground.value
		: m_style.background.value;

	// cornerRadius 真正消费——Phase 8 DrawRoundedRect 能力在此接入
	// （不允许 StyleField 存在但不被 Renderer 消费——"存了但没用"的字段会误导 SetStyle 用户）
	const float radius = m_style.cornerRadius.value;

	// 9.5 方案 A 修正（2026-08-26）：焦点框不再"整块铺 border 色"——
	// 旧实现"先画边框色全块 + 内缩画背景"在背景透明/半透明时会把 border 色（默认白）整块露出来
	// （alpha 补全后暴露：background.a == 0 → 背景 no-op，只剩白块 = 整个按钮变白）。
	// 新实现：① 先画背景全块（透明就透明，语义正确）② 聚焦时再画焦点框描边（DrawFocusRect 点线，
	// 只描边缘不占背景层——Phase 8 焦点框能力接入，替代旧"整块填充"式边框）。
	// 内框尺寸/borderWidth 逻辑随旧实现移除（焦点框不再需要内缩计算——YAGNI）。

	// ① 背景全块（透明/半透明正常合成——DrawRect/DrawRoundedRect 的 a<1 走 AlphaBlend）
	if (radius > 0.0f){
		ctx.DrawRoundedRect(
			Rect{ static_cast<float>(x), static_cast<float>(y),
			      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
			radius, background);
	}
	else{
		ctx.DrawRect(
			Rect{ static_cast<float>(x), static_cast<float>(y),
			      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
			background);
	}

	// ② 焦点框描边（点线框——边缘标记，不覆盖背景；颜色来自 Style.border 单一真相）
	// 圆角跟随控件 radius（9.5 R4：DrawFocusRect 支持 cornerRadius——圆角按钮焦点框贴圆角）
	if (HasFocus()){

		ctx.DrawFocusRect(
			Rect{ static_cast<float>(x), static_cast<float>(y),
			      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
			radius, m_style.border.value);

	}

	// 文字颜色来自 TextStyle（TextWidget::m_style.foreground——单一视觉真相）
	DrawTextContent(ctx, x, y);

}

}
