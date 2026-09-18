#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Theme/StyleField.h"

#include <optional>

namespace ECDI{

/// @brief ScrollBar 专属样式（Phase 15——轨道/滑块三态色 + 圆角 + 厚度）
/// @details 与 ProgressBarStyle 同构（StyleField 携带 override 标志位——D7 契约）。
/// ⚠️ **thickness 放在 Style 而非控件常量**：它不是视觉细节，而是**参与 viewport 计算**的几何量
/// （详设 §3.3 双轴判定要扣它）——必须与主题同源，否则主题切换后 maxOffset 会与实际可滚区不符。
struct ScrollBarStyle{
	StyleField<Color> trackColor;         ///< 轨道底色
	StyleField<Color> thumbColor;         ///< 滑块常态色
	StyleField<Color> thumbHoverColor;    ///< 滑块 hover 色
	StyleField<Color> thumbPressedColor;  ///< 滑块按下/拖拽色
	StyleField<float> cornerRadius;       ///< 滑块圆角（0 = 直角）
	StyleField<int>   thickness;          ///< 条厚度（占用视口的宽/高——**参与 viewport 计算**）
};

/// @brief ScrollBar 样式运行时覆盖（D7——Set() 标记 overridden，后续 ApplyTheme 不覆盖）
struct ScrollBarStyleOverride{
	std::optional<Color> trackColor;
	std::optional<Color> thumbColor;
	std::optional<Color> thumbHoverColor;
	std::optional<Color> thumbPressedColor;
	std::optional<float> cornerRadius;
	std::optional<int>   thickness;
};

}
