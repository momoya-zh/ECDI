#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief TextBox 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复，单一视觉真相）
struct TextBoxStyle{
	StyleField<Color> background;    ///< 背景色
	StyleField<Color> border;        ///< 焦点边框色
	StyleField<float> borderWidth;   ///< 边框宽度（内缩量）
	StyleField<Color> selection;     ///< 选区高亮色
	StyleField<Color> composition;   ///< 组合串下划线色（IME）
	StyleField<float> caretWidth;    ///< 光标竖线宽
	StyleField<float> padding;       ///< 焦点态内缩量（当前语义：焦点框与文本内容区之间的 inset）
};

/// @brief TextBox 样式运行时覆盖（无 foreground——文字经 TextStyleOverride）
struct TextBoxStyleOverride{
	std::optional<Color> background;
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<Color> selection;
	std::optional<Color> composition;
	std::optional<float> caretWidth;
	std::optional<float> padding;
};

}
