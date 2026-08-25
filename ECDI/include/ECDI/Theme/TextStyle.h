#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"

#include <optional>

namespace ECDI{

/// @brief 文本控件共享样式（TextWidget/Label/Button/TextBox 的文字视觉——唯一来源）
/// @details 单一视觉真相：文字颜色/字体只在此定义；ButtonStyle/TextBoxStyle 禁止重复 foreground（Phase 9 锁死）
struct TextStyle{
	StyleField<Color> foreground;    ///< 文字颜色
	StyleField<Font> font;           ///< 字体
};

/// @brief 文本样式运行时覆盖
struct TextStyleOverride{
	std::optional<Color> foreground;
	std::optional<Font> font;
};

}
