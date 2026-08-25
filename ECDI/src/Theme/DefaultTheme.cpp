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
	return s;
}

TextBoxStyle DefaultTheme::GetTextBoxStyle() const{
	TextBoxStyle s;
	s.background.value  = Color::White();                       // 迁移前 TextBox 白底
	s.border.value      = Color::FromRGBA8(80, 120, 220);      // 迁移前 TextBox 焦点蓝框
	s.borderWidth.value = 2.0f;                                 // 迁移前 TextBox 内缩 2px
	s.selection.value   = Color::FromRGBA8(173, 216, 230);     // 迁移前 TextBox 选区浅蓝
	s.composition.value = Color::FromRGBA8(80, 120, 220);      // 迁移前 TextBox 组合串下划线蓝
	s.caretWidth.value  = 2.0f;                                 // 迁移前 TextBox 光标宽 2px
	s.padding.value     = 2.0f;                                 // 迁移前 TextBox 焦点内缩 2px
	return s;
}

PanelStyle DefaultTheme::GetPanelStyle() const{
	PanelStyle s;
	s.background.value = Color::Gray();   // 迁移前 Panel 灰底
	return s;
}

const DefaultTheme& GetDefaultTheme(){
	static DefaultTheme instance;   // static local = 首次调用构造，非程序启动
	return instance;
}

}
