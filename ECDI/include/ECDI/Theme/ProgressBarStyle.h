#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief ProgressBar 专属样式（9.6——轨道/填充双色 + 圆角；无 TextStyle——纯视觉控件无文字）
struct ProgressBarStyle{
	StyleField<Color> trackColor;    ///< 轨道色
	StyleField<Color> fillColor;     ///< 填充色
	StyleField<float> cornerRadius;  ///< 圆角半径——⚠️ 0 = 自动圆角（GetHeight()/2），非真实 0 圆角：
	                                 ///< SetStyle({.cornerRadius = 0}) 得到全圆角而非方角；v0.1 无法表达方角（挂账）
};

/// @brief ProgressBar 样式运行时覆盖（D7——Set() 标记 overridden，后续 ApplyTheme 不覆盖）
struct ProgressBarStyleOverride{
	std::optional<Color> trackColor;
	std::optional<Color> fillColor;
	std::optional<float> cornerRadius;
};

}
