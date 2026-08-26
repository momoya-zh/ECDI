#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief Radio 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复，单一视觉真相）
struct RadioStyle{
	StyleField<Color> border;        ///< 外圆边框色
	StyleField<float> borderWidth;   ///< 边框宽
	StyleField<Color> background;    ///< 外圆背景
	StyleField<Color> dot;           ///< 选中圆点色
	StyleField<Color> focusBorder;   ///< 焦点态边框色
	StyleField<float> circleSize;    ///< 外圆直径（默认 16；内点 = 40% 比例，不暴露独立参数——YAGNI）
};

/// @brief Radio 样式运行时覆盖（无 foreground——文字经 TextStyleOverride）
struct RadioStyleOverride{
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<Color> background;
	std::optional<Color> dot;
	std::optional<Color> focusBorder;
	std::optional<float> circleSize;
};

}
