#pragma once

#include "ECDI/Theme/Theme.h"

namespace ECDI{

/// @brief 默认主题实现（继承 Theme——多态：ApplyTheme(const Theme&) 可接受）
/// @details 非 Singleton：自由函数 GetDefaultTheme() + static local（不锁死全局访问点——
/// 未来可演进为 Window 持有 Theme& / LightTheme/DarkTheme，无需改动架构）
class DefaultTheme : public Theme{
public:
	DefaultTheme();

	TextStyle    GetTextStyle() const override;
	ButtonStyle  GetButtonStyle() const override;
	TextBoxStyle GetTextBoxStyle() const override;
	PanelStyle   GetPanelStyle() const override;
};

/// @brief 获取当前默认主题实例（static local = 首次调用构造，非程序启动）
/// @details 不引入 ThemeManager 全局状态；未来可改为参数传入或 Window 持有
const DefaultTheme& GetDefaultTheme();

}
