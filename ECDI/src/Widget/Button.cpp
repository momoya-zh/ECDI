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

	// 内框尺寸几何防御——borderWidth 是用户可改值（SetStyle），尺寸 - 2×borderWidth 可能为负
	// （如 width=3, borderWidth=2 → -1 无效 Rect）。不做复杂 Clamp 工具（YAGNI），局部 max 保护。
	const float borderWidth = m_style.borderWidth.value;
	const float innerWidth  = (std::max)(0.0f, static_cast<float>(GetWidth()) - 2.0f * borderWidth);
	const float innerHeight = (std::max)(0.0f, static_cast<float>(GetHeight()) - 2.0f * borderWidth);
	const float innerRadius = (std::max)(0.0f, radius - borderWidth);   // 内框圆角随内缩缩小

	if (HasFocus()){

		// 焦点边框：外框 border 色（圆角随 radius），内缩 borderWidth——先画边框色全块，再画背景内缩
		// （不越界、不覆盖相邻控件）
		if (radius > 0.0f){
			ctx.DrawRoundedRect(
				Rect{ static_cast<float>(x), static_cast<float>(y),
				      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
				radius, m_style.border.value);
			ctx.DrawRoundedRect(
				Rect{ static_cast<float>(x) + borderWidth, static_cast<float>(y) + borderWidth,
				      innerWidth, innerHeight },
				innerRadius, background);
		}
		else{
			ctx.DrawRect(
				Rect{ static_cast<float>(x), static_cast<float>(y),
				      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
				m_style.border.value);
			ctx.DrawRect(
				Rect{ static_cast<float>(x) + borderWidth, static_cast<float>(y) + borderWidth,
				      innerWidth, innerHeight },
				background);
		}

	} else {

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

	}

	// 文字颜色来自 TextStyle（TextWidget::m_style.foreground——单一视觉真相）
	DrawTextContent(ctx, x, y);

}

}
