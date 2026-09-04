#include "ECDI/Theme/DefaultTheme.h"

namespace ECDI{

DefaultTheme::DefaultTheme(){}

TextStyle DefaultTheme::GetTextStyle() const{
	TextStyle s;
	s.foreground.value = Color::Black();   // Label/TextBox 默认黑字（迁移前 TextWidget 默认 m_textColor = Black）
	s.font.value        = Font{};           // 默认字体（14.0f + 系统默认——Core/Font.h 默认值）
	return s;
}

ButtonStyle DefaultTheme::GetButtonStyle() const{
	ButtonStyle s;
	s.background.value         = Color::FromRGBA8(80, 120, 220);   // 迁移前 Button 正常态蓝
	s.border.value             = Color::White();                     // 迁移前 Button 焦点白框
	s.borderWidth.value        = 2.0f;                               // 迁移前 Button 内缩 2px
	s.cornerRadius.value       = 0.0f;                               // 迁移前 Button 无圆角（Phase 9 开放消费）
	s.pressedBackground.value  = Color::FromRGBA8(60, 90, 180);     // 迁移前 Button 按下深蓝
	s.hoverBackground.value    = Color::FromRGBA8(80, 120, 220);   // P1：hover 默认 = background（零视觉变化；demo 改底色须同设 hover 色）
	return s;
}

TextBoxStyle DefaultTheme::GetTextBoxStyle() const{
	TextBoxStyle s;
	s.background.value  = Color::White();                       // 迁移前 TextBox 白底
	s.border.value      = Color::FromRGBA8(80, 120, 220);      // 迁移前 TextBox 焦点蓝框
	// 9.6 收尾：borderWidth 字段已删除（旧"内缩量"语义随方案 B 移除——改用 DrawFocusRect 后无消费者，
	// 遵守"StyleField 存了但没用会误导 SetStyle 用户"纪律）
	s.selection.value   = Color::FromRGBA8(173, 216, 230);     // 迁移前 TextBox 选区浅蓝
	s.composition.value = Color::FromRGBA8(80, 120, 220);      // 迁移前 TextBox 组合串下划线蓝
	s.caretWidth.value  = 2.0f;                                 // 迁移前 TextBox 光标宽 2px
	// 9.6 收尾方案 B：padding 语义回归「样式内边距」——常驻布局属性，默认 0
	//（旧默认 2.0f 是"焦点内缩"，会让焦点切换时文本/可视区/滚动上限/点击定位整体位移 2px）
	s.padding.value     = 0.0f;
	// P1 形态（modelprobe-p1 §6）：cornerRadius=0 直角 / borderWidth=0 无恒显边框 / borderColor 透明——现状不变（零回归）
	s.cornerRadius.value = 0.0f;
	s.borderWidth.value  = 0.0f;
	s.borderColor.value  = Color::FromRGBA8(0, 0, 0, 0);
	return s;
}

PanelStyle DefaultTheme::GetPanelStyle() const{
	PanelStyle s;
	// 2026-08-30 变更（phase9.6-panel-container-semantics v1.1）：默认透明——镶板 = 隐形布局容器；
	// 需要底色的实例经 SetStyle(PanelStyleOverride) 显式设色（原迁移默认为 Color::Gray()）
	s.background.value = Color::FromRGBA8(0, 0, 0, 0);
	// P1 形态：cornerRadius=0 / borderWidth=0 / borderColor 透明——透明容器语义不变（零回归）
	s.cornerRadius.value = 0.0f;
	s.borderWidth.value  = 0.0f;
	s.borderColor.value  = Color::FromRGBA8(0, 0, 0, 0);
	return s;
}

CheckBoxStyle DefaultTheme::GetCheckBoxStyle() const{
	// 6.2 v0.1 默认视觉：黑框白底黑勾，焦点蓝框（与 Button/TextBox 焦点色一致）
	CheckBoxStyle s;
	s.border.value            = Color::Black();
	s.borderWidth.value       = 1.0f;
	s.cornerRadius.value      = 0.0f;
	s.background.value        = Color::White();
	s.checkedBackground.value = Color::White();
	s.checkmark.value         = Color::Black();
	s.focusBorder.value       = Color::FromRGBA8(80, 120, 220);
	s.boxSize.value           = 16.0f;
	return s;
}

RadioStyle DefaultTheme::GetRadioStyle() const{
	// 6.2 v0.1 默认视觉：黑圈白底黑点，焦点蓝框；内点 = 40% 比例（非独立参数——YAGNI）
	RadioStyle s;
	s.border.value       = Color::Black();
	s.borderWidth.value  = 1.0f;
	s.background.value   = Color::White();
	s.dot.value          = Color::Black();
	s.focusBorder.value  = Color::FromRGBA8(80, 120, 220);
	s.circleSize.value   = 16.0f;
	return s;
}

ProgressBarStyle DefaultTheme::GetProgressBarStyle() const{
	// 9.6 默认视觉：浅灰轨道 + 主题蓝填充（与 Button/TextBox 焦点色协调——80,120,220 全仓统一）
	// cornerRadius 0 = 自动圆角（GetHeight()/2——详设 §2.5 冻结语义，非真实 0 圆角）
	ProgressBarStyle s;
	s.trackColor.value   = Color::FromRGBA8(220, 220, 230);
	s.fillColor.value    = Color::FromRGBA8(80, 120, 220);
	s.cornerRadius.value = 0.0f;
	return s;
}

const DefaultTheme& GetDefaultTheme(){
	static DefaultTheme instance;   // static local = 首次调用构造，非程序启动
	return instance;
}

}
