#include "ECDI/Widget/Button.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Window/Window.h"

#include <algorithm>
#include <chrono>
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
	m_style.hoverBackground.Apply(defaults.hoverBackground.value);

	// 9.6 S1：背景呈现值与主题同步（构造/换主题 = 即时到位；运行期状态变化才走过渡）。
	// 换主题 = 即时重置语义——若背景动画仍在跑则取消（防旧动画写回旧色）。
	if (Window* window = GetWindow()){

		window->GetAnimationManager().Cancel(m_backgroundAnimToken);

	}

	m_displayedBackground = m_style.background.value;

	Invalidate();

}

void Button::SetStyle(ButtonStyleOverride override){

	if (override.background)
	{
		// 9.6 S1：背景样式覆盖 = 即时重置（同 ApplyTheme——取消在跑动画防旧色写回）
		if (Window* window = GetWindow()){

			window->GetAnimationManager().Cancel(m_backgroundAnimToken);

		}

		m_displayedBackground = *override.background;
	}

	if (override.background)         m_style.background.Set(*override.background);
	if (override.border)             m_style.border.Set(*override.border);
	if (override.borderWidth)        m_style.borderWidth.Set(*override.borderWidth);
	if (override.cornerRadius)       m_style.cornerRadius.Set(*override.cornerRadius);
	if (override.pressedBackground)  m_style.pressedBackground.Set(*override.pressedBackground);
	if (override.hoverBackground)    m_style.hoverBackground.Set(*override.hoverBackground);
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
	// 9.6 S1：状态翻转（事件驱动）→ 色过渡（表现层）——动画只平滑到达状态、不产生状态
	m_pressed = true;

	AnimateBackgroundTo(GetTargetBackground());

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
	// 9.6 S1：松开 → 色过渡回正常态（from = 当前呈现色——快速点击无跳变，替换式重启）
	m_pressed = false;

	AnimateBackgroundTo(GetTargetBackground());

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

// ── 背景色过渡（9.6 S1）────────────────────────────────────────

Color Button::GetTargetBackground() const noexcept{

	// P1 三态优先级：按下 > hover > 正常（QSS `:active` 覆盖 `:hover` 同语义）
	return m_pressed
		? m_style.pressedBackground.value
		: (m_hovered ? m_style.hoverBackground.value : m_style.background.value);

}

void Button::OnMouseEnter(){
	// P1 hover：进入 → 目标色切 hoverBackground（复用 9.6 S1 过渡——200ms EaseOut 平滑变亮）
	m_hovered = true;
	AnimateBackgroundTo(GetTargetBackground());
	Invalidate();
}

void Button::OnMouseLeave(){
	// P1 hover：离开 → 目标色还原 normal（按下中离开：m_pressed 仍优先）
	m_hovered = false;
	AnimateBackgroundTo(GetTargetBackground());
	Invalidate();
}

void Button::AnimateBackgroundTo(const Color& target){

	// 目标即当前呈现——无过渡需求（同 token 空转无意义）
	if (target == m_displayedBackground){

		return;

	}

	Window* window = GetWindow();

	if (window == nullptr){

		m_displayedBackground = target;   // 无窗口（测试树/构造期）——即时到位

		return;

	}

	// d6 值回调式 + d7 token：from = 当前呈现值（替换式重启语义兑现点——快速点击无跳变）；
	// EaseOut 减速收尾（按下/松开手感）
	window->GetAnimationManager().Start(
		m_backgroundAnimToken,
		m_displayedBackground,
		target,
		std::chrono::milliseconds(kBackgroundTransitionMs),
		Easing::EaseOut,
		[this](const Color& c){ m_displayedBackground = c; });

}

void Button::SetOnClick(ClickCallback callback){

	m_onClick = std::move(callback);

}

void Button::OnPaint(PaintContext& ctx,int x,int y){

	// 9.6 S1：视觉全部来自呈现值（m_displayedBackground——动画 onValue 写入的单一视觉真相；
	// 无动画时 = m_style.background.value 由状态变化即时同步）

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
			radius, m_displayedBackground);
	}
	else{
		ctx.DrawRect(
			Rect{ static_cast<float>(x), static_cast<float>(y),
			      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
			m_displayedBackground);
	}

	// ② 焦点框描边（点线框——边缘标记，不覆盖背景；颜色来自 Style.border 单一真相）
	// 圆角跟随控件 radius（9.5 R4：DrawFocusRect 支持 cornerRadius——圆角按钮焦点框贴圆角）
	// 9.6 收尾方案 A：ShowFocusRect 视觉开关（默认 true——行为不变；焦点状态本身不受影响）
	if (HasFocus() && ShowFocusRect()){

		ctx.DrawFocusRect(
			Rect{ static_cast<float>(x), static_cast<float>(y),
			      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
			radius, m_style.border.value);

	}

	// 文字颜色来自 TextStyle（TextWidget::m_style.foreground——单一视觉真相）
	DrawTextContent(ctx, x, y);

}

}
