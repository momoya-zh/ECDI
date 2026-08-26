#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief CheckBox 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复，单一视觉真相）
struct CheckBoxStyle{
	StyleField<Color> border;            ///< 状态框边框色
	StyleField<float> borderWidth;       ///< 边框宽
	StyleField<float> cornerRadius;      ///< 圆角（0 = 直角；>0 用 DrawRoundedRect）
	StyleField<Color> background;        ///< 状态框背景（未选中）
	StyleField<Color> checkedBackground; ///< 状态框背景（选中）
	StyleField<Color> checkmark;         ///< 勾色
	StyleField<Color> focusBorder;       ///< 焦点态边框色
	StyleField<float> boxSize;           ///< 状态框边长（默认 16）
};

/// @brief CheckBox 样式运行时覆盖（无 foreground——文字经 TextStyleOverride）
struct CheckBoxStyleOverride{
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<float> cornerRadius;
	std::optional<Color> background;
	std::optional<Color> checkedBackground;
	std::optional<Color> checkmark;
	std::optional<Color> focusBorder;
	std::optional<float> boxSize;
};

}
